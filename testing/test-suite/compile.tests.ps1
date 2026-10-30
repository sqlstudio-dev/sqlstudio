$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

# Load runner functions without executing its build/run entry point.
$tokens = $null
$parseErrors = $null
$runner = Join-Path $PSScriptRoot "compile.ps1"
$ast = [System.Management.Automation.Language.Parser]::ParseFile($runner, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count -gt 0) {
    throw ($parseErrors -join [Environment]::NewLine)
}
foreach ($function in $ast.FindAll({
    param($node)
    $node -is [System.Management.Automation.Language.FunctionDefinitionAst]
}, $false)) {
    . ([scriptblock]::Create($function.Extent.Text))
}

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Assert-Throws([scriptblock]$Action, [string]$Expected) {
    $message = ""
    try { & $Action } catch { $message = $_.Exception.Message }
    Assert-True ($message -like "*$Expected*") "Expected '$Expected', got '$message'"
}

$script:HostWrites = @()
function Write-Host {
    param($Object, $ForegroundColor, [switch]$NoNewline)
    $script:HostWrites += [pscustomobject]@{
        Text = [string]$Object
        Color = [string]$ForegroundColor
        NoNewline = [bool]$NoNewline
    }
}
try {
    foreach ($case in @(
        @("[       OK ] Suite.Test (1 ms)", "Green"),
        @("[  PASSED  ] 2 tests.", "Green"),
        @("[ RUN      ] Suite.Test", "Green"),
        @("[  FAILED  ] Suite.Test", "Red"),
        @("[  SKIPPED ] Suite.Test", "Yellow"),
        @("[----------] 2 tests from Suite", "Green"),
        @("[==========] Running 2 tests.", "Green")
    )) {
        $script:HostWrites = @()
        Write-TestOutput -Line $case[0]
        Assert-True ($script:HostWrites[0].Color -eq $case[1]) "Incorrect test status color"
        Assert-True ($script:HostWrites[0].NoNewline) "Status prefix must share its output line"
        Assert-True (($script:HostWrites.Text -join "") -eq $case[0]) "Colored output changed test text"
    }
    $script:HostWrites = @()
    Write-TestOutput -Line "[       OK ] Suite.Test" -UseColor $false
    Assert-True ($script:HostWrites.Count -eq 1 -and $script:HostWrites[0].Color -eq "") "Color opt-out was ignored"
    $script:HostWrites = @()
    Write-TestOutput -Line "Diagnostic text"
    Assert-True ($script:HostWrites[0].Text -eq "Diagnostic text" -and $script:HostWrites[0].Color -eq "") "Diagnostics changed"
}
finally {
    Remove-Item Function:\Write-Host
}

function Invoke-LoggedCommand {
    param($Label, $FilePath, [string[]]$Arguments, $WorkingDirectory, [scriptblock]$OutputHandler)
    $script:Invocations++
    $script:CapturedArguments = $Arguments
    if ($OutputHandler) {
        foreach ($line in $script:ConsoleLines) { & $OutputHandler $line }
    }
    $output = @($Arguments | Where-Object { $_ -like "--gtest_output=xml:*" })
    if ($script:ProduceReport -and $output.Count -eq 1) {
        $path = $output[0].Substring("--gtest_output=xml:".Length)
        if (-not [System.IO.Path]::IsPathRooted($path)) {
            $path = Join-Path $WorkingDirectory $path
        }
        [System.IO.File]::WriteAllText($path, $script:NextReport)
    }
    if ($script:CommandFails) { throw "Simulated test failure" }
}

$temporaryRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("sqlstudio-runner-" + [guid]::NewGuid())
$oldPath = $env:PATH
try {
    $script:ScriptDir = $temporaryRoot
    $script:ProjectRoot = $temporaryRoot
    $script:BundleDir = $temporaryRoot
    $Platform = "x64"
    $Configuration = "Debug"
    $TestArgs = @()
    $RerunFailed = $false
    $outputDir = Join-Path $temporaryRoot "bin\x64\Debug"
    New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
    New-Item -ItemType File -Path (Get-TestExecutablePath) | Out-Null
    $historyDir = Join-Path $temporaryRoot "TestResults\x64\Debug"
    New-Item -ItemType Directory -Path $historyDir -Force | Out-Null
    $history = Join-Path $historyDir "last-test-results.xml"
    $passing = '<testsuites name="AllTests" failures="0"><testsuite name="Passing" failures="0" errors="0"><testcase name="Ok"/></testsuite></testsuites>'
    $failing = '<testsuites name="AllTests" failures="3"><testsuite name="Passing" failures="0"/><testsuite name="Broken" failures="1"><testcase name="Bad"><failure message="failed"/></testcase></testsuite><testsuite name="Setup" failures="1"><testcase name="NonTestSuiteFailure"><failure/></testcase></testsuite><testsuite name="Typed/0" failures="1"><testcase name="Bad"><failure/></testcase></testsuite></testsuites>'
    $script:Invocations = 0
    $script:ProduceReport = $true
    $script:CommandFails = $false
    $script:NextReport = $passing
    $script:CapturedArguments = @()
    $script:ConsoleLines = @()

    Assert-Throws { Get-FailedSuiteFilter $history } "No previous test report"
    [System.IO.File]::WriteAllText($history, "<not-results/>")
    Assert-Throws { Get-FailedSuiteFilter $history } "Invalid Google Test"
    [System.IO.File]::WriteAllText($history, "<testsuites>")
    Assert-Throws { Get-FailedSuiteFilter $history } "Load"

    [System.IO.File]::WriteAllText($history, $failing)
    Assert-True ((Get-FailedSuiteFilter $history) -eq "Broken.*:Setup.*:Typed/0.*") "Incorrect failed-suite selection"
    [System.IO.File]::WriteAllText($history, '<testsuites><testsuite name="NonTestSuiteFailure" failures="1"/></testsuites>')
    Assert-Throws { Get-FailedSuiteFilter $history } "cannot be selected"
    [System.IO.File]::WriteAllText($history, '<testsuites><testsuite name="Bad*" failures="1"/></testsuites>')
    Assert-Throws { Get-FailedSuiteFilter $history } "cannot be selected"
    [System.IO.File]::WriteAllText($history, '<testsuites><testsuite name="Skipped" failures="0"><testcase name="Skip"><skipped/></testcase></testsuite></testsuites>')
    Assert-True ((Get-FailedSuiteFilter $history) -eq "") "Skipped tests must not be rerun"

    $script:NextReport = $failing
    $script:CommandFails = $true
    Assert-Throws { Run-TestSuite } "Simulated test failure"
    Assert-True ((Get-Content -Raw $history) -eq $failing) "Failed run did not save its XML"
    $script:CommandFails = $false
    $script:NextReport = $passing
    $RerunFailed = $true
    Run-TestSuite
    Assert-True ($script:CapturedArguments -contains "--gtest_filter=Broken.*:Setup.*:Typed/0.*") "Rerun did not pass the suite filter"
    $count = $script:Invocations
    Run-TestSuite
    Assert-True ($script:Invocations -eq $count) "Passing history should not launch tests"

    $TestArgs = @("--gtest_filter=Other.*")
    Assert-Throws { Run-TestSuite } "cannot be combined"
    $TestArgs = @("--gtest_list_tests")
    Assert-Throws { Run-TestSuite } "cannot be combined"
    $RerunFailed = $false
    $script:ProduceReport = $false
    Run-TestSuite
    Assert-True ((Get-Content -Raw $history) -eq $passing) "Listing tests changed history"

    $TestArgs = @()
    Assert-True (Test-Path -LiteralPath (Join-Path $outputDir "current-test-results.xml")) "Expected previous report"
    Run-TestSuite
    Assert-True (-not (Test-Path -LiteralPath $history)) "Missing fresh XML must invalidate history"
    $RerunFailed = $true
    Assert-Throws { Run-TestSuite } "recorded no completed failures"
    $RerunFailed = $false

    $script:ConsoleLines = @(
        "[  FAILED  ] Broken.Test (2 ms)",
        "[  FAILED  ] Setup: SetUpTestSuite or TearDownTestSuite",
        "[  FAILED  ] Broken.Test",
        "[  FAILED  ] 2 tests, listed below:"
    )
    $script:CommandFails = $true
    Assert-Throws { Run-TestSuite } "Simulated test failure"
    $recorded = @(Get-Content -LiteralPath (Join-Path $historyDir "failed-suites.txt") | Where-Object { $_ })
    Assert-True (($recorded -join ":") -eq "Broken:Setup") "Incremental failures were not recorded correctly"
    $RerunFailed = $true
    $script:CommandFails = $false
    $script:ProduceReport = $true
    $script:ConsoleLines = @()
    Run-TestSuite
    Assert-True ($script:CapturedArguments -contains "--gtest_filter=Broken.*:Setup.*") "Crash fallback did not select recorded failures"
    $RerunFailed = $false

    $script:ProduceReport = $true
    $TestArgs = @("--gtest_output=xml:custom.xml")
    Run-TestSuite
    Assert-True (Test-Path -LiteralPath (Join-Path $temporaryRoot "custom.xml")) "Custom report was not written"
    Assert-True ((Get-Content -Raw $history) -eq $passing) "Custom XML was not saved to history"
    $TestArgs = @("--gtest_output=xml:first.xml", "--gtest_output=xml:second.xml")
    Assert-Throws { Run-TestSuite } "at most once"
    $TestArgs = @("--gtest_output=json:custom.json")
    Run-TestSuite
    Assert-True (-not (Test-Path -LiteralPath $history)) "Non-XML output must invalidate history"
    Write-Host "All compile.ps1 runner checks passed."
}
finally {
    $env:PATH = $oldPath
    if (Test-Path -LiteralPath $temporaryRoot) {
        Remove-Item -LiteralPath $temporaryRoot -Recurse -Force
    }
}
