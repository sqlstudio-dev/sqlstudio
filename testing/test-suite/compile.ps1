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

.PARAMETER RerunFailed
Run only suites with failures in the previous XML report for this configuration.

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
    [switch]$RerunFailed,
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
    Write-Host "  -RerunFailed                      Run suites that failed in the previous run" -ForegroundColor Gray
    Write-Host "  -Help                             Show this text" -ForegroundColor Gray
    Write-Host "" 
    Write-Host "Notes:" -ForegroundColor Yellow
    Write-Host "  -Action test automatically skips build when studio.testing.exe is newer than studio.testing.vcxproj." -ForegroundColor Gray
    Write-Host ""
    Write-Host "Examples:" -ForegroundColor Yellow
    Write-Host "  .\testing\test-suite\compile.ps1 -Action build -Configuration Debug" -ForegroundColor Green
    Write-Host "  .\testing\test-suite\compile.ps1 -Action run -Configuration Debug -TestArgs --gtest_filter=*mysqlcanvas*" -ForegroundColor Green
    Write-Host "  .\testing\test-suite\compile.ps1 -Action test -Configuration Debug" -ForegroundColor Green
    Write-Host "  .\testing\test-suite\compile.ps1 -Action test -SkipBuild -RerunFailed" -ForegroundColor Green
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
        $environment = cmd /c "`"$vcVars`" x64 >nul && set"
        if ($LASTEXITCODE -ne 0) {
            throw "Visual Studio environment initialization failed (exit code $LASTEXITCODE)."
        }
        $environment | ForEach-Object {
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

function Invoke-LoggedCommand([string]$Label, [string]$FilePath, [string[]]$Arguments, [string]$WorkingDirectory,
                              [scriptblock]$OutputHandler) {
    Write-Info ("{0}: {1} {2}" -f $Label, $FilePath, ($Arguments -join " "))
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    Push-Location $WorkingDirectory
    try {
        if ($OutputHandler) {
            & $FilePath @Arguments | ForEach-Object {
                & $OutputHandler ([string]$_)
                $_
            }
        } else {
            & $FilePath @Arguments
        }
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

function Get-FailedSuiteFilter([string]$ReportPath) {
    if (-not (Test-Path -LiteralPath $ReportPath -PathType Leaf)) {
        throw "No previous test report was found: $ReportPath. Run tests once without -RerunFailed."
    }

    $report = New-Object System.Xml.XmlDocument
    $report.XmlResolver = $null
    $report.Load($ReportPath)
    if ($report.DocumentElement.LocalName -ne "testsuites") {
        throw "Invalid Google Test XML report: $ReportPath"
    }

    $failedSuites = @($report.SelectNodes("/testsuites/testsuite") | Where-Object {
        $_.SelectNodes("testcase/failure | testcase/error").Count -gt 0 -or
        [int]$_.GetAttribute("failures") -gt 0 -or [int]$_.GetAttribute("errors") -gt 0
    } | ForEach-Object {
        $name = $_.GetAttribute("name")
        if ($name -eq "NonTestSuiteFailure" -or $name -notmatch '^[A-Za-z0-9_/]+$') {
            throw "Failure '$name' cannot be selected as a test suite. Run tests without -RerunFailed."
        }
        "$name.*"
    } | Sort-Object -Unique)
    return $failedSuites -join ":"
}

function Save-FailedSuite([string]$Line, [string]$Path) {
    if ($Line -match '^\[\s*FAILED\s*\]\s+([A-Za-z0-9_/]+)(?:\.[^\s]+|:\s+SetUpTestSuite|:\s+TearDownTestSuite)') {
        $suite = $Matches[1]
        if ($suite -eq "NonTestSuiteFailure") {
            throw "A global test-environment failure cannot be rerun as an individual suite."
        }
        if (@(Get-Content -LiteralPath $Path) -notcontains $suite) {
            Add-Content -LiteralPath $Path -Value $suite
        }
    }
}

function Run-TestSuite {
    $testExe = Get-TestExecutablePath
    if (-not (Test-Path -LiteralPath $testExe -PathType Leaf)) {
        throw "Unit test executable was not found: $testExe"
    }

    $studioOutputDir = Join-Path $script:ProjectRoot ("bin\{0}\{1}" -f $Platform, $Configuration)
    $testOutputDir = Join-Path $script:ScriptDir ("bin\{0}\{1}" -f $Platform, $Configuration)
    $historyDir = Join-Path $script:ScriptDir ("TestResults\{0}\{1}" -f $Platform, $Configuration)
    $historyPath = Join-Path $historyDir "last-test-results.xml"
    $failurePath = Join-Path $historyDir "failed-suites.txt"
    $reportPath = Join-Path $testOutputDir "current-test-results.xml"
    $arguments = @($TestArgs)
    if ($RerunFailed) {
        if (@($arguments | Where-Object { $_ -match '^--gtest_(filter|list_tests)(=|$)' }).Count -gt 0) {
            throw "-RerunFailed cannot be combined with --gtest_filter or --gtest_list_tests."
        }
        if (Test-Path -LiteralPath $historyPath -PathType Leaf) {
            $filter = Get-FailedSuiteFilter -ReportPath $historyPath
        } elseif (Test-Path -LiteralPath $failurePath -PathType Leaf) {
            $suites = @(Get-Content -LiteralPath $failurePath | Where-Object { $_ })
            if ($suites.Count -eq 0) {
                throw "The previous run produced no XML and recorded no completed failures. Run tests without -RerunFailed."
            }
            foreach ($suite in $suites) {
                if ($suite -notmatch '^[A-Za-z0-9_/]+$') {
                    throw "Invalid suite name in failure history: $failurePath"
                }
            }
            $filter = ($suites | Sort-Object -Unique | ForEach-Object { "$_.*" }) -join ":"
            Write-Warn "The previous run did not finish its XML report; rerunning failures recorded before it stopped."
        } else {
            throw "No previous test report was found: $historyPath. Run tests once without -RerunFailed."
        }
        if (-not $filter) {
            Write-Ok "The previous run had no failed test suites. Nothing to rerun."
            return
        }
        $arguments += "--gtest_filter=$filter"
    }

    $listing = @($arguments | Where-Object { $_ -match '^--gtest_list_tests(=|$)' }).Count -gt 0
    $trackResults = -not $listing
    $outputArgs = @($arguments | Where-Object { $_ -match '^--gtest_output=' })
    if ($outputArgs.Count -gt 1) {
        throw "Pass --gtest_output at most once."
    }
    if ($outputArgs.Count -eq 1) {
        if ($outputArgs[0] -match '^--gtest_output=xml:(.+\.xml)$') {
            $reportPath = $Matches[1]
            if (-not [System.IO.Path]::IsPathRooted($reportPath)) {
                $reportPath = Join-Path $script:ScriptDir $reportPath
            }
            $reportPath = [System.IO.Path]::GetFullPath($reportPath)
        } else {
            $trackResults = $false
            if (-not $listing) {
                Write-Warn "Full results require --gtest_output=xml:<file.xml>; this run will record console failures only."
            }
        }
    } elseif (-not $listing) {
        $arguments += "--gtest_output=xml:$reportPath"
    }
    if ($trackResults) {
        New-Item -ItemType Directory -Path (Split-Path -Parent $reportPath) -Force | Out-Null
        if (Test-Path -LiteralPath $reportPath) {
            Remove-Item -LiteralPath $reportPath
        }
        if (-not $listing) {
            New-Item -ItemType Directory -Path $historyDir -Force | Out-Null
            if (Test-Path -LiteralPath $historyPath) {
                Remove-Item -LiteralPath $historyPath
            }
            Set-Content -LiteralPath $failurePath -Value "" -Encoding UTF8
            Write-Info "Recording failed suites: $failurePath"
        }
    }
    $env:PATH = "$testOutputDir;$studioOutputDir;$($script:BundleDir)\bin;$($script:BundleDir)\lib;$($script:BundleDir)\debug\lib;$env:PATH"

    try {
        $outputHandler = $null
        if (-not $listing) {
            $outputHandler = { param($line) Save-FailedSuite -Line $line -Path $failurePath }
        }
        Invoke-LoggedCommand -Label "run studio.testing" -FilePath $testExe -Arguments $arguments -WorkingDirectory $script:ScriptDir -OutputHandler $outputHandler
    }
    finally {
        if (-not $listing) {
            if ($trackResults -and (Test-Path -LiteralPath $reportPath -PathType Leaf)) {
                if ($reportPath -ne $historyPath) {
                    Copy-Item -LiteralPath $reportPath -Destination $historyPath -Force
                }
                Write-Info "Test results saved: $historyPath"
            } else {
                if (Test-Path -LiteralPath $historyPath) {
                    Remove-Item -LiteralPath $historyPath
                }
                if ($trackResults) {
                    Write-Warn "No XML report was produced. Completed failures remain recorded in $failurePath; unfinished tests cannot be inferred."
                }
            }
        }
    }
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

if ($RerunFailed -and $Action -eq "build") {
    throw "-RerunFailed requires -Action run or -Action test."
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

if ($SkipBuild -and $Action -in @("run", "test")) {
    $script:ResolvedPlatformToolset = Resolve-PlatformToolset -VsPath (Get-VSInstallPath)
}
else {
    Enter-VsEnvironment
}

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
