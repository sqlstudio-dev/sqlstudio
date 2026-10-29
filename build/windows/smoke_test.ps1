<#
.SYNOPSIS
Smoke test for an unpacked Windows build of SqlStudio.

.DESCRIPTION
Starts SqlStudio.exe, waits for its main window to appear and checks that the
application is still running a while later without Python errors.

The output directory receives the console output, the application's own log
(wb.log) and, in CI only (CI is set), a screenshot of the desktop, for
inspection when the test fails.

.EXAMPLE
.\smoke_test.ps1 -AppDir 'C:\SqlStudio 1.0.1 (winx64)' -OutDir smoke-test
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$AppDir,
    [string]$OutDir = 'smoke-test',
    # Seconds to wait for the main window.
    [int]$WindowTimeout = 120,
    # Seconds the app must keep running after that.
    [int]$SettleTime = 20
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$exe = Join-Path (Resolve-Path -LiteralPath $AppDir).Path 'SqlStudio.exe'
[void][System.IO.Directory]::CreateDirectory($OutDir)
$OutDir = (Resolve-Path -LiteralPath $OutDir).Path
$wbLog = Join-Path $env:APPDATA 'MySQL\SqlStudio\log\wb.log'

function Fail([string]$Message) {
    Write-Host "::error::Smoke test: $Message"
    exit 1
}

function Save-Screenshot([string]$Path) {
    Add-Type -AssemblyName System.Windows.Forms, System.Drawing
    $bounds = [System.Windows.Forms.SystemInformation]::VirtualScreen
    $bitmap = [System.Drawing.Bitmap]::new($bounds.Width, $bounds.Height)
    try {
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        $graphics.CopyFromScreen($bounds.Location, [System.Drawing.Point]::Empty, $bounds.Size)
        $graphics.Dispose()
        $bitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    } finally {
        $bitmap.Dispose()
    }
}

# The previous run's log would hide errors of this one.
Remove-Item -LiteralPath $wbLog -ErrorAction SilentlyContinue

Write-Host "Starting $exe ..."
$process = Start-Process -FilePath $exe -WorkingDirectory (Split-Path -Parent $exe) -PassThru `
    -RedirectStandardOutput (Join-Path $OutDir 'console.log') -RedirectStandardError (Join-Path $OutDir 'console-err.log')
try {
    $waited = 0
    while ($true) {
        $process.Refresh()
        if ($process.HasExited) { Fail "SqlStudio exited with code $($process.ExitCode) before showing a window" }
        if ($process.MainWindowHandle -ne [IntPtr]::Zero) { break }
        if ($waited -ge $WindowTimeout) { Fail "no window appeared within ${WindowTimeout}s" }
        Start-Sleep -Seconds 2
        $waited += 2
    }
    Write-Host "First window appeared after about ${waited}s: '$($process.MainWindowTitle)'"

    Start-Sleep -Seconds $SettleTime
    $process.Refresh()
    if ($process.HasExited) { Fail "SqlStudio exited with code $($process.ExitCode) within ${SettleTime}s of starting" }
    Write-Host "Still running after another ${SettleTime}s, main window '$($process.MainWindowTitle)'."

    # An unhandled exception shows the app's own error dialog instead of the
    # main window.
    if ($process.MainWindowTitle -match 'error|exception') {
        Fail "the main window is an error dialog: '$($process.MainWindowTitle)'"
    }

    $logs = @(Join-Path $OutDir 'console.log'; Join-Path $OutDir 'console-err.log')
    if (Test-Path -LiteralPath $wbLog) { $logs += $wbLog }
    if (-not ((Get-Content -LiteralPath $wbLog -ErrorAction SilentlyContinue) -match 'UI is up')) {
        Write-Host '::warning::wb.log has no "UI is up" line.'
    }

    # Python plugins and modules report load errors as tracebacks.
    $tracebacks = @(Select-String -LiteralPath $logs -Pattern 'Traceback')
    if ($tracebacks.Count) {
        $tracebacks | ForEach-Object { Write-Host $_ }
        Fail 'Python errors in the application output'
    }

    # Errors logged by the application itself are reported but not fatal.
    Select-String -LiteralPath $logs -Pattern '\[ERR\]' | ForEach-Object { Write-Host "::warning::$($_.Line)" }

    Write-Host 'Smoke test passed.'
} finally {
    # Collected on every exit, so a failure can be inspected too. The
    # screenshot shows the whole desktop, so it is only taken in CI, never on
    # a developer's machine.
    if ($env:CI) {
        try { Save-Screenshot (Join-Path $OutDir 'screenshot.png') } catch { Write-Host "::warning::No screenshot: $_" }
    }
    Copy-Item -LiteralPath $wbLog -Destination $OutDir -ErrorAction SilentlyContinue
    if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue }
}
