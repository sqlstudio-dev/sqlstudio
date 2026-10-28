#requires -Version 5.1
<#
.SYNOPSIS
Run the repository clang-format check with the pinned clang-format version.
.DESCRIPTION
Runs tools/check_format.py, the same check CI uses, and honors .clang-format and
.clang-format-ignore. The formatter comes from -ClangFormat, from PATH when its
version matches tools/format-requirements.txt, or from a cached virtual environment
that this script creates outside the repository on first use.
.EXAMPLE
.\run-clang-format.ps1
Check C/C++ files changed since HEAD, plus untracked files.
.EXAMPLE
.\run-clang-format.ps1 -Base origin/develop
Check files changed on this branch, as the pull request check does.
.EXAMPLE
.\run-clang-format.ps1 -All
Check every tracked C/C++ file that is not excluded by .clang-format-ignore.
.EXAMPLE
.\run-clang-format.ps1 -Base origin/develop -Fix
Reformat the files changed on this branch in place.
.EXAMPLE
.\run-clang-format.ps1 --fix-format --base origin/develop
The same as the previous example, using the GNU-style options of tools/check_format.py.
#>
[CmdletBinding(PositionalBinding = $false)]
param(
    [string]$Base,
    [switch]$All,
    [Alias('FixFormat')]
    [switch]$Fix,
    [string]$ClangFormat,
    # GNU-style options (--fix-format, --fix, --all, --base REF, --clang-format PATH).
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$Options = @()
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

for ($i = 0; $i -lt $Options.Count; $i++) {
    $option, $value = $Options[$i] -split '=', 2
    switch ($option) {
        { $_ -in '--fix-format', '--fix' } { $Fix = $true; continue }
        '--all' { $All = $true; continue }
        { $_ -in '--base', '--clang-format' } {
            if ($null -eq $value) {
                if (++$i -ge $Options.Count) { throw "$option requires a value." }
                $value = $Options[$i]
            }
            if ($option -eq '--base') { $Base = $value } else { $ClangFormat = $value }
            continue
        }
        default { throw "Unknown option '$($Options[$i])'. Use Get-Help $PSCommandPath for usage." }
    }
}

$root = $PSScriptRoot
$requirements = Join-Path $root 'tools/format-requirements.txt'
$version = ((Get-Content -LiteralPath $requirements -Raw).Trim() -split '==')[1]
$onWindows = [Environment]::OSVersion.Platform -eq [PlatformID]::Win32NT

function Get-ClangFormatVersion([string]$Path) {
    try {
        $output = & $Path --version 2>$null
    } catch {
        return $null
    }
    if ("$output" -match 'clang-format version (\d+\.\d+\.\d+)') { return $Matches[1] }
    return $null
}

function Find-Python {
    # The Windows Store "python3" alias is an app stub, so verify each candidate actually runs.
    foreach ($name in 'python', 'python3', 'py') {
        foreach ($command in @(Get-Command $name -CommandType Application -ErrorAction SilentlyContinue)) {
            try {
                $major = & $command.Source -c 'import sys; print(sys.version_info[0])' 2>$null
            } catch {
                continue
            }
            if ($LASTEXITCODE -eq 0 -and "$major".Trim() -eq '3') { return $command.Source }
        }
    }
    throw 'Python 3 was not found. Install Python 3 and make sure it is on PATH.'
}

$python = Find-Python

if ($ClangFormat) {
    $found = Get-ClangFormatVersion $ClangFormat
    if ($found -ne $version) { throw "clang-format $version is required; '$ClangFormat' reports '$found'." }
} else {
    $onPath = Get-Command clang-format -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($onPath -and (Get-ClangFormatVersion $onPath.Source) -eq $version) {
        $ClangFormat = $onPath.Source
    } else {
        $cacheRoot = if ($onWindows) { $env:LOCALAPPDATA }
                     elseif ($env:XDG_CACHE_HOME) { $env:XDG_CACHE_HOME }
                     else { Join-Path $HOME '.cache' }
        $venv = Join-Path $cacheRoot "sqlstudio/clang-format-$version"
        $binDir = Join-Path $venv $(if ($onWindows) { 'Scripts' } else { 'bin' })
        $exeName = if ($onWindows) { 'clang-format.exe' } else { 'clang-format' }
        $ClangFormat = Join-Path $binDir $exeName

        if ((Get-ClangFormatVersion $ClangFormat) -ne $version) {
            Write-Host "Installing clang-format $version into $venv"
            & $python -m venv $venv
            if ($LASTEXITCODE -ne 0) { throw "Could not create a virtual environment in $venv." }
            $venvPython = Join-Path $binDir $(if ($onWindows) { 'python.exe' } else { 'python' })
            & $venvPython -m pip install --disable-pip-version-check --quiet -r $requirements
            if ($LASTEXITCODE -ne 0) { throw "Could not install clang-format $version." }
            if ((Get-ClangFormatVersion $ClangFormat) -ne $version) {
                throw "clang-format $version was not installed correctly in $venv."
            }
        }
    }
}

Write-Host "clang-format: $ClangFormat ($version)"

$checkArgs = @('--clang-format', $ClangFormat)
if ($Base) { $checkArgs += @('--base', $Base) }
if ($All) { $checkArgs += '--all' }
if ($Fix) { $checkArgs += '--fix' }

& $python (Join-Path $root 'tools/check_format.py') @checkArgs
exit $LASTEXITCODE
