[CmdletBinding()]
<#
.SYNOPSIS
Build and run the native studio.testing test suite on Windows.

.DESCRIPTION
Builds required SqlStudio targets, builds testing\test-suite\studio.testing.vcxproj,
prepares runtime test data, and optionally runs studio.testing.exe.

.PARAMETER Action
Operation to perform:
  build - build SqlStudio + studio.testing and prepare runtime output.
  run   - optionally build first, then run studio.testing.exe.
  test  - run studio.testing.exe and auto-skip build when binaries are up to date (or force skip with -SkipBuild).

.PARAMETER Configuration
Build configuration: Debug or Release.

.PARAMETER Platform
Build platform. Currently only x64 is supported.

.PARAMETER PlatformToolset
MSVC platform toolset: auto, v143, or v145.

.PARAMETER VisualStudio
Visual Studio selection: latest, 2026, or 2022.

.PARAMETER VsInstallPath
Explicit Visual Studio installation path.

.PARAMETER BundleDir
Path to the 3rd-party bundle (MSS_3DPARTY_PATH).

.PARAMETER SolutionPath
Path to SqlStudio.sln.

.PARAMETER Jobs
Parallel build worker count passed to MSBuild (/m).

.PARAMETER TestArgs
Arguments forwarded to studio.testing.exe when Action=run.

.PARAMETER SkipBuild
Skip build/preparation steps when Action=run or Action=test.

.PARAMETER Help
Print script usage text.

.EXAMPLE
.\testing\test-suite\compile.ps1 -Action build -Configuration Debug

.EXAMPLE
.\testing\test-suite\compile.ps1 -Action run -Configuration Debug -TestArgs --gtest_filter=*mysqlcanvas*

.EXAMPLE
Get-Help .\testing\test-suite\compile.ps1 -Detailed
#>
param(
    [ValidateSet("build", "run", "test")]
    [string]$Action = "build",

    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",

    [ValidateSet("x64")]
    [string]$Platform = "x64",

    [ValidateSet("auto", "v143", "v145")]
    [string]$PlatformToolset = "auto",

    [ValidateSet("latest", "2026", "2022")]
    [string]$VisualStudio = "latest",

    [string]$VsInstallPath,
    [string]$BundleDir,
    [string]$SolutionPath,
    [int]$Jobs = [Math]::Max(1, [Environment]::ProcessorCount),
    [string[]]$TestArgs = @(),
    [switch]$SkipBuild,
    [Alias("h", "?")]
    [switch]$Help
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$script:ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$script:ProjectRoot = [System.IO.Path]::GetFullPath((Join-Path $script:ScriptDir "..\.."))
$script:ProjectParent = Split-Path -Parent $script:ProjectRoot
$script:ProjectPath = Join-Path $script:ScriptDir "studio.testing.vcxproj"
$script:SolutionPath = $null
$script:MSBuildPath = $null
$script:ResolvedPlatformToolset = $null

function Write-Info([string]$Message) { Write-Host "[INFO] $Message" -ForegroundColor Cyan }
function Write-Ok([string]$Message) { Write-Host "[ OK ] $Message" -ForegroundColor Green }
function Write-Warn([string]$Message) { Write-Host "[WARN] $Message" -ForegroundColor Yellow }
function Write-Section([string]$Title) { Write-Host ""; Write-Host ("-- {0} --" -f $Title) -ForegroundColor Magenta }

function Show-Usage {
    Write-Host "Test-suite build helper" -ForegroundColor Magenta
    Write-Host ""
    Write-Host "Usage:" -ForegroundColor Yellow
    Write-Host "  powershell -NoProfile -File .\testing\test-suite\compile.ps1 [-Action build|run|test] [options]" -ForegroundColor Gray
    Write-Host ""
    Write-Host "Options:" -ForegroundColor Yellow
    Write-Host "  -Configuration Debug|Release      Default: Debug" -ForegroundColor Gray
    Write-Host "  -Platform x64                     Default: x64" -ForegroundColor Gray
    Write-Host "  -PlatformToolset auto|v143|v145   Default: auto" -ForegroundColor Gray
    Write-Host "  -VisualStudio latest|2026|2022    Default: latest" -ForegroundColor Gray
    Write-Host "  -VsInstallPath <path>             Optional" -ForegroundColor Gray
    Write-Host "  -BundleDir <path>                 Optional (uses MSS_3DPARTY_PATH or common bundle paths)" -ForegroundColor Gray
    Write-Host "  -SolutionPath <path>              Optional (defaults to SqlStudio.sln)" -ForegroundColor Gray
    Write-Host "  -Jobs <n>                         Default: CPU count" -ForegroundColor Gray
    Write-Host "  -TestArgs <args>                  Extra args passed to studio.testing.exe" -ForegroundColor Gray
    Write-Host "  -SkipBuild                        For -Action run or -Action test" -ForegroundColor Gray
    Write-Host "  -Help                             Show this text" -ForegroundColor Gray
    Write-Host "" 
    Write-Host "Notes:" -ForegroundColor Yellow
    Write-Host "  -Action test automatically skips build when studio.testing.exe is newer than studio.testing.vcxproj." -ForegroundColor Gray
    Write-Host ""
    Write-Host "Examples:" -ForegroundColor Yellow
    Write-Host "  .\testing\test-suite\compile.ps1 -Action build -Configuration Debug" -ForegroundColor Green
    Write-Host "  .\testing\test-suite\compile.ps1 -Action run -Configuration Debug -TestArgs --gtest_filter=*mysqlcanvas*" -ForegroundColor Green
    Write-Host "  .\testing\test-suite\compile.ps1 -Action test -Configuration Debug" -ForegroundColor Green
}

function Get-VSWhere {
    $cmd = Get-Command "vswhere.exe" -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }

    $fallback = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $fallback) { return $fallback }

    throw "vswhere.exe was not found."
}

function Get-VSInstallPath {
    if ($VsInstallPath) {
        if (-not (Test-Path -LiteralPath $VsInstallPath -PathType Container)) {
            throw "Visual Studio install path was not found: $VsInstallPath"
        }
        return [System.IO.Path]::GetFullPath($VsInstallPath)
    }

    $vsWhere = Get-VSWhere
    $args = @("-latest")
    if ($VisualStudio -eq "2026") {
        $args += @("-version", "[18,19)")
    } elseif ($VisualStudio -eq "2022") {
        $args += @("-version", "[17,18)")
    }
    $args += @("-requires", "Microsoft.Component.MSBuild", "-property", "installationPath")

    $path = (& $vsWhere @args).Trim()
    if (-not $path) {
        throw "A compatible Visual Studio installation with MSBuild support was not found."
    }

    return $path
}

function Resolve-PlatformToolset([string]$VsPath) {
    if ($PlatformToolset -ne "auto") {
        return $PlatformToolset
    }

    if ($VsPath -match "\\Microsoft Visual Studio\\18\\") {
        return "v145"
    }

    return "v143"
}

function Enter-VsEnvironment {
    $vsPath = Get-VSInstallPath
    $script:ResolvedPlatformToolset = Resolve-PlatformToolset -VsPath $vsPath

    $vcVars = Join-Path $vsPath "VC\Auxiliary\Build\vcvarsall.bat"
    $msbuildCandidate = Join-Path $vsPath "MSBuild\Current\Bin\MSBuild.exe"
    if (-not (Test-Path -LiteralPath $vcVars -PathType Leaf)) {
        throw "vcvarsall.bat was not found under $vsPath"
    }
    if (-not (Test-Path -LiteralPath $msbuildCandidate -PathType Leaf)) {
        throw "MSBuild.exe was not found under $vsPath"
    }

    Write-Info "Loading Visual Studio environment from $vsPath"
    Push-Location (Split-Path -Parent $vcVars)
    try {
        cmd /c "`"$vcVars`" x64 >nul && set" | ForEach-Object {
            if ($_ -match "=") {
                $parts = $_ -split "=", 2
                [Environment]::SetEnvironmentVariable($parts[0], $parts[1], "Process")
            }
        }
    }
    finally {
        Pop-Location
    }

    $script:MSBuildPath = $msbuildCandidate
    Write-Ok "Visual Studio environment ready."
}

function Get-DefaultBundleDir {
    $candidates = @()
    if ($env:MSS_3DPARTY_PATH) { $candidates += $env:MSS_3DPARTY_PATH }
    $candidates += @(
        (Join-Path $script:ProjectParent "wb_build\bundle"),
        (Join-Path $script:ProjectParent "bundle"),
        (Join-Path $script:ProjectRoot "bundle")
    )

    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate -PathType Container)) {
            return [System.IO.Path]::GetFullPath($candidate)
        }
    }

    throw "Could not find a 3rd-party bundle directory. Pass -BundleDir or set MSS_3DPARTY_PATH."
}

function Invoke-LoggedCommand([string]$Label, [string]$FilePath, [string[]]$Arguments, [string]$WorkingDirectory) {
    Write-Info ("{0}: {1} {2}" -f $Label, $FilePath, ($Arguments -join " "))
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    Push-Location $WorkingDirectory
    try {
        & $FilePath @Arguments
        $exitCode = $LASTEXITCODE
        if ($null -eq $exitCode) { $exitCode = 0 }
    }
    finally {
        Pop-Location
        $sw.Stop()
    }

    if ($exitCode -ne 0) {
        throw ("Command failed: {0} (exit code {1})" -f $Label, $exitCode)
    }

    Write-Ok ("{0} ({1:n1}s)" -f $Label, $sw.Elapsed.TotalSeconds)
}

function Build-TestSuite {
    $msbuildArgs = @(
        $script:ProjectPath,
        "/t:Build",
        "/m:$Jobs",
        "/p:Configuration=$Configuration",
        "/p:Platform=$Platform",
        "/p:SolutionDir=$($script:ProjectRoot)\\",
        "/p:MSS_3DPARTY_PATH=$script:BundleDir",
        "/p:PlatformToolset=$script:ResolvedPlatformToolset"
    )

    Invoke-LoggedCommand -Label "build studio.testing" -FilePath $script:MSBuildPath -Arguments $msbuildArgs -WorkingDirectory $script:ProjectRoot
    Prepare-TestRuntime
}

function Build-StudioTarget {
    $msbuildArgs = @(
        $script:SolutionPath,
        "/t:SqlStudio",
        "/m:$Jobs",
        "/p:Configuration=$Configuration",
        "/p:Platform=$Platform",
        "/p:MSS_3DPARTY_PATH=$script:BundleDir",
        "/p:PlatformToolset=$script:ResolvedPlatformToolset"
    )

    Invoke-LoggedCommand -Label "build solution target SqlStudio" -FilePath $script:MSBuildPath -Arguments $msbuildArgs -WorkingDirectory $script:ProjectRoot
}

function Prepare-TestRuntime {
    $prepareScript = Join-Path $script:ScriptDir "PrepareOutputDir.cmd"
    if (-not (Test-Path -LiteralPath $prepareScript -PathType Leaf)) {
        throw "PrepareOutputDir.cmd was not found: $prepareScript"
    }

    Invoke-LoggedCommand -Label "prepare test runtime" -FilePath $prepareScript -Arguments @(
        "$($script:ProjectRoot)\",
        $Configuration,
        $Platform
    ) -WorkingDirectory $script:ProjectRoot
}

function Get-TestExecutablePath {
    return Join-Path $script:ScriptDir ("bin\{0}\{1}\studio.testing.exe" -f $Platform, $Configuration)
}

function Run-TestSuite {
    $testExe = Get-TestExecutablePath
    if (-not (Test-Path -LiteralPath $testExe -PathType Leaf)) {
        throw "Unit test executable was not found: $testExe"
    }

    $studioOutputDir = Join-Path $script:ProjectRoot ("bin\{0}\{1}" -f $Platform, $Configuration)
    $testOutputDir = Join-Path $script:ScriptDir ("bin\{0}\{1}" -f $Platform, $Configuration)
    $env:PATH = "$testOutputDir;$studioOutputDir;$($script:BundleDir)\bin;$($script:BundleDir)\lib;$($script:BundleDir)\debug\lib;$env:PATH"

    Invoke-LoggedCommand -Label "run studio.testing" -FilePath $testExe -Arguments $TestArgs -WorkingDirectory $script:ScriptDir
}

function Test-IsTestBuildUpToDate {
    $testExe = Get-TestExecutablePath
    if (-not (Test-Path -LiteralPath $testExe -PathType Leaf)) {
        Write-Info "studio.testing.exe not found, build is required."
        return $false
    }

    $exeTime = (Get-Item -LiteralPath $testExe).LastWriteTimeUtc
    $projectTime = (Get-Item -LiteralPath $script:ProjectPath).LastWriteTimeUtc

    if ($exeTime -lt $projectTime) {
        Write-Info "studio.testing.vcxproj is newer than studio.testing.exe, build is required."
        return $false
    }

    Write-Info "studio.testing.exe is up to date, skipping build."
    return $true
}

function Invoke-Testing {
    Build-StudioTarget
    Build-TestSuite
    Run-TestSuite
}

if ($Help) {
    Show-Usage
    exit 0
}

if (-not (Test-Path -LiteralPath $script:ProjectPath -PathType Leaf)) {
    throw "Project file was not found: $script:ProjectPath"
}

if (-not $SolutionPath) {
    $SolutionPath = Join-Path $script:ProjectRoot "SqlStudio.sln"
}
$script:SolutionPath = [System.IO.Path]::GetFullPath($SolutionPath)
if (-not (Test-Path -LiteralPath $script:SolutionPath -PathType Leaf)) {
    throw "Solution file was not found: $script:SolutionPath"
}

if (-not $BundleDir) {
    $BundleDir = Get-DefaultBundleDir
}
$script:BundleDir = [System.IO.Path]::GetFullPath($BundleDir)
[Environment]::SetEnvironmentVariable("MSS_3DPARTY_PATH", $script:BundleDir, "Process")
[Environment]::SetEnvironmentVariable("WB_3DPARTY_LIB", $script:BundleDir, "Process")

Enter-VsEnvironment

Write-Section "Configuration"
Write-Info "Action            : $Action"
Write-Info "Solution          : $script:SolutionPath"
Write-Info "Project           : $script:ProjectPath"
Write-Info "Configuration     : $Configuration"
Write-Info "Platform          : $Platform"
Write-Info "Platform toolset  : $script:ResolvedPlatformToolset"
Write-Info "Bundle            : $script:BundleDir"
Write-Info "Jobs              : $Jobs"
if ($TestArgs.Count -gt 0) {
    Write-Info ("Test args         : {0}" -f ($TestArgs -join " "))
}

switch ($Action) {
    "build" {
        Write-Section "Build"
        Build-StudioTarget
        Build-TestSuite
    }
    "run" {
        Write-Section "Run"
        if (-not $SkipBuild) {
            Build-StudioTarget
            Build-TestSuite
        }
        Run-TestSuite
    }
    "test" {
        Write-Section "Test"
        if ($SkipBuild) {
            Run-TestSuite
        }
        elseif (Test-IsTestBuildUpToDate) {
            Run-TestSuite
        }
        else {
            Invoke-Testing
        }
    }
}
