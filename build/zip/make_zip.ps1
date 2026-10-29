<#
.SYNOPSIS
Packs a Windows build of SqlStudio into a portable .zip.

.DESCRIPTION
Copies the build output without build by-products, adds the Visual C++
runtime DLLs next to SqlStudio.exe (app-local deployment, so the .zip runs
on a machine without the VC++ Redistributable), byte-compiles the Python
sources as a syntax check, and writes the .zip with a .sha256 file next to it.

.EXAMPLE
.\build\zip\make_zip.ps1 -BinDir bin\x64\Release -OutDir dist
#>
[CmdletBinding()]
param(
    [string]$BinDir,
    [string]$OutDir,
    # Defaults to the version in backend\wbprivate\studio\wb_version.h.
    [string]$Version
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if (-not $BinDir) { $BinDir = Join-Path $root 'bin\x64\Release' }
if (-not $OutDir) { $OutDir = Join-Path $PSScriptRoot 'distribution' }
$BinDir = (Resolve-Path -LiteralPath $BinDir).Path
if (-not (Test-Path -LiteralPath (Join-Path $BinDir 'SqlStudio.exe') -PathType Leaf)) {
    throw "$BinDir does not contain a build (SqlStudio.exe is missing)."
}

if (-not $Version) {
    $header = Get-Content -LiteralPath (Join-Path $root 'backend\wbprivate\studio\wb_version.h')
    $numbers = foreach ($name in 'APP_MAJOR_NUMBER', 'APP_MINOR_NUMBER', 'APP_RELEASE_NUMBER') {
        $line = $header | Where-Object { $_ -match "^#define\s+$name\s+(\d+)" } | Select-Object -First 1
        if (-not $line) { throw "$name is missing from wb_version.h" }
        [void]($line -match "^#define\s+$name\s+(\d+)")
        $Matches[1]
    }
    $Version = $numbers -join '.'
}

$name = "sql-studio-$Version-winx64"
$dirName = "SqlStudio $Version (winx64)"
$tmp = Join-Path ([System.IO.Path]::GetTempPath()) ("make-zip-" + [guid]::NewGuid().ToString('N'))
$stage = Join-Path $tmp $dirName
[void][System.IO.Directory]::CreateDirectory($stage)
try {
    Write-Host "Copying $BinDir ..."
    $robocopyArgs = @($BinDir, $stage, '/E', '/R:0', '/W:0', '/NFL', '/NDL', '/NJH', '/NJS', '/NP',
        '/XF', '*.pdb', '*.ilk', '*.exp', '*.lib', '*.metagen', '*vshost.exe', '*vshost.exe.manifest', 'Makefile.am', '*.pyc',
        '/XD', '__pycache__', (Join-Path $BinDir 'python\lib\sqlite3\test'))
    & robocopy.exe @robocopyArgs | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "robocopy failed with exit code $LASTEXITCODE" }

    # The newest x64 CRT that Visual Studio ships for redistribution. It must
    # be at least as new as the toolset the app was built with.
    $vsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $vsPath = (& $vsWhere -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
    if (-not $vsPath) { throw 'No Visual Studio installation with the x64 C++ tools was found.' }
    $crt = Get-ChildItem -Path (Join-Path $vsPath 'VC\Redist\MSVC\*\x64\Microsoft.VC*.CRT') -Directory |
        Where-Object { $_.Parent.Parent.Name -match '^\d+\.\d+\.\d+$' } |
        Sort-Object { [version]$_.Parent.Parent.Name } | Select-Object -Last 1
    if (-not $crt) { throw "No Visual C++ runtime for redistribution was found under $vsPath" }
    Write-Host "Adding the Visual C++ runtime from $($crt.FullName)"
    Copy-Item -Path (Join-Path $crt.FullName '*.dll') -Destination $stage -Force

    # A failure here is a syntax error in a shipped Python file.
    Write-Host 'Byte-compiling the Python sources ...'
    $env:PYTHONHOME = Join-Path $stage 'python'
    & (Join-Path $stage 'python.exe') -m compileall -q -j 0 $stage
    $compileExit = $LASTEXITCODE
    Remove-Item Env:PYTHONHOME
    @(Get-ChildItem -LiteralPath $stage -Recurse -Directory -Filter '__pycache__') | Remove-Item -Recurse -Force
    if ($compileExit) { throw "Python byte-compilation failed with exit code $compileExit" }

    [void][System.IO.Directory]::CreateDirectory($OutDir)
    $zip = Join-Path (Resolve-Path -LiteralPath $OutDir).Path "$name.zip"
    Remove-Item -LiteralPath $zip -ErrorAction SilentlyContinue
    Write-Host "Writing $zip ..."
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [System.IO.Compression.ZipFile]::CreateFromDirectory($stage, $zip, [System.IO.Compression.CompressionLevel]::Optimal, $true)
    $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $zip).Hash.ToLowerInvariant()
    Set-Content -LiteralPath "$zip.sha256" -Value "$hash  $name.zip" -Encoding ascii -NoNewline
    Write-Host ('{0} ({1:N1} MB), SHA256 {2}' -f $zip, ((Get-Item -LiteralPath $zip).Length / 1MB), $hash)
} finally {
    Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
}
