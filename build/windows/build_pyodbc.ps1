<#
.SYNOPSIS
Builds the pyodbc module against the bundle's Python and the Windows ODBC driver manager.

.DESCRIPTION
The migration plugins (MSSQL, Access, Sybase, generic ODBC) import pyodbc.
The module is compiled here with MSVC from the pyodbc source archive, the way
pyodbc's setup.py would, against the bundle's Python headers and odbc32.lib
from the Windows SDK. PrepareOutputDir.ps1 then copies it from the bundle's
Python directory to python\site-packages, which the app puts on sys.path.

.EXAMPLE
.\build_pyodbc.ps1 -BundleDir C:\bundle -Sdist C:\downloads\pyodbc-5.3.0.tar.gz
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$BundleDir,
    [Parameter(Mandatory)][string]$Sdist,
    # Defaults to the bundle's Python directory, where PrepareOutputDir.ps1 looks.
    [string]$OutDir,
    # MSVC toolset to compile with; "auto" takes the newest one installed.
    [ValidateSet("auto", "v143", "v145")]
    [string]$PlatformToolset = "auto"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$python = Join-Path $BundleDir 'Python'
$pythonExe = Join-Path $python 'python.exe'
foreach ($required in @($pythonExe, (Join-Path $python 'Include\Python.h'), (Join-Path $python 'libs\python312.lib'))) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "The bundle has no $required" }
}
if (-not (Test-Path -LiteralPath $Sdist -PathType Leaf)) { throw "-Sdist must point at a pyodbc .tar.gz" }
if (-not $OutDir) { $OutDir = $python }

$vsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsPath = (& $vsWhere -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
if (-not $vsPath) { throw 'No Visual Studio installation with the x64 C++ tools was found.' }
$vcVars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvarsall.bat'
$vcVarsArgs = 'x64'
if ($PlatformToolset -ne 'auto') {
    $toolsVersionFile = Join-Path $vsPath "VC\Auxiliary\Build\Microsoft.VCToolsVersion.$PlatformToolset.default.txt"
    if (-not (Test-Path -LiteralPath $toolsVersionFile -PathType Leaf)) { throw "The $PlatformToolset toolset is not installed under $vsPath" }
    $vcVarsArgs += ' -vcvars_ver=' + (Get-Content -LiteralPath $toolsVersionFile -TotalCount 1).Trim()
}

$workDir = Join-Path ([System.IO.Path]::GetTempPath()) ("pyodbc-" + [guid]::NewGuid().ToString('N'))
[void][System.IO.Directory]::CreateDirectory($workDir)
try {
    & tar.exe -xzf $Sdist -C $workDir
    if ($LASTEXITCODE) { throw "tar failed with exit code $LASTEXITCODE" }
    $src = @(Get-ChildItem -LiteralPath $workDir -Directory -Filter 'pyodbc-*')[0].FullName
    $version = (Split-Path -Leaf $src).Substring('pyodbc-'.Length)
    $suffix = (& $pythonExe -I -c 'import importlib.machinery as m; print(m.EXTENSION_SUFFIXES[0])').Trim()
    if ($LASTEXITCODE -or -not $suffix) { throw 'Could not read the extension suffix from the bundle Python.' }

    [void][System.IO.Directory]::CreateDirectory($OutDir)
    $target = Join-Path $OutDir "pyodbc$suffix"
    $sources = (Get-ChildItem -LiteralPath (Join-Path $src 'src') -Filter '*.cpp' | ForEach-Object { "`"$($_.FullName)`"" }) -join ' '
    $cl = "cl.exe /nologo /LD /O2 /MD /EHsc /std:c++17 /DNDEBUG /DPYODBC_VERSION=$version " +
          "/I`"$python\Include`" /Fo`"$workDir\\`" $sources " +
          "/link /LIBPATH:`"$python\libs`" odbc32.lib /OUT:`"$target`""
    cmd.exe /c "`"$vcVars`" $vcVarsArgs >nul && $cl"
    if ($LASTEXITCODE) { throw "cl.exe failed with exit code $LASTEXITCODE" }
    # The import library and export file are only build by-products.
    Remove-Item -LiteralPath ([System.IO.Path]::ChangeExtension($target, '.lib')), ([System.IO.Path]::ChangeExtension($target, '.exp')) -ErrorAction SilentlyContinue

    Write-Host "Built $target (pyodbc $version)"
} finally {
    Remove-Item -LiteralPath $workDir -Recurse -Force -ErrorAction SilentlyContinue
}
