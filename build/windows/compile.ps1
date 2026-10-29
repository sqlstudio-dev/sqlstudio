[CmdletBinding()]
param(
    [ValidateSet("build", "build-unittest", "run-unittest", "run-coverage")]
    [string]$Action = "build",

    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",

    [ValidateSet("x64")]
    [string]$Platform = "x64",

    [ValidateSet("auto", "v143", "v145")]
    [string]$PlatformToolset = "auto",

    [string]$VCToolsVersion,

    [ValidateSet("latest", "2026", "2022")]
    [string]$VisualStudio = "latest",

    [string]$VsInstallPath,

    [int]$Jobs = [Math]::Max(1, [Environment]::ProcessorCount),

    [string]$BundleDir,

    [string]$SolutionPath,

    [string]$TestProjectPath,

    [string]$CoverageOutput,

    [string[]]$TestArgs = @(),

    [switch]$Clean,

    [switch]$Rebuild,

    [switch]$SkipBuild,

    [switch]$Help
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$script:ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$script:ProjectRoot = [System.IO.Path]::GetFullPath((Join-Path $script:ScriptDir "..\.."))
$script:ProjectParent = Split-Path -Parent $script:ProjectRoot
$script:VsEnvLoaded = $false
$script:ResolvedPlatformToolset = $null
$script:VsPath = $null
$script:MSBuildPath = $null

if (-not $SolutionPath) {
    $SolutionPath = Join-Path $script:ProjectRoot "SqlStudio.sln"
}
if (-not $TestProjectPath) {
    $TestProjectPath = Join-Path $script:ProjectRoot "testing\test-suite\studio.testing.vcxproj"
}
if (-not $CoverageOutput) {
    $CoverageOutput = Join-Path $script:ProjectRoot "artifacts\coverage\sqlstudio-coverage.xml"
}

function Write-Info([string]$Message) {
    Write-Host "[INFO] $Message" -ForegroundColor Cyan
}

function Write-Ok([string]$Message) {
    Write-Host "[ OK ] $Message" -ForegroundColor Green
}

function Write-Warn([string]$Message) {
    Write-Host "[WARN] $Message" -ForegroundColor Yellow
}

function Write-Section([string]$Title) {
    Write-Host ""
    Write-Host ("-- {0} --" -f $Title) -ForegroundColor Magenta
}

function Show-Usage {
    @"
SqlStudio Windows build script

Usage:
  powershell -NoProfile -File .\build\windows\compile.ps1 [-Action <name>] [options]

Actions:
  build            Build the whole SqlStudio solution.
  build-unittest   Build the application and the native test-suite executable.
  run-unittest     Build if needed, prepare runtime files, and run test-suite.exe.
  run-coverage     Build if needed, prepare runtime files, and run test-suite.exe under OpenCppCoverage.

Options:
  -Configuration   Debug | Release                   Default: Debug
  -Platform        x64                               Default: x64
  -PlatformToolset auto | v143 | v145                Default: auto
  -VCToolsVersion  Exact MSVC tools version override Optional
  -VisualStudio    latest | 2026 | 2022              Default: latest
  -VsInstallPath   Explicit Visual Studio install path
  -Jobs            Parallel MSBuild workers          Default: CPU count
  -BundleDir       Path to MSS_3DPARTY_PATH bundle
  -SolutionPath    Path to SqlStudio.sln
  -TestProjectPath Path to testing\test-suite\test-suite.vcxproj
  -CoverageOutput  Coverage XML output path
  -TestArgs        Extra arguments forwarded to test-suite.exe
  -Clean           Clean before building
  -Rebuild         Use MSBuild Rebuild target instead of Build
  -SkipBuild       Skip build steps for run-unittest/run-coverage
  -Help            Show this usage text

Examples:
  .\build\windows\compile.ps1 -Action build -Configuration Debug
  .\build\windows\compile.ps1 -Action build-unittest -Configuration Release
  .\build\windows\compile.ps1 -Action build -VisualStudio 2026
  .\build\windows\compile.ps1 -Action build -PlatformToolset v143 -VisualStudio 2022
  .\build\windows\compile.ps1 -Action run-unittest -TestArgs --gtest_filter=*parser*
  .\build\windows\compile.ps1 -Action run-coverage -CoverageOutput .\artifacts\coverage\studio.xml
"@ | Write-Host
}

function Ensure-Directory([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
        New-Item -ItemType Directory -Path $Path -Force | Out-Null
    }
}

function Test-CommandAvailable([string]$Name) {
    return $null -ne (Get-Command $Name -ErrorAction SilentlyContinue)
}

function Find-VSWhere {
    $cmd = Get-Command "vswhere.exe" -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Source
    }

    $fallback = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $fallback) {
        return $fallback
    }

    throw "vswhere.exe was not found."
}

function Get-VSInstallPath {
    if ($VsInstallPath) {
        if (-not (Test-Path -LiteralPath $VsInstallPath -PathType Container)) {
            throw "Visual Studio install path was not found: $VsInstallPath"
        }
        return [System.IO.Path]::GetFullPath($VsInstallPath)
    }

    $vsWhere = Find-VSWhere
    $args = @("-latest")
    if ($VisualStudio -eq "2026") {
        $args += @("-version", "[18,19)")
    }
    elseif ($VisualStudio -eq "2022") {
        $args += @("-version", "[17,18)")
    }
    $args += @("-requires", "Microsoft.Component.MSBuild", "-property", "installationPath")

    $path = (& $vsWhere @args).Trim()
    if (-not $path) {
        throw "A compatible Visual Studio installation with MSBuild support was not found."
    }

    return $path
}

function Resolve-PlatformToolset {
    if ($PlatformToolset -ne "auto") {
        return $PlatformToolset
    }

    if ($script:VsPath -match "\\Microsoft Visual Studio\\18\\") {
        return "v145"
    }

    return "v143"
}

function Enter-VsBuildEnvironment {
    if ($script:VsEnvLoaded) {
        return
    }

    $script:VsPath = Get-VSInstallPath
    $script:ResolvedPlatformToolset = Resolve-PlatformToolset
    $vcVars = Join-Path $script:VsPath "VC\Auxiliary\Build\vcvarsall.bat"
    $msbuildCandidate = Join-Path $script:VsPath "MSBuild\Current\Bin\MSBuild.exe"
    if (-not (Test-Path -LiteralPath $vcVars -PathType Leaf)) {
        throw "vcvarsall.bat was not found under $($script:VsPath)"
    }
    if (-not (Test-Path -LiteralPath $msbuildCandidate -PathType Leaf)) {
        throw "MSBuild.exe was not found under $($script:VsPath)"
    }

    Write-Info "Loading Visual Studio build environment from $($script:VsPath)"
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

    $script:VsEnvLoaded = $true
    $script:MSBuildPath = $msbuildCandidate
    Write-Ok "Visual Studio environment ready."
}

function Get-DefaultBundleDir {
    $candidates = @()
    if ($env:MSS_3DPARTY_PATH) {
        $candidates += $env:MSS_3DPARTY_PATH
    }
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

function Initialize-BuildEnvironment {
    if (-not $BundleDir) {
        $BundleDir = Get-DefaultBundleDir
    }

    $script:BundleDir = [System.IO.Path]::GetFullPath($BundleDir)
    [Environment]::SetEnvironmentVariable("MSS_3DPARTY_PATH", $script:BundleDir, "Process")
    [Environment]::SetEnvironmentVariable("WB_3DPARTY_LIB", $script:BundleDir, "Process")

    if (-not (Test-Path -LiteralPath $SolutionPath -PathType Leaf)) {
        throw "Solution file was not found: $SolutionPath"
    }
    if (-not (Test-Path -LiteralPath $TestProjectPath -PathType Leaf)) {
        throw "Unit-test project file was not found: $TestProjectPath"
    }

    Enter-VsBuildEnvironment

    foreach ($tool in @("cmd.exe")) {
        if (-not (Test-CommandAvailable $tool)) {
            throw "Required build tool was not found after loading the VS environment: $tool"
        }
    }
}

function Invoke-LoggedCommand {
    param(
        [Parameter(Mandatory)] [string]$Label,
        [Parameter(Mandatory)] [string]$FilePath,
        [string[]]$Arguments = @(),
        [string]$WorkingDirectory = $PWD.Path
    )

    Write-Info ("{0}: {1} {2}" -f $Label, $FilePath, ($Arguments -join " "))
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    Push-Location $WorkingDirectory
    try {
        & $FilePath @Arguments
        $exitCode = $LASTEXITCODE
        if ($null -eq $exitCode) {
            $exitCode = 0
        }
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

function New-MSBuildPropertyArguments {
    $arguments = @(
        "/m:$Jobs",
        "/p:Configuration=$Configuration",
        "/p:Platform=$Platform",
        "/p:MSS_3DPARTY_PATH=$script:BundleDir",
        "/p:PlatformToolset=$script:ResolvedPlatformToolset"
    )

    if ($VCToolsVersion) {
        $arguments += "/p:VCToolsVersion=$VCToolsVersion"
    }

    return $arguments
}

function Invoke-MSBuildSolutionTarget {
    param([Parameter(Mandatory)] [string]$TargetName)

    $commonArgs = New-MSBuildPropertyArguments
    if ($Clean -or $Rebuild) {
        $solutionCleanArgs = @(
            $SolutionPath,
            "/t:Clean"
        ) + $commonArgs
        Invoke-LoggedCommand -Label "solution clean" -FilePath $script:MSBuildPath -Arguments $solutionCleanArgs -WorkingDirectory $script:ProjectRoot
    }

    $solutionBuildArgs = @(
        $SolutionPath,
        "/t:$TargetName"
    ) + $commonArgs
    Invoke-LoggedCommand -Label "solution target $TargetName" -FilePath $script:MSBuildPath -Arguments $solutionBuildArgs -WorkingDirectory $script:ProjectRoot
}

function Invoke-MSBuildProject {
    param([Parameter(Mandatory)] [string]$ProjectPath)

    $commonArgs = New-MSBuildPropertyArguments
    if ($Rebuild) {
        $target = "Rebuild"
    }
    else {
        $target = "Build"
    }

    if ($Clean -and -not $Rebuild) {
        $projectCleanArgs = @(
            $ProjectPath,
            "/t:Clean"
        ) + $commonArgs
        Invoke-LoggedCommand -Label ("clean {0}" -f ([System.IO.Path]::GetFileName($ProjectPath))) -FilePath $script:MSBuildPath -Arguments $projectCleanArgs -WorkingDirectory $script:ProjectRoot
    }

    $msbuildArgs = @(
        $ProjectPath,
        "/t:$target"
    ) + $commonArgs

    Invoke-LoggedCommand -Label ("project {0}" -f ([System.IO.Path]::GetFileName($ProjectPath))) -FilePath $script:MSBuildPath -Arguments $msbuildArgs -WorkingDirectory $script:ProjectRoot
}

function Get-OutputDirectory {
    return Join-Path $script:ProjectRoot ("bin\{0}\{1}" -f $Platform, $Configuration)
}

function Get-TestExecutablePath {
    return Join-Path (Get-OutputDirectory) "test-suite.exe"
}

function Prepare-RuntimeOutput {
    $prepareScript = Join-Path $script:ProjectRoot "PrepareOutputDir.cmd"
    if (-not (Test-Path -LiteralPath $prepareScript -PathType Leaf)) {
        throw "PrepareOutputDir.cmd was not found: $prepareScript"
    }

    Invoke-LoggedCommand -Label "prepare runtime output" -FilePath "cmd.exe" -Arguments @(
        "/c",
        "`"$prepareScript`"",
        "`"$($script:ProjectRoot)\`"",
        $Configuration,
        $Platform
    ) -WorkingDirectory $script:ProjectRoot
}

function Build-Studio {
    # The whole solution: the SqlStudio target alone leaves out the GRT
    # modules, plugins, Python bindings and helper tools it loads at run time.
    Invoke-MSBuildSolutionTarget -TargetName "Build"
}

function Build-UnitTests {
    Build-Studio
    Invoke-MSBuildProject -ProjectPath $TestProjectPath
}

function Invoke-TestExecutable {
    param([switch]$Coverage)

    $testExe = Get-TestExecutablePath
    if (-not (Test-Path -LiteralPath $testExe -PathType Leaf)) {
        throw "Unit-test executable was not found: $testExe"
    }

    $outputDir = Get-OutputDirectory
    $env:PATH = "$outputDir;$($script:BundleDir)\bin;$($script:BundleDir)\lib;$($script:BundleDir)\debug\lib;$env:PATH"

    if ($Coverage) {
        $coverageTool = Get-Command "OpenCppCoverage.exe" -ErrorAction SilentlyContinue
        if (-not $coverageTool) {
            throw "OpenCppCoverage.exe was not found in PATH. Install OpenCppCoverage or use -Action run-unittest."
        }

        Ensure-Directory (Split-Path -Parent $CoverageOutput)
        $coverageArgs = @(
            "--quiet",
            "--working_dir=$($script:ProjectRoot)",
            "--export_type=cobertura:$CoverageOutput",
            "--sources=$($script:ProjectRoot)",
            "--excluded_sources=$($script:BundleDir)",
            "--",
            $testExe
        ) + $TestArgs

        Invoke-LoggedCommand -Label "coverage run" -FilePath $coverageTool.Source -Arguments $coverageArgs -WorkingDirectory (Join-Path $script:ProjectRoot "testing\test-suite")
        Write-Ok "Coverage report written to $CoverageOutput"
        return
    }

    Invoke-LoggedCommand -Label "unit tests" -FilePath $testExe -Arguments $TestArgs -WorkingDirectory (Join-Path $script:ProjectRoot "testing\test-suite")
}

if ($Help) {
    Show-Usage
    exit 0
}

Initialize-BuildEnvironment

Write-Section "Configuration"
Write-Info "Action            : $Action"
Write-Info "Solution          : $SolutionPath"
Write-Info "Unit-test project : $TestProjectPath"
Write-Info "Configuration     : $Configuration"
Write-Info "Platform          : $Platform"
Write-Info "Visual Studio     : $script:VsPath"
Write-Info "MSBuild           : $script:MSBuildPath"
Write-Info "Platform toolset  : $script:ResolvedPlatformToolset"
if ($VCToolsVersion) {
    Write-Info "VC tools version  : $VCToolsVersion"
}
Write-Info "Jobs              : $Jobs"
Write-Info "Bundle            : $script:BundleDir"
if ($TestArgs.Count -gt 0) {
    Write-Info ("Test args         : {0}" -f ($TestArgs -join " "))
}

switch ($Action) {
    "build" {
        Write-Section "Build"
        Build-Studio
    }
    "build-unittest" {
        Write-Section "Build Unit Tests"
        Build-UnitTests
        Prepare-RuntimeOutput
    }
    "run-unittest" {
        Write-Section "Run Unit Tests"
        if (-not $SkipBuild) {
            Build-UnitTests
        }
        Prepare-RuntimeOutput
        Invoke-TestExecutable
    }
    "run-coverage" {
        Write-Section "Run Coverage"
        if (-not $SkipBuild) {
            Build-UnitTests
        }
        Prepare-RuntimeOutput
        Invoke-TestExecutable -Coverage
    }
}
