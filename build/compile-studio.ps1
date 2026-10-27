#requires -Version 5.1
<#
.SYNOPSIS
Find Visual Studio and build the SqlStudio application on Windows.
.DESCRIPTION
Uses vswhere to find the newest Visual Studio 2022 or newer installation (including
Build Tools) with MSBuild and C++ tools. A Developer Command Prompt is not required.
The repository's Windows minimum is Visual Studio 2022; older compilers are not supported.
.EXAMPLE
.\build\compile-studio.ps1
.EXAMPLE
.\build\compile-studio.ps1 -Configuration Release -BundleDir D:\dependencies\bundle
.EXAMPLE
.\build\compile-studio.ps1 -DryRun
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [ValidateRange(1, 256)]
    [int]$Jobs = [Math]::Max(1, [Environment]::ProcessorCount),
    [string]$VsInstallPath,
    [string]$BundleDir,
    [switch]$Rebuild,
    [switch]$DryRun
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$solution = Join-Path $repoRoot 'SqlStudio.sln'

if (!$VsInstallPath) {
    $vswhereCommand = Get-Command vswhere.exe -ErrorAction SilentlyContinue
    $vswhere = if ($vswhereCommand) { $vswhereCommand.Source } else {
        Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    }
    if (!(Test-Path -LiteralPath $vswhere -PathType Leaf)) {
        throw 'vswhere.exe was not found. Install Visual Studio 2022 or newer with Desktop development with C++, or pass -VsInstallPath.'
    }
    $installation = @(& $vswhere -latest -products '*' -version '[17.0,)' -requires `
        Microsoft.Component.MSBuild Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)
    if ($LASTEXITCODE -ne 0 -or $installation.Count -eq 0) {
        throw 'No Visual Studio 2022 or newer installation with MSBuild and C++ tools was found.'
    }
    $VsInstallPath = $installation[0].Trim()
}

$VsInstallPath = (Resolve-Path -LiteralPath $VsInstallPath).Path
$msbuild = Join-Path $VsInstallPath 'MSBuild\Current\Bin\MSBuild.exe'
if (!(Test-Path -LiteralPath $msbuild -PathType Leaf)) {
    throw "MSBuild.exe was not found in $VsInstallPath. Install the MSBuild component."
}

# Inspect installed toolsets rather than guessing from the installation directory name.
$toolsets = @(Get-ChildItem -Path (Join-Path $VsInstallPath 'MSBuild\Microsoft\VC\*\Platforms\x64\PlatformToolsets\v*') `
    -Directory -ErrorAction SilentlyContinue | Where-Object { $_.Name -match '^v\d+$' -and [int]$_.Name.Substring(1) -ge 143 } |
    Sort-Object { [int]$_.Name.Substring(1) } -Descending)
$compilers = @(Get-ChildItem -Path (Join-Path $VsInstallPath 'VC\Tools\MSVC\*\bin\Hostx64\x64\cl.exe') `
    -File -ErrorAction SilentlyContinue)
if ($toolsets.Count -eq 0 -or $compilers.Count -eq 0) {
    throw "No supported x64 C++ toolset was found in $VsInstallPath. Install the v143 or newer C++ tools and a Windows SDK."
}
$toolset = $toolsets[0].Name

if (!$BundleDir) {
    if ($env:MSS_3DPARTY_PATH) {
        $BundleDir = $env:MSS_3DPARTY_PATH
    } else {
        $parent = Split-Path -Parent $repoRoot
        foreach ($candidate in @((Join-Path $parent 'wb_build\bundle'), (Join-Path $parent 'bundle'), (Join-Path $repoRoot 'bundle'))) {
            if ((Test-Path -LiteralPath (Join-Path $candidate 'include') -PathType Container) -and
                (Test-Path -LiteralPath (Join-Path $candidate 'lib') -PathType Container)) {
                $BundleDir = $candidate
                break
            }
        }
    }
}
if (!$BundleDir -or !(Test-Path -LiteralPath $BundleDir -PathType Container)) {
    throw 'The third-party bundle was not found. Pass -BundleDir or set MSS_3DPARTY_PATH.'
}
$BundleDir = (Resolve-Path -LiteralPath $BundleDir).Path
if (!(Test-Path -LiteralPath (Join-Path $BundleDir 'include') -PathType Container) -or
    !(Test-Path -LiteralPath (Join-Path $BundleDir 'lib') -PathType Container)) {
    throw "The dependency bundle must contain include and lib directories: $BundleDir"
}

# Rebuild the application project and its project references, including Aga.Controls.
$buildTarget = if ($Rebuild) { "/t:SqlStudio:Rebuild" } else { "/t:SqlStudio" }
$buildArguments = @(
    $solution, $buildTarget, "/m:$Jobs", '/nologo', '/verbosity:minimal',
    "/p:Configuration=$Configuration", '/p:Platform=x64',
    "/p:PlatformToolset=$toolset", "/p:MSS_3DPARTY_PATH=$BundleDir"
)
Write-Host "Visual Studio: $VsInstallPath"
Write-Host "MSBuild:       $msbuild"
Write-Host "Toolset:       $toolset"
Write-Host "Bundle:        $BundleDir"
Write-Host "Target:        SqlStudio ($Configuration|x64)"
if ($DryRun) {
    Write-Host ('Arguments: ' + ($buildArguments -join ' '))
    return
}

Push-Location $repoRoot
try {
    & $msbuild @buildArguments
    $buildExitCode = $LASTEXITCODE
} finally {
    Pop-Location
}
if ($buildExitCode -ne 0) { exit $buildExitCode }
Write-Host "Build succeeded. Output: $(Join-Path $repoRoot "bin\x64\$Configuration")"
