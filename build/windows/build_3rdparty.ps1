<#
.SYNOPSIS
  Download, build, and stage the Windows 3rd-party bundle for MySQL Workbench / SqlStudio.

.DESCRIPTION
  This script is the Windows-side equivalent of build/linux/build_3rdparty.sh.
  It creates a bundle layout compatible with the existing Visual Studio project files:

    <bundle>\
      bin\
      include\
      lib\
      debug\lib\
      python\

  The default output is a sibling build folder outside the source tree:

    ..\bundle

  The script mixes:
  - direct source builds for MySQL, Connector/C++, SQLite, Python, and header-only deps
  - explicit source builds for the heavier native libraries used by the Windows bundle

.EXAMPLE
  pwsh ./build/windows/build_3rdparty.ps1

.EXAMPLE
  pwsh ./build/windows/build_3rdparty.ps1 -Only openssl,zlib,libxml2,cairo

.EXAMPLE
  pwsh ./build/windows/build_3rdparty.ps1 -BundleDir D:\develop\MySQL\wb_build\bundle -Clean -VerboseOutput
#>

[CmdletBinding()]
param(
    [string]$BundleDir,
    [switch]$Clean,
    [switch]$DownloadOnly,
    [switch]$BuildOnly,
    [switch]$VerboseOutput,
    [int]$Jobs = [Math]::Max([Environment]::ProcessorCount, 1),
    [string[]]$Only = @(),
    [ValidateSet("auto", "Visual Studio 18 2026", "Visual Studio 17 2022")]
    [string]$Generator = "auto",
    [ValidateSet("latest", "2026", "2022")]
    [string]$VisualStudio = "latest",
    [string]$VsInstallPath,
    # MSVC toolset for every library; "auto" takes the newest one installed.
    # The application has to be built with the same or a newer toolset.
    [ValidateSet("auto", "v143", "v145")]
    [string]$PlatformToolset = "auto"
)

Set-StrictMode -Version 3.0
$ErrorActionPreference = "Stop"

$script:ScriptDir = Split-Path -Parent $PSCommandPath
$script:ProjectRoot = [System.IO.Path]::GetFullPath((Join-Path $script:ScriptDir "..\.."))
$script:ProjectParent = Split-Path -Parent $script:ProjectRoot
if (-not $BundleDir) {
    $BundleDir = Join-Path $script:ProjectParent "bundle"
}
$script:BundleDir = [System.IO.Path]::GetFullPath($BundleDir)
$script:WorkRoot = Join-Path $script:BundleDir "_work"
$script:DownloadRoot = Join-Path $script:WorkRoot "downloads"
$script:SourceRoot = Join-Path $script:WorkRoot "source"
$script:BuildRoot = Join-Path $script:WorkRoot "build"
$script:StampRoot = Join-Path $script:WorkRoot "stamps"
$script:ToolsRoot = Join-Path $script:BundleDir "_tools"
$script:DepManifestPath = Join-Path $script:ScriptDir "libs.txt"
$script:VsEnvLoaded = $false
$script:StatusLineVisible = $false
$script:StatusPanelMaxLines = 14
$script:StatusPanelTitle = ""
$script:StatusPanelLines = @()
$script:StatusPanelPhysicalRows = 0
$script:DepManifest = @()
$script:SelectedOnly = @($Only | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
$script:ActiveProcesses = [System.Collections.Generic.List[System.Diagnostics.Process]]::new()
$script:VsPath = $null
$script:ResolvedGenerator = $Generator
$script:ResolvedToolset = $null

function Stop-AllActiveProcesses {
    $procs = @($script:ActiveProcesses)
    $script:ActiveProcesses.Clear()
    foreach ($proc in $procs) {
        try {
            if ($proc -and -not $proc.HasExited) {
                & taskkill /T /F /PID $proc.Id 2>$null | Out-Null
            }
        }
        catch {}
        finally {
            try { $proc.Dispose() } catch {}
        }
    }
}

function Write-Banner {
    Clear-StatusLine
    Write-Host ""
    Write-Host "+------------------------------------------------------------------+" -ForegroundColor DarkYellow
    Write-Host "|   MySQL Workbench / Studio - Windows 3rd-Party Bundle Builder    |" -ForegroundColor Gray
    Write-Host "+------------------------------------------------------------------+" -ForegroundColor DarkYellow
    Write-Host ""
}

function Get-ConsoleWidth {
    try {
        return [Math]::Max($Host.UI.RawUI.BufferSize.Width, 80)
    }
    catch {
        return 120
    }
}

function Get-PhysicalRowCount([string]$Text, [int]$Width) {
    if ([string]::IsNullOrEmpty($Text) -or $Width -le 0) {
        return 1
    }
    return [Math]::Max(1, [Math]::Ceiling($Text.Length / $Width))
}

function Render-StatusPanel {
    if (-not $script:StatusLineVisible) {
        return
    }

    $width = Get-ConsoleWidth
    $esc = [char]0x1b
    $clearLine = "${esc}[2K"
    $colorGray = "${esc}[90m"
    $colorYellow = "${esc}[33m"
    $colorReset = "${esc}[0m"

    # Erase previous render by moving up the exact physical rows we wrote last time
    $prevRows = $script:StatusPanelPhysicalRows
    $buf = [System.Text.StringBuilder]::new(4096)
    if ($prevRows -gt 0) {
        [void]$buf.Append("${esc}[${prevRows}A")
        for ($i = 0; $i -lt $prevRows; $i++) {
            [void]$buf.Append($clearLine)
            [void]$buf.AppendLine()
        }
        [void]$buf.Append("${esc}[${prevRows}A")
    }

    # Build new content — lines are truncated so 1 line = 1 physical row
    $titleText = ([string]$script:StatusPanelTitle -replace "\s+", " ").Trim()
    $visibleLines = @($script:StatusPanelLines)
    $newPhysicalRows = 1 + $visibleLines.Count

    # Write title (truncate with ... if needed)
    $maxLen = $width - 2
    if ($titleText.Length -gt $maxLen) {
        $titleText = $titleText.Substring(0, $maxLen - 3) + "..."
    }
    [void]$buf.Append($clearLine)
    [void]$buf.Append($colorYellow)
    [void]$buf.Append($titleText)
    [void]$buf.AppendLine($colorReset)

    # Write output lines (truncate with ... if needed)
    foreach ($vl in $visibleLines) {
        $lineText = "  " + $vl
        if ($lineText.Length -gt $maxLen) {
            $lineText = $lineText.Substring(0, $maxLen - 3) + "..."
        }
        [void]$buf.Append($clearLine)
        [void]$buf.Append($colorGray)
        [void]$buf.Append($lineText)
        [void]$buf.AppendLine($colorReset)
    }

    [Console]::Write($buf.ToString())
    $script:StatusPanelPhysicalRows = $newPhysicalRows
}

function Start-StatusPanel([string]$Title) {
    if ($script:StatusLineVisible) {
        $script:StatusPanelTitle = $Title
        Render-StatusPanel
        return
    }

    $script:StatusPanelTitle = $Title
    $script:StatusPanelLines = @()
    $script:StatusLineVisible = $true
    $script:StatusPanelPhysicalRows = 0
    Render-StatusPanel
}

function Update-StatusPanelTitle([string]$Title) {
    if (-not $script:StatusLineVisible) {
        return
    }

    $script:StatusPanelTitle = $Title
    Render-StatusPanel
}

function Push-StatusPanelLine([string]$Message) {
    if (-not $script:StatusLineVisible -or [string]::IsNullOrWhiteSpace($Message)) {
        return
    }

    $normalized = ([string]$Message -replace "\s+", " ").Trim()
    if ($script:StatusPanelLines -is [System.Collections.ArrayList]) {
        [void]$script:StatusPanelLines.Add($normalized)
    } else {
        $script:StatusPanelLines = [System.Collections.ArrayList]@($script:StatusPanelLines)
        [void]$script:StatusPanelLines.Add($normalized)
    }
    while ($script:StatusPanelLines.Count -gt $script:StatusPanelMaxLines) {
        $script:StatusPanelLines.RemoveAt(0)
    }

    Render-StatusPanel
}

function Clear-StatusLine {
    if (-not $script:StatusLineVisible) {
        return
    }

    $prevRows = $script:StatusPanelPhysicalRows
    if ($prevRows -gt 0) {
        $esc = [char]0x1b
        $clearLine = "${esc}[2K"
        $buf = [System.Text.StringBuilder]::new(256)
        [void]$buf.Append("${esc}[${prevRows}A")
        for ($i = 0; $i -lt $prevRows; $i++) {
            [void]$buf.Append($clearLine)
            [void]$buf.AppendLine()
        }
        [void]$buf.Append("${esc}[${prevRows}A")
        [Console]::Write($buf.ToString())
    }

    $script:StatusLineVisible = $false
    $script:StatusPanelTitle = ""
    $script:StatusPanelLines = @()
    $script:StatusPanelPhysicalRows = 0
}

function Write-Section([string]$Message) {
    Clear-StatusLine
    Write-Host ""
    Write-Host ("-- {0} --" -f $Message) -ForegroundColor Magenta
}

function Write-Info([string]$Message) {
    Clear-StatusLine
    Write-Host ("  [INFO] {0}" -f $Message) -ForegroundColor Cyan
}

function Write-Ok([string]$Message) {
    Clear-StatusLine
    Write-Host ("  [ OK ] {0}" -f $Message) -ForegroundColor Green
}

function Write-Warn([string]$Message) {
    Clear-StatusLine
    Write-Host ("  [WARN] {0}" -f $Message) -ForegroundColor Yellow
}

function Write-Fail([string]$Message) {
    Clear-StatusLine
    Write-Host ("  [FAIL] {0}" -f $Message) -ForegroundColor Red
}

function Write-Detail([string]$Message) {
    Clear-StatusLine
    Write-Host ("    {0}" -f $Message) -ForegroundColor DarkGray
}

function Write-StepHeader([int]$Index, [int]$Total, [string]$Label) {
    $barLength = 30
    $filled = if ($Total -gt 0) { [Math]::Floor(($Index / $Total) * $barLength) } else { 0 }
    $filled = [Math]::Min($filled, $barLength)
    $bar = ("#" * $filled).PadRight($barLength, ".")
    Clear-StatusLine
    Write-Host ""
    Write-Host ("[{0}/{1}] {2}  {3}" -f $Index, $Total, $bar, $Label) -ForegroundColor White
}

function Get-RecentProcessLines([string[]]$Paths, [int]$TailCount = 50) {
    $noisePatterns = @(
        '^\s*$',
        '^Copyright \(C\) Microsoft Corporation\.',
        '^Microsoft \(R\).+',
        '^The input line is too long\.$',
        '^The syntax of the command is incorrect\.$',
        '^All rights reserved\.$'
    )

    $result = [System.Collections.Generic.List[string]]::new()
    foreach ($path in $Paths) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            continue
        }

        $tail = @(Get-Content -LiteralPath $path -Tail $TailCount -ErrorAction SilentlyContinue)
        foreach ($line in $tail) {
            $text = ([string]$line).Trim()
            if ([string]::IsNullOrWhiteSpace($text)) { continue }
            $isNoise = $false
            foreach ($pattern in $noisePatterns) {
                if ($text -match $pattern) {
                    $isNoise = $true
                    break
                }
            }
            if (-not $isNoise) {
                $result.Add($text)
            }
        }
    }

    return $result
}

function Remove-FileWithRetry {
    param(
        [Parameter(Mandatory)] [string]$Path,
        [int]$Attempts = 20,
        [int]$DelayMilliseconds = 250
    )

    for ($attempt = 1; $attempt -le $Attempts; $attempt++) {
        if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
            return
        }

        try {
            Remove-Item -LiteralPath $Path -Force
            return
        }
        catch {
            if ($attempt -eq $Attempts) {
                Write-Warn ("Leaving temporary log file behind because it is still locked: {0}" -f $Path)
                return
            }
            Start-Sleep -Milliseconds $DelayMilliseconds
        }
    }
}

function ConvertTo-WindowsCommandLineArgument {
    param([AllowEmptyString()] [string]$Value)

    if ($null -eq $Value) {
        return '""'
    }

    if ($Value.Length -eq 0) {
        return '""'
    }

    if ($Value -notmatch '[\s"]') {
        return $Value
    }

    $escaped = $Value -replace '(\\*)"', '$1$1\"'
    $escaped = $escaped -replace '(\\+)$', '$1$1'
    return '"' + $escaped + '"'
}

function Ensure-Directory([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
        [void](New-Item -ItemType Directory -Path $Path -Force)
    }
}

function Reset-Directory([string]$Path) {
    if (Test-Path -LiteralPath $Path -PathType Container) {
        Remove-Item -LiteralPath $Path -Recurse -Force
    }
    [void](New-Item -ItemType Directory -Path $Path -Force)
}

function Initialize-BundleLayout {
    $dirs = @(
        $script:BundleDir,
        (Join-Path $script:BundleDir "bin"),
        (Join-Path $script:BundleDir "include"),
        (Join-Path $script:BundleDir "lib"),
        (Join-Path $script:BundleDir "debug"),
        (Join-Path $script:BundleDir "debug\lib"),
        (Join-Path $script:BundleDir "python"),
        (Join-Path $script:BundleDir "share"),
        $script:WorkRoot,
        $script:DownloadRoot,
        $script:SourceRoot,
        $script:BuildRoot,
        $script:StampRoot,
        $script:ToolsRoot
    )

    foreach ($dir in $dirs) {
        Ensure-Directory $dir
    }
}

function Get-DependencySlug([pscustomobject]$Dependency) {
    return "{0}-{1}" -f $Dependency.Name, $Dependency.Version
}

function Get-ArchivePath([pscustomobject]$Dependency) {
    if (-not $Dependency.PSObject.Properties["Urls"] -and -not $Dependency.PSObject.Properties["Url"]) {
        return $null
    }

    $primarySource = if ($Dependency.PSObject.Properties["Urls"] -and $Dependency.Urls.Count -gt 0) {
        $Dependency.Urls[0]
    }
    else {
        $Dependency.Url
    }

    $fileName = if ($primarySource -match '^[a-zA-Z][a-zA-Z0-9+.-]*://') {
        [System.IO.Path]::GetFileName(([Uri]$primarySource).AbsolutePath)
    }
    else {
        [System.IO.Path]::GetFileName($primarySource)
    }
    return Join-Path $script:DownloadRoot $fileName
}

function Get-ExtractPath([pscustomobject]$Dependency) {
    return Join-Path $script:SourceRoot (Get-DependencySlug $Dependency)
}

function Get-StampPath([pscustomobject]$Dependency) {
    return Join-Path $script:StampRoot ((Get-DependencySlug $Dependency) + ".done")
}

function Get-AntlrToolJarFileName([pscustomobject]$Dependency) {
    return "antlr-{0}-complete.jar" -f $Dependency.Version
}

function Get-AntlrToolJarSources([pscustomobject]$Dependency) {
    $jarName = Get-AntlrToolJarFileName $Dependency
    return @("https://www.antlr.org/download/$jarName")
}

function Get-AntlrToolJarArchivePath([pscustomobject]$Dependency) {
    return Join-Path $script:DownloadRoot (Get-AntlrToolJarFileName $Dependency)
}

function Ensure-AntlrToolJarDownloaded {
    param([pscustomobject]$Dependency)

    $jarArchivePath = Get-AntlrToolJarArchivePath $Dependency
    if (-not $BuildOnly) {
        Download-File -Sources (Get-AntlrToolJarSources $Dependency) -Destination $jarArchivePath
    }
    elseif (-not (Test-Path -LiteralPath $jarArchivePath -PathType Leaf)) {
        throw "--BuildOnly was requested but ANTLR tool jar is missing: $jarArchivePath"
    }

    return $jarArchivePath
}

function Test-DependencyBuilt([pscustomobject]$Dependency) {
    return Test-Path -LiteralPath (Get-StampPath $Dependency)
}

function Mark-DependencyBuilt([pscustomobject]$Dependency) {
    Set-Content -LiteralPath (Get-StampPath $Dependency) -Value ("built {0:yyyy-MM-dd HH:mm:ss}" -f (Get-Date))
}

function Test-AnyPathExists {
    param([string[]]$RelativePaths)

    foreach ($relativePath in $RelativePaths) {
        $fullPath = Join-Path $script:BundleDir $relativePath
        if (Test-Path -LiteralPath $fullPath) {
            return $true
        }
    }

    return $false
}

function Test-DependencyOutputsPresent {
    param([pscustomobject]$Dependency)

    $requiredGroups = @(switch ($Dependency.Type) {
        "openssl" {
            @(
                @("include\openssl\ssl.h"),
                @("lib\libssl.lib", "lib\libssl-3-x64.dll")
            )
        }
        "zlib" {
            @(
                @("include\zlib\zlib.h"),
                @("include\zlib\zconf.h"),
                @("lib\zlib.lib", "lib\zlib1.lib", "lib\zlib.dll", "lib\zlib1.dll"),
                @("debug\lib\zlibd.lib", "debug\lib\zlibd1.lib", "debug\lib\zlibd.dll", "debug\lib\zlibd1.dll")
            )
        }
        "libxml2" {
            @(
                @("include\libxml2\libxml\parser.h"),
                @("lib\libxml2.lib", "lib\libxml2.dll")
            )
        }
        "cairo" {
            @(
                @("include\cairo.h", "include\cairo\cairo.h"),
                @("lib\cairo.lib", "lib\cairo-2.dll")
            )
        }
        "libzip" {
            @(
                @("include\zip.h", "include\zipconf.h"),
                @("lib\zip.lib", "lib\zip.dll")
            )
        }
        "antlr4" {
            @(
                @("include\antlr4-runtime\antlr4-runtime.h"),
                @("lib\antlr4-runtime.lib", "lib\antlr4-runtime.dll"),
                @("bin\antlr-4.13.2-complete.jar")
            )
        }
        "libssh" {
            @(
                @("include\libssh\libssh.h", "include\libssh.h"),
                @("include\libssh\server.h"),
                @("include\libssh\libsshpp.hpp"),
                @("lib\ssh.lib", "lib\libssh.lib", "lib\ssh.dll", "lib\libssh.dll")
            )
        }
        "proj" {
            @(
                @("include\proj.h"),
                @("lib\proj.lib", "lib\proj.dll")
            )
        }
        "gdal" {
            @(
                @("include\gdal.h", "include\gdal_version.h"),
                @("include\ogrsf_frmts.h"),
                @("include\ogr_geometry.h"),
                @("lib\gdal.lib", "lib\gdal.dll")
            )
        }
        "googletest" {
            @(
                @("include\googletest\gtest\gtest.h"),
                @("include\googletest\gmock\gmock.h"),
                @("lib\gtest.lib"),
                @("lib\gmock.lib"),
                @("debug\lib\gtest.lib"),
                @("debug\lib\gmock.lib")
            )
        }
        "htmlrenderer" {
            @(
                @("lib\HtmlRenderer.dll"),
                @("debug\lib\HtmlRenderer.dll")
            )
        }
        "header-only" {
            switch ($Dependency.Name) {
                "boost" { @(@("include\boost\config.hpp")) }
                "rapidjson" { @(@("include\rapidjson\document.h")) }
                default { @() }
            }
        }
        "sqlite" {
            @(
                @("include\sqlite\sqlite3.h"),
                @("lib\sqlite3.lib", "lib\sqlite3.dll")
            )
        }
        "vsqlitepp" {
            @(
                @("lib\vsqlitepp.lib")
            )
        }
        "python" {
            @(
                @("python\python.exe", "python\python312.exe"),
                @("python\Include\Python.h")
            )
        }
        "mysql" {
            @(
                @("include\mysql\mysql.h"),
                @("lib\libmysql.lib", "lib\libmysql.dll")
            )
        }
        "connector-cpp" {
            @(
                @("include\cppconn\driver.h"),
                @("lib\mysqlcppconn.lib", "lib\mysqlcppconn-10-vs14.dll", "lib\mysqlcppconn-10-vs17.dll")
            )
        }
        default { @() }
    })

    if ($requiredGroups.Count -eq 0) {
        return $true
    }

    foreach ($group in $requiredGroups) {
        if (-not (Test-AnyPathExists -RelativePaths @($group))) {
            return $false
        }
    }

    return $true
}

function Load-DependencyManifest {
    if (-not (Test-Path -LiteralPath $script:DepManifestPath -PathType Leaf)) {
        throw ("Dependency manifest not found: {0}" -f $script:DepManifestPath)
    }

    $dependencies = [System.Collections.Generic.List[pscustomobject]]::new()
    $seenNames = @{}
    $allowedTypes = @(
        "openssl",
        "zlib",
        "libxml2",
        "cairo",
        "libzip",
        "antlr4",
        "libssh",
        "proj",
        "gdal",
        "googletest",
        "htmlrenderer",
        "header-only",
        "sqlite",
        "vsqlitepp",
        "python",
        "mysql",
        "connector-cpp"
    )

    $lineNumber = 0
    foreach ($line in Get-Content -LiteralPath $script:DepManifestPath) {
        $lineNumber++
        $trimmed = $line.Trim()
        if (-not $trimmed -or $trimmed.StartsWith("#")) {
            continue
        }

        $parts = @($line.Split("|"))
        if ($parts.Count -ne 4) {
            throw ("Invalid dependency manifest entry at {0}:{1}. Expected format: name|version|type|url" -f $script:DepManifestPath, $lineNumber)
        }

        $name = $parts[0].Trim()
        $version = $parts[1].Trim()
        $type = $parts[2].Trim()
        $urlField = $parts[3].Trim()

        if ([string]::IsNullOrWhiteSpace($name) -or
            [string]::IsNullOrWhiteSpace($version) -or
            [string]::IsNullOrWhiteSpace($type) -or
            [string]::IsNullOrWhiteSpace($urlField)) {
            throw ("Invalid dependency manifest entry at {0}:{1}. None of the four fields may be empty." -f $script:DepManifestPath, $lineNumber)
        }

        $urls = @($urlField.Split(";") | ForEach-Object { $_.Trim() } | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
        if ($urls.Count -eq 0) {
            throw ("Invalid dependency manifest entry at {0}:{1}. At least one download source is required." -f $script:DepManifestPath, $lineNumber)
        }

        if ($type -notin $allowedTypes) {
            throw ("Unsupported dependency type '{0}' at {1}:{2}" -f $type, $script:DepManifestPath, $lineNumber)
        }

        if ($seenNames.ContainsKey($name)) {
            throw ("Duplicate dependency name '{0}' in {1} at lines {2} and {3}" -f $name, $script:DepManifestPath, $seenNames[$name], $lineNumber)
        }

        $seenNames[$name] = $lineNumber
        $dependencies.Add([pscustomobject]@{
            Name = $name
            Version = $version
            Type = $type
            Url = $urls[0]
            Urls = $urls
        })
    }

    if ($dependencies.Count -eq 0) {
        throw ("Dependency manifest is empty: {0}" -f $script:DepManifestPath)
    }

    return $dependencies
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

    $pathOutput = & $vsWhere @args
    $path = if ($null -eq $pathOutput) { "" } else { ([string]::Join([Environment]::NewLine, @($pathOutput))).Trim() }
    if (-not $path) {
        throw "A compatible Visual Studio installation with MSBuild support was not found."
    }

    return $path
}

function Resolve-CMakeGenerator([string]$ResolvedVsPath) {
    if ($Generator -ne "auto") {
        return $Generator
    }

    if ($ResolvedVsPath -match "\\Microsoft Visual Studio\\18\\") {
        return "Visual Studio 18 2026"
    }

    return "Visual Studio 17 2022"
}

function Resolve-VsPlatformToolset([string]$ResolvedVsPath) {
    $auxiliaryBuildDir = Join-Path $ResolvedVsPath "VC\Auxiliary\Build"
    if (Test-Path -LiteralPath $auxiliaryBuildDir -PathType Container) {
        $toolsetCandidates = @(
            Get-ChildItem -LiteralPath $auxiliaryBuildDir -File -Filter "Microsoft.VCToolsVersion.v*.default.props" -ErrorAction SilentlyContinue |
                ForEach-Object {
                    if ($_.Name -match '^Microsoft\.VCToolsVersion\.(v\d+)\.default\.props$') {
                        [pscustomobject]@{
                            Name = $Matches[1]
                            Number = [int]($Matches[1].Substring(1))
                        }
                    }
                } |
                Where-Object { $null -ne $_ } |
                Sort-Object Number -Descending
        )

        if ($toolsetCandidates.Count -gt 0) {
            return $toolsetCandidates[0].Name
        }
    }

    if ($ResolvedVsPath -match "\\Microsoft Visual Studio\\18\\") {
        return "v145"
    }

    return "v143"
}

function Enter-VsBuildEnvironment {
    if ($script:VsEnvLoaded) {
        return
    }

    $vsPath = Get-VSInstallPath
    $script:VsPath = $vsPath
    $script:ResolvedGenerator = Resolve-CMakeGenerator -ResolvedVsPath $vsPath
    $script:ResolvedToolset = if ($PlatformToolset -ne "auto") { $PlatformToolset } else { Resolve-VsPlatformToolset -ResolvedVsPath $vsPath }
    $vcVars = Join-Path $vsPath "VC\Auxiliary\Build\vcvarsall.bat"
    if (-not (Test-Path -LiteralPath $vcVars)) {
        throw "vcvarsall.bat was not found under $vsPath"
    }
    # The compiler version of the toolset, so that nmake, meson and other
    # builds outside MSBuild use the same compiler as the CMake builds.
    $vcVarsArgs = "x64"
    $toolsVersionFile = Join-Path $vsPath ("VC\Auxiliary\Build\Microsoft.VCToolsVersion.{0}.default.txt" -f $script:ResolvedToolset)
    if (Test-Path -LiteralPath $toolsVersionFile -PathType Leaf) {
        $vcVarsArgs += " -vcvars_ver=" + (Get-Content -LiteralPath $toolsVersionFile -TotalCount 1).Trim()
    }
    elseif ($PlatformToolset -ne "auto") {
        throw "The $PlatformToolset toolset is not installed under $vsPath"
    }

    Write-Info "Loading Visual Studio build environment from $vsPath ($($script:ResolvedToolset), vcvarsall $vcVarsArgs)"
    Push-Location (Split-Path -Parent $vcVars)
    try {
        cmd /c "`"$vcVars`" $vcVarsArgs >nul && set" | ForEach-Object {
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
    Write-Ok "Visual Studio environment ready."
}

function Invoke-LoggedCommand {
    param(
        [Parameter(Mandatory)] [string]$Label,
        [Parameter(Mandatory)] [string]$FilePath,
        [string[]]$Arguments = @(),
        [string]$WorkingDirectory = $PWD.Path,
        [switch]$AllowFailure
    )

    if ($VerboseOutput) {
        Write-Detail ("[{0}] {1} {2}" -f $Label, $FilePath, ($Arguments -join " "))
    }
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $output = @()
    $exitCode = 0

    if ($VerboseOutput) {
        Push-Location $WorkingDirectory
        try {
            $previousErrorActionPreference = $ErrorActionPreference
            $hadNativeCommandPreference = Test-Path Variable:\PSNativeCommandUseErrorActionPreference
            if ($hadNativeCommandPreference) {
                $previousNativeCommandPreference = $PSNativeCommandUseErrorActionPreference
            }

            try {
                # Native tools often write progress and diagnostics to stderr even when
                # PowerShell should decide failure from the process exit code.
                $ErrorActionPreference = "Continue"
                if ($hadNativeCommandPreference) {
                    $PSNativeCommandUseErrorActionPreference = $false
                }

                $output = & $FilePath @Arguments 2>&1
                $exitCode = $LASTEXITCODE
            }
            finally {
                $ErrorActionPreference = $previousErrorActionPreference
                if ($hadNativeCommandPreference) {
                    $PSNativeCommandUseErrorActionPreference = $previousNativeCommandPreference
                }
            }
        }
        finally {
            Pop-Location
        }
    }
    else {
        $resolvedCommand = $FilePath
        $commandInfo = Get-Command $FilePath -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($commandInfo -and $commandInfo.Source) {
            $resolvedCommand = $commandInfo.Source
        }

        $stdoutPath = Join-Path $script:WorkRoot ("cmd-" + [guid]::NewGuid().ToString("N") + ".stdout.log")
        $stderrPath = Join-Path $script:WorkRoot ("cmd-" + [guid]::NewGuid().ToString("N") + ".stderr.log")
        try {
            $startProcessParameters = @{
                FilePath = $resolvedCommand
                WorkingDirectory = $WorkingDirectory
                NoNewWindow = $true
                RedirectStandardOutput = $stdoutPath
                RedirectStandardError = $stderrPath
                PassThru = $true
            }
            if ($Arguments.Count -gt 0) {
                $startProcessParameters["ArgumentList"] = (($Arguments | ForEach-Object { ConvertTo-WindowsCommandLineArgument $_ }) -join " ")
            }

            $process = Start-Process @startProcessParameters
            $script:ActiveProcesses.Add($process)

            $lastStatusLine = $null
            $lastHeartbeatBucket = -1
            $lastSeenLineCount = 0
            Start-StatusPanel -Title ("{0} (starting)" -f $Label)
            Push-StatusPanelLine ("working directory: {0}" -f $WorkingDirectory)
            while (-not $process.HasExited) {
                $recentLines = @(Get-RecentProcessLines -Paths @($stderrPath, $stdoutPath) -TailCount 50)
                $elapsedSeconds = $sw.Elapsed.TotalSeconds
                Update-StatusPanelTitle -Title ("{0} ({1:n1}s)" -f $Label, $elapsedSeconds)

                $newLineCount = $recentLines.Count
                if ($newLineCount -gt $lastSeenLineCount) {
                    # Push only lines we haven't shown yet
                    $startIdx = [Math]::Max(0, $newLineCount - ($newLineCount - $lastSeenLineCount))
                    for ($li = $lastSeenLineCount; $li -lt $newLineCount; $li++) {
                        # Clamp index to valid range (tail may have shifted)
                        if ($li -lt $recentLines.Count) {
                            Push-StatusPanelLine -Message $recentLines[$li]
                        }
                    }
                    $lastSeenLineCount = $newLineCount
                    $lastStatusLine = $recentLines[-1]
                }
                elseif ($recentLines.Count -gt 0 -and $recentLines[-1] -ne $lastStatusLine) {
                    # Log file was recycled or shifted — refresh the whole panel
                    $script:StatusPanelLines = [System.Collections.ArrayList]::new()
                    foreach ($rl in $recentLines) {
                        Push-StatusPanelLine -Message $rl
                    }
                    $lastSeenLineCount = $recentLines.Count
                    $lastStatusLine = $recentLines[-1]
                }
                else {
                    $heartbeatBucket = [int][Math]::Floor($elapsedSeconds / 5)
                    if ($heartbeatBucket -gt $lastHeartbeatBucket) {
                        $heartbeatMsg = "still running ({0:n1}s)" -f $elapsedSeconds
                        if ($lastStatusLine) {
                            $heartbeatMsg = "{0} => {1}" -f $heartbeatMsg, $lastStatusLine
                        }
                        Push-StatusPanelLine -Message $heartbeatMsg
                        $lastHeartbeatBucket = $heartbeatBucket
                    }
                }

                Start-Sleep -Milliseconds 250
            }

            $process.WaitForExit()
            $process.Refresh()
            try {
                $exitCode = [int]$process.ExitCode
            }
            catch {
                $exitCode = -1
            }
            $output = @(
                @(if (Test-Path -LiteralPath $stdoutPath -PathType Leaf) { Get-Content -LiteralPath $stdoutPath })
                @(if (Test-Path -LiteralPath $stderrPath -PathType Leaf) { Get-Content -LiteralPath $stderrPath })
            )
        }
        finally {
            Clear-StatusLine
            if ($process) {
                try {
                    if (-not $process.HasExited) {
                        & taskkill /T /F /PID $process.Id 2>$null | Out-Null
                    }
                } catch {}
                $script:ActiveProcesses.Remove($process) | Out-Null
                $process.Dispose()
            }
            foreach ($path in @($stdoutPath, $stderrPath)) {
                if ($path -and (Test-Path -LiteralPath $path -PathType Leaf)) {
                    Remove-FileWithRetry -Path $path
                }
            }
        }
    }

    if ($VerboseOutput) {
        foreach ($line in $output) {
            if ($line -is [System.Management.Automation.ErrorRecord]) {
                Write-Host ("    {0}" -f $line.Exception.Message) -ForegroundColor Red
            }
            else {
                $text = [string]$line
                if ($text -match "error|fatal") {
                    Write-Host ("    {0}" -f $text) -ForegroundColor Red
                }
                elseif ($text -match "warning") {
                    Write-Host ("    {0}" -f $text) -ForegroundColor Yellow
                }
                elseif ($text -match "^\s*(\[\s*\d+%\]|-- )") {
                    Write-Host ("    {0}" -f $text) -ForegroundColor Green
                }
                else {
                    Write-Host ("    {0}" -f $text)
                }
            }
        }
    }

    $sw.Stop()
    if ($null -eq $exitCode -or [string]::IsNullOrWhiteSpace([string]$exitCode)) {
        $exitCode = -1
    }
    if ($exitCode -ne 0 -and -not $AllowFailure) {
        $tail = @($output | Select-Object -Last 40)
        foreach ($line in $tail) {
            Write-Host ("    {0}" -f $line) -ForegroundColor DarkGray
        }
        throw ("Command failed: {0} (exit code {1})" -f $Label, $exitCode)
    }

    if ($exitCode -eq 0) {
        Write-Ok ("{0} ({1:n1}s)" -f $Label, $sw.Elapsed.TotalSeconds)
    }

    return [pscustomobject]@{
        ExitCode = $exitCode
        Output = $output
    }
}

function Invoke-WithPathPrefix {
    param(
        [string[]]$Prefixes = @(),
        [Parameter(Mandatory)] [scriptblock]$Action
    )

    $resolvedPrefixes = @(
        $Prefixes |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
        Where-Object { Test-Path -LiteralPath $_ -PathType Container } |
        Select-Object -Unique
    )

    if ($resolvedPrefixes.Count -eq 0) {
        & $Action
        return
    }

    $originalPath = $env:PATH
    try {
        $env:PATH = (($resolvedPrefixes -join ';') + ';' + $originalPath)
        & $Action
    }
    finally {
        $env:PATH = $originalPath
    }
}

function Copy-DirectoryContent {
    param(
        [Parameter(Mandatory)] [string]$Source,
        [Parameter(Mandatory)] [string]$Destination
    )

    if (-not (Test-Path -LiteralPath $Source -PathType Container)) {
        return
    }

    Ensure-Directory $Destination
    Copy-Item -Path (Join-Path $Source "*") -Destination $Destination -Recurse -Force
}

function Copy-FirstMatch {
    param(
        [Parameter(Mandatory)] [string]$SearchRoot,
        [Parameter(Mandatory)] [string[]]$Patterns,
        [Parameter(Mandatory)] [string]$Destination,
        [switch]$AllowMany
    )

    Ensure-Directory $Destination
    $matches = @()
    foreach ($pattern in $Patterns) {
        $matches += Get-ChildItem -Path $SearchRoot -Recurse -File -Filter $pattern -ErrorAction SilentlyContinue
    }

    $matches = @($matches | Sort-Object FullName -Unique)
    if (-not $matches) {
        return $false
    }

    if (-not $AllowMany) {
        $matches = @($matches[0])
    }

    foreach ($match in $matches) {
        Copy-Item -LiteralPath $match.FullName -Destination (Join-Path $Destination $match.Name) -Force
    }

    return $true
}

function Test-ZipArchiveHealthy {
    param([Parameter(Mandatory)] [string]$ArchivePath)

    try {
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        $zip = [System.IO.Compression.ZipFile]::OpenRead($ArchivePath)
        try {
            return ($zip.Entries.Count -gt 0)
        }
        finally {
            $zip.Dispose()
        }
    }
    catch {
        return $false
    }
}

function Test-ArchiveHealthy {
    param([Parameter(Mandatory)] [string]$ArchivePath)

    if (-not (Test-Path -LiteralPath $ArchivePath -PathType Leaf)) {
        return $false
    }

    if ($ArchivePath.ToLowerInvariant().EndsWith(".zip")) {
        return (Test-ZipArchiveHealthy -ArchivePath $ArchivePath)
    }

    return $true
}

function Test-ExtractedSourceHealthy {
    param([Parameter(Mandatory)] [string]$Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
        return $false
    }

    return (@(Get-ChildItem -LiteralPath $Path -Force -ErrorAction SilentlyContinue | Select-Object -First 1).Count -gt 0)
}

function Resolve-TarEntryDestinationPath {
    param(
        [Parameter(Mandatory)] [string]$RootPath,
        [Parameter(Mandatory)] [string]$EntryName
    )

    $normalizedEntryName = ($EntryName -replace '^[.][/\\]+', '') -replace '/', '\\'
    if ([string]::IsNullOrWhiteSpace($normalizedEntryName) -or $normalizedEntryName -eq '.') {
        return $null
    }

    $rootFullPath = [System.IO.Path]::GetFullPath($RootPath)
    $destinationPath = [System.IO.Path]::GetFullPath((Join-Path $rootFullPath $normalizedEntryName))
    if (-not $destinationPath.StartsWith($rootFullPath, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Archive entry escapes extraction root: $EntryName"
    }

    return $destinationPath
}

function Expand-GZipTarArchiveManaged {
    param(
        [Parameter(Mandatory)] [string]$ArchivePath,
        [Parameter(Mandatory)] [string]$Destination
    )

    Ensure-Directory $Destination
    $pendingLinks = [System.Collections.Generic.List[pscustomobject]]::new()
    $archiveStream = [System.IO.File]::OpenRead($ArchivePath)
    try {
        $gzipStream = [System.IO.Compression.GZipStream]::new($archiveStream, [System.IO.Compression.CompressionMode]::Decompress, $false)
        try {
            $reader = [System.Formats.Tar.TarReader]::new($gzipStream, $false)
            try {
                while ($true) {
                    $entry = $reader.GetNextEntry()
                    if ($null -eq $entry) {
                        break
                    }

                    $destinationPath = Resolve-TarEntryDestinationPath -RootPath $Destination -EntryName $entry.Name
                    if ($null -eq $destinationPath) {
                        continue
                    }

                    switch ($entry.EntryType) {
                        ([System.Formats.Tar.TarEntryType]::Directory) {
                            Ensure-Directory $destinationPath
                            continue
                        }
                        ([System.Formats.Tar.TarEntryType]::RegularFile) {
                            Ensure-Directory (Split-Path -Parent $destinationPath)
                            $fileStream = [System.IO.File]::Create($destinationPath)
                            try {
                                if ($entry.DataStream) {
                                    $entry.DataStream.CopyTo($fileStream)
                                }
                            }
                            finally {
                                $fileStream.Dispose()
                            }
                            continue
                        }
                        ([System.Formats.Tar.TarEntryType]::V7RegularFile) {
                            Ensure-Directory (Split-Path -Parent $destinationPath)
                            $fileStream = [System.IO.File]::Create($destinationPath)
                            try {
                                if ($entry.DataStream) {
                                    $entry.DataStream.CopyTo($fileStream)
                                }
                            }
                            finally {
                                $fileStream.Dispose()
                            }
                            continue
                        }
                        ([System.Formats.Tar.TarEntryType]::SymbolicLink) {
                            $pendingLinks.Add([pscustomobject]@{ Path = $destinationPath; LinkName = [string]$entry.LinkName; EntryType = "symlink" })
                            continue
                        }
                        ([System.Formats.Tar.TarEntryType]::HardLink) {
                            $pendingLinks.Add([pscustomobject]@{ Path = $destinationPath; LinkName = [string]$entry.LinkName; EntryType = "hard link" })
                            continue
                        }
                        default {
                            continue
                        }
                    }
                }
            }
            finally {
                $reader.Dispose()
            }
        }
        finally {
            $gzipStream.Dispose()
        }
    }
    finally {
        $archiveStream.Dispose()
    }

    foreach ($pendingLink in $pendingLinks) {
        if ([string]::IsNullOrWhiteSpace($pendingLink.LinkName)) {
            Write-Warn ("Skipping {0} with empty target: {1}" -f $pendingLink.EntryType, $pendingLink.Path)
            continue
        }

        $linkParent = Split-Path -Parent $pendingLink.Path
        $targetPath = Resolve-TarEntryDestinationPath -RootPath $linkParent -EntryName $pendingLink.LinkName
        if ($null -eq $targetPath -or -not (Test-Path -LiteralPath $targetPath)) {
            Write-Warn ("Skipping {0} because target was not extracted: {1} -> {2}" -f $pendingLink.EntryType, $pendingLink.Path, $pendingLink.LinkName)
            continue
        }

        Ensure-Directory $linkParent
        if (Test-Path -LiteralPath $targetPath -PathType Container) {
            Copy-DirectoryContent -Source $targetPath -Destination $pendingLink.Path
        }
        else {
            Copy-Item -LiteralPath $targetPath -Destination $pendingLink.Path -Force
        }
    }
}

function Restore-MesonInstallOutputs {
    param(
        [Parameter(Mandatory)] [string]$BuildDir,
        [Parameter(Mandatory)] [string]$StageDir
    )

    if (Test-ExtractedSourceHealthy -Path $StageDir) {
        return
    }

    $installedJson = Join-Path $BuildDir "meson-info\intro-installed.json"
    if (-not (Test-Path -LiteralPath $installedJson -PathType Leaf)) {
        return
    }

        $installedMap = Get-Content -LiteralPath $installedJson -Raw | ConvertFrom-Json
        if ($null -eq $installedMap) {
            return
        }

    $restoredCount = 0
        foreach ($installedEntry in $installedMap.PSObject.Properties) {
            $sourcePath = [string]$installedEntry.Name
            $destinationPath = [string]$installedEntry.Value
        if (-not $destinationPath.StartsWith($StageDir, [System.StringComparison]::OrdinalIgnoreCase)) {
            continue
        }
        if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
            continue
        }

        Ensure-Directory (Split-Path -Parent $destinationPath)
        Copy-Item -LiteralPath $sourcePath -Destination $destinationPath -Force
        $restoredCount++
    }

    if ($restoredCount -gt 0) {
        Write-Warn ("Meson install outputs were restored from build metadata: {0}" -f $StageDir)
    }
}

function Ensure-AntlrRuntimeCompatibilityPatch {
    param([Parameter(Mandatory)] [string]$SourcePath)

    $profilingSource = Join-Path $SourcePath "runtime\src\atn\ProfilingATNSimulator.cpp"
    if (-not (Test-Path -LiteralPath $profilingSource -PathType Leaf)) {
        return
    }

    $content = Get-Content -LiteralPath $profilingSource -Raw
    if ($content -match '(?m)^\s*#include <chrono>\s*$') {
        return
    }

    $updated = $content -replace '(#include "support/CPPUtils.h"\r?\n)', "`$1#include <chrono>`r`n"
    if ($updated -ne $content) {
        Set-Content -LiteralPath $profilingSource -Value $updated -NoNewline
        Write-Warn "Applied ANTLR4 MSVC compatibility patch for ProfilingATNSimulator.cpp"
    }
}

function Ensure-PythonSourceArchiveCompatibilityPatch {
    param([Parameter(Mandatory)] [string]$SourcePath)

    $sbomGenerator = Join-Path $SourcePath "Tools\build\generate_sbom.py"
    if (-not (Test-Path -LiteralPath $sbomGenerator -PathType Leaf)) {
        return
    }

    $content = Get-Content -LiteralPath $sbomGenerator -Raw
    $oldSnippet = @'
    assert git_check_ignore_proc.returncode in (0, 1)
'@
    if ($content -notmatch [regex]::Escape($oldSnippet.Trim())) {
        return
    }

    $newSnippet = @'
    if git_check_ignore_proc.returncode not in (0, 1):
        return sorted(paths)
'@
    $updated = $content.Replace($oldSnippet.Trim(), $newSnippet.Trim())
    if ($updated -ne $content) {
        Set-Content -LiteralPath $sbomGenerator -Value $updated -NoNewline
        Write-Warn "Patched Python generate_sbom.py for source archive builds without a git checkout"
    }
}

function Patch-CairoGlibMkenumsCommands {
    param(
        [Parameter(Mandatory)] [string]$BuildDir,
        [Parameter(Mandatory)] [string]$BuildNinjaContent
    )

    $pattern = '(?m)^(?<indent>\s*)COMMAND = "(?<meson>[^"]+)" "--internal" "exe" "--capture" "(?<output>subprojects\\glib-2\.74\.0\\gio\\gioenumtypes\.(?:c|h))" "--" "(?<python>[^"]+)" "(?<script>subprojects/glib-2\.74\.0/gobject/glib-mkenums)" (?<args>.+)$'
    $matches = [regex]::Matches($BuildNinjaContent, $pattern)
    if ($matches.Count -eq 0) {
        return $BuildNinjaContent
    }

    $updatedContent = $BuildNinjaContent
    foreach ($match in $matches) {
        $outputRelativePath = $match.Groups['output'].Value
        $responseAbsolutePath = Join-Path $BuildDir ($outputRelativePath + '.rsp')
        $responseRelativePath = (($outputRelativePath + '.rsp') -replace '\\', '/')
        Ensure-Directory (Split-Path -Parent $responseAbsolutePath)
        Set-Content -LiteralPath $responseAbsolutePath -Value $match.Groups['args'].Value -Encoding ascii -NoNewline

        $replacement = '{0}COMMAND = "{1}" "--internal" "exe" "--capture" "{2}" "--" "{3}" "{4}" "@{5}"' -f `
            $match.Groups['indent'].Value,
            $match.Groups['meson'].Value,
            $outputRelativePath,
            $match.Groups['python'].Value,
            $match.Groups['script'].Value,
            $responseRelativePath
        $updatedContent = $updatedContent.Replace($match.Value, $replacement)
    }

    return $updatedContent
}

function Get-SqliteHeaderRoot {
    $bundleCandidates = @(
        (Join-Path $script:BundleDir "include\sqlite"),
        (Join-Path $script:BundleDir "include")
    )
    foreach ($candidate in $bundleCandidates) {
        if (Test-Path -LiteralPath (Join-Path $candidate "sqlite3.h") -PathType Leaf) {
            return $candidate
        }
    }

    $sourceCandidates = @(Get-ChildItem -LiteralPath $script:SourceRoot -Directory -Filter "sqlite-*" -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending)
    foreach ($candidate in $sourceCandidates) {
        if (Test-Path -LiteralPath (Join-Path $candidate.FullName "sqlite3.h") -PathType Leaf) {
            return $candidate.FullName
        }
    }

    if (-not $script:DepManifest -or $script:DepManifest.Count -eq 0) {
        $script:DepManifest = @(Load-DependencyManifest)
    }

    $sqliteDependency = @($script:DepManifest | Where-Object { $_.Name -eq "sqlite" } | Select-Object -First 1)
    if ($sqliteDependency.Count -gt 0) {
        $sqliteSourcePath = Get-SourceTree $sqliteDependency[0]
        foreach ($candidate in @(
            $sqliteSourcePath,
            (Join-Path $sqliteSourcePath "include"),
            (Join-Path $sqliteSourcePath "sqlite")
        )) {
            if (Test-Path -LiteralPath (Join-Path $candidate "sqlite3.h") -PathType Leaf) {
                return $candidate
            }
        }
    }

    throw "sqlite3.h was not found in the bundle or extracted sqlite sources."
}

function Get-DependencyByName {
    param([Parameter(Mandatory)] [string]$Name)

    if (-not $script:DepManifest -or $script:DepManifest.Count -eq 0) {
        $script:DepManifest = @(Load-DependencyManifest)
    }

    $dependency = @($script:DepManifest | Where-Object { $_.Name -eq $Name } | Select-Object -First 1)
    if ($dependency.Count -eq 0) {
        throw "Dependency '$Name' was not found in the manifest."
    }

    return $dependency[0]
}

function Ensure-SqliteBundleOutputs {
    $sqliteDependency = Get-DependencyByName "sqlite"
    if (-not (Test-DependencyOutputsPresent $sqliteDependency)) {
        Process-Dependency $sqliteDependency
    }
}

function Ensure-SqliteCliShim {
    $toolsDir = Join-Path $script:BuildRoot "tools"
    $shimPy = Join-Path $toolsDir "sqlite3_cli.py"
    $shimCmd = Join-Path $toolsDir "sqlite3.cmd"

    $pythonPath = $null
    foreach ($candidate in @("python.exe", "python", "py.exe", "py")) {
        $command = Get-Command $candidate -ErrorAction SilentlyContinue
        if ($command) {
            $pythonPath = $command.Source
            break
        }
    }

    if (-not $pythonPath) {
        throw "Python is required to emulate sqlite3 for the PROJ build."
    }

    Ensure-Directory $toolsDir
    Set-Content -LiteralPath $shimPy -Value @'
import sqlite3
import sys

def main():
    if len(sys.argv) != 2:
        print("usage: sqlite3_cli.py <database>", file=sys.stderr)
        return 2

    database_path = sys.argv[1]
    sql = sys.stdin.buffer.read().decode("utf-8-sig")

    connection = sqlite3.connect(database_path)
    try:
        connection.executescript(sql)
        connection.commit()
    finally:
        connection.close()

    return 0

if __name__ == "__main__":
    raise SystemExit(main())
'@ -NoNewline
    Set-Content -LiteralPath $shimCmd -Value @(
        "@echo off",
        "`"$pythonPath`" `"$shimPy`" %*"
    )

    return $shimCmd
}

function Download-File {
    param(
        [Parameter(Mandatory)] [string[]]$Sources,
        [Parameter(Mandatory)] [string]$Destination
    )

    if (Test-Path -LiteralPath $Destination -PathType Leaf) {
        if (Test-ArchiveHealthy -ArchivePath $Destination) {
            Write-Ok ("Archive already downloaded: {0}" -f ([System.IO.Path]::GetFileName($Destination)))
            return
        }

        Write-Warn ("Cached archive is invalid - re-downloading {0}" -f ([System.IO.Path]::GetFileName($Destination)))
        Remove-Item -LiteralPath $Destination -Force -ErrorAction SilentlyContinue
    }

    Ensure-Directory (Split-Path -Parent $Destination)
    $errors = @()
    foreach ($source in $Sources) {
        Write-Info "Downloading $source"

        try {
            if ($source -notmatch '^[a-zA-Z][a-zA-Z0-9+.-]*://') {
                if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
                    throw "Local source file was not found."
                }

                Copy-Item -LiteralPath $source -Destination $Destination -Force
                Write-Ok ("Saved archive to {0}" -f $Destination)
                return
            }

            $downloadedViaBits = $false
            if (Get-Module -ListAvailable -Name BitsTransfer) {
                try {
                    Import-Module BitsTransfer
                    Start-BitsTransfer -Source $source -Destination $Destination -DisplayName ([System.IO.Path]::GetFileName($Destination)) -ErrorAction Stop
                    $downloadedViaBits = $true
                }
                catch {
                    Write-Warn ("BITS download failed, falling back to Invoke-WebRequest: {0}" -f $_.Exception.Message)
                    if (Test-Path -LiteralPath $Destination -PathType Leaf) {
                        Remove-Item -LiteralPath $Destination -Force
                    }
                }
            }

            if (-not $downloadedViaBits) {
                $invokeWebRequestParameters = @{
                    Uri = $source
                    OutFile = $Destination
                    ErrorAction = "Stop"
                }
                if ((Get-Command Invoke-WebRequest).Parameters.ContainsKey("UseBasicParsing")) {
                    $invokeWebRequestParameters["UseBasicParsing"] = $true
                }

                $previousProgressPreference = $ProgressPreference
                try {
                    $ProgressPreference = "SilentlyContinue"
                    Invoke-WebRequest @invokeWebRequestParameters
                }
                finally {
                    $ProgressPreference = $previousProgressPreference
                }
            }

            Write-Ok ("Saved archive to {0}" -f $Destination)
            return
        }
        catch {
            if (Test-Path -LiteralPath $Destination -PathType Leaf) {
                Remove-Item -LiteralPath $Destination -Force -ErrorAction SilentlyContinue
            }
            $message = $_.Exception.Message
            if ($source -like "https://dev.mysql.com/*" -and $message -match "\(403\)\s+Forbidden") {
                $message = "{0}. Oracle blocked the automated download. Add a fallback local archive path in {1} using ';' separators." -f $message, $script:DepManifestPath
            }
            $errors += ("{0} -> {1}" -f $source, $message)
            Write-Warn ("Download source failed: {0}" -f $source)
        }
    }

    throw ("All download sources failed for {0}: {1}" -f ([System.IO.Path]::GetFileName($Destination)), ($errors -join " | "))
}

function Expand-ArchiveSmart {
    param(
        [Parameter(Mandatory)] [string]$ArchivePath,
        [Parameter(Mandatory)] [string]$Destination
    )

    if (Test-Path -LiteralPath $Destination -PathType Container) {
        if (Test-ExtractedSourceHealthy -Path $Destination) {
            Write-Ok ("Source already extracted: {0}" -f $Destination)
            return
        }

        Write-Warn ("Extracted source is incomplete - re-extracting {0}" -f $Destination)
        Remove-Item -LiteralPath $Destination -Recurse -Force
    }

    $tempDir = $Destination + ".extracting"
    Reset-Directory $tempDir
    $lower = $ArchivePath.ToLowerInvariant()

    if ($lower.EndsWith(".zip")) {
        Expand-Archive -LiteralPath $ArchivePath -DestinationPath $tempDir -Force
    }
    elseif ($lower.EndsWith(".tar.gz") -or $lower.EndsWith(".tgz")) {
        try {
            Expand-GZipTarArchiveManaged -ArchivePath $ArchivePath -Destination $tempDir
        }
        catch {
            Write-Warn ("Managed tar.gz extraction failed, falling back to tar.exe: {0}" -f $_.Exception.Message)
            Reset-Directory $tempDir
            Invoke-LoggedCommand -Label "extract $([System.IO.Path]::GetFileName($ArchivePath))" -FilePath "tar" -Arguments @("-xf", $ArchivePath, "-C", $tempDir) -WorkingDirectory $script:ProjectRoot | Out-Null
        }
    }
    else {
        Invoke-LoggedCommand -Label "extract $([System.IO.Path]::GetFileName($ArchivePath))" -FilePath "tar" -Arguments @("-xf", $ArchivePath, "-C", $tempDir) -WorkingDirectory $script:ProjectRoot | Out-Null
    }

    $children = @(Get-ChildItem -LiteralPath $tempDir -Force)
    if ($children.Count -eq 1 -and $children[0].PSIsContainer) {
        Move-Item -LiteralPath $children[0].FullName -Destination $Destination
        Remove-Item -LiteralPath $tempDir -Recurse -Force
    }
    else {
        Move-Item -LiteralPath $tempDir -Destination $Destination
    }

    Write-Ok ("Extracted to {0}" -f $Destination)
}

function Get-SourceTree {
    param([pscustomobject]$Dependency)

    $archivePath = Get-ArchivePath $Dependency
    $extractPath = Get-ExtractPath $Dependency

    if (-not $BuildOnly) {
        $sources = if ($Dependency.PSObject.Properties["Urls"] -and $Dependency.Urls.Count -gt 0) { $Dependency.Urls } else { @($Dependency.Url) }
        Download-File -Sources $sources -Destination $archivePath
    }
    elseif (-not (Test-Path -LiteralPath $archivePath -PathType Leaf)) {
        throw "--BuildOnly was requested but archive is missing: $archivePath"
    }

    if (-not $DownloadOnly) {
        Expand-ArchiveSmart -ArchivePath $archivePath -Destination $extractPath
    }

    return $extractPath
}

function New-DualBuildLayout {
    param([Parameter(Mandatory)] [string]$Name)

    $layout = [pscustomobject]@{
        BuildRelease = Join-Path $script:BuildRoot "$Name\release"
        BuildDebug = Join-Path $script:BuildRoot "$Name\debug"
        StageRelease = Join-Path $script:BuildRoot "$Name\stage-release"
        StageDebug = Join-Path $script:BuildRoot "$Name\stage-debug"
    }

    foreach ($path in @($layout.BuildRelease, $layout.BuildDebug, $layout.StageRelease, $layout.StageDebug)) {
        if ($Clean -and (Test-Path -LiteralPath $path -PathType Container)) {
            Remove-Item -LiteralPath $path -Recurse -Force
        }
        Ensure-Directory $path
    }

    return $layout
}

function Invoke-CMakeInstallPair {
    param(
        [Parameter(Mandatory)] [string]$Label,
        [Parameter(Mandatory)] [string]$SourcePath,
        [Parameter(Mandatory)] $Layout,
        [Parameter(Mandatory)] [string[]]$CommonArguments,
        [string]$ReleaseConfig = "RelWithDebInfo",
        [string]$DebugConfig = "Debug"
    )

    $configureReleaseArgs = @(
        "-S", $SourcePath,
        "-B", $Layout.BuildRelease,
        "-G", $script:ResolvedGenerator, "-T", $script:ResolvedToolset,
        "-A", "x64",
        "-DCMAKE_INSTALL_PREFIX=$($Layout.StageRelease)"
    ) + $CommonArguments
    Invoke-LoggedCommand -Label "$Label configure release" -FilePath "cmake" -Arguments $configureReleaseArgs -WorkingDirectory $script:ProjectRoot | Out-Null

    Invoke-LoggedCommand -Label "$Label build release" -FilePath "cmake" -Arguments @(
        "--build", $Layout.BuildRelease,
        "--target", "INSTALL",
        "--config", $ReleaseConfig,
        "--parallel", $Jobs
    ) -WorkingDirectory $script:ProjectRoot | Out-Null

    $configureDebugArgs = @(
        "-S", $SourcePath,
        "-B", $Layout.BuildDebug,
        "-G", $script:ResolvedGenerator, "-T", $script:ResolvedToolset,
        "-A", "x64",
        "-DCMAKE_INSTALL_PREFIX=$($Layout.StageDebug)"
    ) + $CommonArguments
    Invoke-LoggedCommand -Label "$Label configure debug" -FilePath "cmake" -Arguments $configureDebugArgs -WorkingDirectory $script:ProjectRoot | Out-Null

    Invoke-LoggedCommand -Label "$Label build debug" -FilePath "cmake" -Arguments @(
        "--build", $Layout.BuildDebug,
        "--target", "INSTALL",
        "--config", $DebugConfig,
        "--parallel", $Jobs
    ) -WorkingDirectory $script:ProjectRoot | Out-Null
}

function Copy-MatchingFiles {
    param(
        [Parameter(Mandatory)] [string[]]$SearchRoots,
        [Parameter(Mandatory)] [string[]]$Patterns,
        [Parameter(Mandatory)] [string]$Destination
    )

    Ensure-Directory $Destination
    foreach ($root in $SearchRoots) {
        if (-not (Test-Path -LiteralPath $root -PathType Container)) {
            continue
        }

        foreach ($pattern in $Patterns) {
            $items = Get-ChildItem -Path $root -Recurse -File -Filter $pattern -ErrorAction SilentlyContinue
            foreach ($item in $items) {
                Copy-Item -LiteralPath $item.FullName -Destination (Join-Path $Destination $item.Name) -Force
            }
        }
    }
}

function Build-OpenSsl {
    param([pscustomobject]$Dependency)

    Enter-VsBuildEnvironment
    $sourcePath = Get-SourceTree $Dependency
    if ($DownloadOnly) { return }

    $layout = New-DualBuildLayout "openssl"
    $srcRelease = Join-Path $script:BuildRoot "openssl\src-release"
    $srcDebug = Join-Path $script:BuildRoot "openssl\src-debug"
    foreach ($path in @($layout.StageRelease, $layout.StageDebug, $srcRelease, $srcDebug)) {
        Reset-Directory $path
    }
    foreach ($path in @($srcRelease, $srcDebug)) {
        Copy-Item -Path (Join-Path $sourcePath "*") -Destination $path -Recurse -Force
    }

    Invoke-LoggedCommand -Label "openssl configure release" -FilePath "perl" -WorkingDirectory $srcRelease -Arguments @(
        "Configure", "VC-WIN64A", "shared", "no-tests",
        "--prefix=$($layout.StageRelease)",
        "--openssldir=$($layout.StageRelease)"
    ) | Out-Null
    Invoke-LoggedCommand -Label "openssl build release" -FilePath "nmake" -WorkingDirectory $srcRelease | Out-Null
    Invoke-LoggedCommand -Label "openssl install release" -FilePath "nmake" -WorkingDirectory $srcRelease -Arguments @("install_sw") | Out-Null

    Invoke-LoggedCommand -Label "openssl configure debug" -FilePath "perl" -WorkingDirectory $srcDebug -Arguments @(
        "Configure", "debug-VC-WIN64A", "shared", "no-tests",
        "--prefix=$($layout.StageDebug)",
        "--openssldir=$($layout.StageDebug)"
    ) | Out-Null
    Invoke-LoggedCommand -Label "openssl build debug" -FilePath "nmake" -WorkingDirectory $srcDebug | Out-Null
    Invoke-LoggedCommand -Label "openssl install debug" -FilePath "nmake" -WorkingDirectory $srcDebug -Arguments @("install_sw") | Out-Null

    Copy-DirectoryContent -Source (Join-Path $layout.StageRelease "include") -Destination (Join-Path $script:BundleDir "include")
    Copy-MatchingFiles -SearchRoots @((Join-Path $layout.StageRelease "bin"), (Join-Path $layout.StageRelease "lib")) -Patterns @("*.dll", "*.lib", "*.pdb") -Destination (Join-Path $script:BundleDir "lib")
    Copy-MatchingFiles -SearchRoots @((Join-Path $layout.StageDebug "bin"), (Join-Path $layout.StageDebug "lib")) -Patterns @("*.dll", "*.lib", "*.pdb") -Destination (Join-Path $script:BundleDir "debug\lib")

    Write-Ok "OpenSSL release and debug artefacts staged."
}

function Build-ZlibSource {
    param([pscustomobject]$Dependency)

    Enter-VsBuildEnvironment
    $sourcePath = Get-SourceTree $Dependency
    if ($DownloadOnly) { return }

    $layout = New-DualBuildLayout "zlib"
    $commonArgs = @("-DBUILD_SHARED_LIBS=ON")
    Invoke-CMakeInstallPair -Label "zlib" -SourcePath $sourcePath -Layout $layout -CommonArguments $commonArgs -ReleaseConfig "Release" -DebugConfig "Debug"

    $zlibIncludeDest = Join-Path $script:BundleDir "include\zlib"
    $zlibReleaseRoots = @((Join-Path $layout.StageRelease "bin"), (Join-Path $layout.StageRelease "lib"), $layout.BuildRelease)
    $zlibDebugRoots = @((Join-Path $layout.StageDebug "bin"), (Join-Path $layout.StageDebug "lib"), $layout.BuildDebug)

    Ensure-Directory $zlibIncludeDest
    # zlib's CMake build generates zconf.h in the build/install tree and may rename the source copy.
    Copy-MatchingFiles -SearchRoots @((Join-Path $layout.StageRelease "include"), $layout.BuildRelease, $sourcePath) -Patterns @("zlib.h", "zconf.h", "*.h") -Destination $zlibIncludeDest
    # Some generators populate the install tree, others leave the usable artefacts in the config subdirs.
    Copy-MatchingFiles -SearchRoots $zlibReleaseRoots -Patterns @("zlib*.dll", "zlib*.lib", "zlib*.pdb") -Destination (Join-Path $script:BundleDir "lib")
    Copy-MatchingFiles -SearchRoots $zlibDebugRoots -Patterns @("zlib*.dll", "zlib*.lib", "zlib*.pdb") -Destination (Join-Path $script:BundleDir "debug\lib")

    Write-Ok "zlib release and debug artefacts staged."
}

function Build-LibXml2 {
    param([pscustomobject]$Dependency)

    Enter-VsBuildEnvironment
    $sourcePath = Get-SourceTree $Dependency
    if ($DownloadOnly) { return }

    $layout = New-DualBuildLayout "libxml2"
    $commonArgs = @(
        "-DLIBXML2_WITH_ICONV=OFF",
        "-DLIBXML2_WITH_PYTHON=OFF",
        "-DLIBXML2_WITH_TESTS=OFF",
        "-DBUILD_SHARED_LIBS=ON"
    )
    Invoke-CMakeInstallPair -Label "libxml2" -SourcePath $sourcePath -Layout $layout -CommonArguments $commonArgs -ReleaseConfig "Release" -DebugConfig "Debug"

    Copy-DirectoryContent -Source (Join-Path $layout.StageRelease "include") -Destination (Join-Path $script:BundleDir "include")
    Copy-MatchingFiles -SearchRoots @((Join-Path $layout.StageRelease "bin"), (Join-Path $layout.StageRelease "lib")) -Patterns @("libxml2*.dll", "libxml2*.lib", "libxml2*.pdb", "iconv*.dll", "iconv*.lib", "iconv*.pdb") -Destination (Join-Path $script:BundleDir "lib")
    Copy-MatchingFiles -SearchRoots @((Join-Path $layout.StageDebug "bin"), (Join-Path $layout.StageDebug "lib")) -Patterns @("libxml2*.dll", "libxml2*.lib", "libxml2*.pdb", "iconv*.dll", "iconv*.lib", "iconv*.pdb") -Destination (Join-Path $script:BundleDir "debug\lib")

    Write-Ok "libxml2 release and debug artefacts staged."
}

function Build-CairoBundle {
    param([pscustomobject]$Dependency)

    Enter-VsBuildEnvironment
    $sourcePath = Get-SourceTree $Dependency
    if ($DownloadOnly) { return }

    $layout = New-DualBuildLayout "cairo"
    Reset-Directory $layout.BuildRelease
    Reset-Directory $layout.BuildDebug
    Reset-Directory $layout.StageRelease
    Reset-Directory $layout.StageDebug
    $bundleZlibInclude = (Join-Path $script:BundleDir "include\zlib") -replace "\\", "/"
    $bundleZlibReleaseLib = (Join-Path $script:BundleDir "lib\zlib.lib") -replace "\\", "/"
    $bundleZlibDebugLib = (Join-Path $script:BundleDir "debug\lib\zlibd.lib") -replace "\\", "/"
    $gdbusCodegenUtilsPath = Join-Path $sourcePath "subprojects\glib-2.74.0\gio\gdbus-2.0\codegen\utils.py"
    $gdbusCodegenCompatImport = @'
try:
    from distutils.version import LooseVersion as _LooseVersion
except ModuleNotFoundError:
    try:
        from setuptools._distutils.version import LooseVersion as _LooseVersion
    except ModuleNotFoundError:
        import re

        class _LooseVersion:
            def __init__(self, value):
                self._value = str(value)
                self._parts = tuple(
                    (0, int(part)) if part.isdigit() else (1, part.lower())
                    for part in re.findall(r"\d+|[A-Za-z]+", self._value)
                )

            def _cmp_key(self):
                return self._parts

            def __lt__(self, other):
                return self._cmp_key() < other._cmp_key()

            def __le__(self, other):
                return self._cmp_key() <= other._cmp_key()

            def __eq__(self, other):
                return self._cmp_key() == other._cmp_key()

            def __ne__(self, other):
                return self._cmp_key() != other._cmp_key()

            def __gt__(self, other):
                return self._cmp_key() > other._cmp_key()

            def __ge__(self, other):
                return self._cmp_key() >= other._cmp_key()
'@
    if (-not (Test-Path -LiteralPath $gdbusCodegenUtilsPath -PathType Leaf)) {
        # glib is a meson subproject that "meson setup" would fetch only after
        # this point. Fetch it now so the patch below reaches it. Only glib:
        # parallel downloads of all subprojects can fail on Windows with
        # "Resource deadlock avoided" from meson's lock file.
        Invoke-LoggedCommand -Label "cairo meson subprojects download glib" -FilePath "meson" -Arguments @("subprojects", "download", "--sourcedir", $sourcePath, "glib") -WorkingDirectory $sourcePath | Out-Null
    }
    if (Test-Path -LiteralPath $gdbusCodegenUtilsPath -PathType Leaf) {
        $gdbusCodegenUtilsContent = Get-Content -LiteralPath $gdbusCodegenUtilsPath -Raw
        $patchedGdbusCodegenUtilsContent = $gdbusCodegenUtilsContent -replace 'import distutils\.version', $gdbusCodegenCompatImport
        $patchedGdbusCodegenUtilsContent = $patchedGdbusCodegenUtilsContent -replace 'distutils\.version\.LooseVersion', '_LooseVersion'
        if ($patchedGdbusCodegenUtilsContent -ne $gdbusCodegenUtilsContent) {
            Set-Content -LiteralPath $gdbusCodegenUtilsPath -Value $patchedGdbusCodegenUtilsContent -Encoding utf8 -NoNewline
            Write-Warn "Patched Cairo gdbus-codegen for Python distutils removal"
        }
    }

    $clPath = (Get-Command "cl.exe" -ErrorAction Stop).Source
    $linkPath = (Get-Command "link.exe" -ErrorAction Stop).Source
    $libPath = (Get-Command "lib.exe" -ErrorAction Stop).Source
    $rcPath = (Get-Command "rc.exe" -ErrorAction Stop).Source
    $pkgConfigPath = (Get-Command "pkg-config.exe" -ErrorAction Stop).Source
    $msysBinPath = Split-Path -Parent $pkgConfigPath
    $pythonPath = Join-Path $msysBinPath "python.exe"
    if (-not (Test-Path -LiteralPath $pythonPath -PathType Leaf)) {
        $pythonPath = (Get-Command "python.exe" -ErrorAction Stop | Select-Object -First 1).Source
    }
    $normalizedPythonPath = $pythonPath -replace "\\", "/"
    $ninjaPythonPath = $normalizedPythonPath -replace "^([A-Za-z]):", '$1$:' 
    $libffiDllPath = Join-Path (Split-Path -Parent $pkgConfigPath) "libffi-8.dll"
    $libffiImportLibDir = Join-Path $script:BuildRoot "cairo\ffi-msvc"
    $libffiDefPath = Join-Path $libffiImportLibDir "libffi-8.def"
    $libffiImportLibPath = Join-Path $libffiImportLibDir "libffi.lib"
    $mesonNativeFile = Join-Path $script:BuildRoot "cairo\msvc-native.ini"
    $mesonNativeFileContent = @(
        "[binaries]",
        ("c = '{0}'" -f ($clPath -replace "\\", "/")),
        ("cpp = '{0}'" -f ($clPath -replace "\\", "/")),
        ("c_ld = '{0}'" -f ($linkPath -replace "\\", "/")),
        ("cpp_ld = '{0}'" -f ($linkPath -replace "\\", "/")),
        ("ar = '{0}'" -f ($libPath -replace "\\", "/")),
        ("windres = '{0}'" -f ($rcPath -replace "\\", "/")),
        ("pkgconfig = '{0}'" -f ($pkgConfigPath -replace "\\", "/")),
        ("python = '{0}'" -f ($pythonPath -replace "\\", "/"))
    )
    Set-Content -LiteralPath $mesonNativeFile -Value ($mesonNativeFileContent -join [Environment]::NewLine) -Encoding ascii

    if (-not (Test-Path -LiteralPath $libffiDllPath -PathType Leaf)) {
        throw "libffi runtime DLL was not found for Cairo build: $libffiDllPath"
    }
    Ensure-Directory $libffiImportLibDir
    $dumpbinOutput = & "dumpbin.exe" /exports $libffiDllPath 2>&1
    $exportNames = @(
        $dumpbinOutput |
        Where-Object { $_ -match '^\s+\d+\s+[0-9A-F]+\s+[0-9A-F]+\s+([A-Za-z0-9_]+)$' } |
        ForEach-Object { $Matches[1] }
    )
    if (-not $exportNames) {
        throw "Unable to extract libffi exports from $libffiDllPath"
    }
    $defContent = @("LIBRARY libffi-8.dll", "EXPORTS") + $exportNames
    Set-Content -LiteralPath $libffiDefPath -Value ($defContent -join [Environment]::NewLine) -Encoding ascii
    Invoke-LoggedCommand -Label "cairo libffi import lib" -FilePath "lib.exe" -Arguments @("/NOLOGO", "/DEF:$libffiDefPath", "/OUT:$libffiImportLibPath", "/MACHINE:x64") -WorkingDirectory $libffiImportLibDir | Out-Null

    $mesonArgs = @(
        "--native-file", $mesonNativeFile,
        "--default-library", "shared",
        "-Dtests=disabled",
        "-Dgtk_doc=false",
        "-Dglib:tests=false",
        "-Dglib:installed_tests=false",
        "-Dglib:gtk_doc=false"
    )
    $releaseArgs = @(
        "setup", $layout.BuildRelease, $sourcePath,
        "--prefix", $layout.StageRelease,
        "--buildtype", "plain",
        "-Ddebug=false",
        "-Doptimization=2",
        "-Db_ndebug=true",
        "-Db_vscrt=md"
    ) + $mesonArgs
    $debugArgs = @(
        "setup", $layout.BuildDebug, $sourcePath,
        "--prefix", $layout.StageDebug,
        "--buildtype", "plain",
        "-Ddebug=true",
        "-Doptimization=0",
        "-Db_ndebug=false",
        "-Db_vscrt=mdd"
    ) + $mesonArgs

    Invoke-LoggedCommand -Label "cairo meson setup release" -FilePath "meson" -Arguments $releaseArgs -WorkingDirectory $script:ProjectRoot | Out-Null
    $releaseBuildNinja = Join-Path $layout.BuildRelease "build.ninja"
    if (Test-Path -LiteralPath $releaseBuildNinja -PathType Leaf) {
        $releaseBuildNinjaContent = Get-Content -LiteralPath $releaseBuildNinja -Raw
        $releaseSanitizedBuildNinjaContent = $releaseBuildNinjaContent -replace '"/release"\s*', ''
        $releaseSanitizedBuildNinjaContent = $releaseSanitizedBuildNinjaContent -replace [regex]::Escape('"-IC:/msys64/mingw64/bin/../include"'), ('"-I{0}" "-IC:/msys64/mingw64/bin/../include"' -f $bundleZlibInclude)
        $releaseSanitizedBuildNinjaContent = $releaseSanitizedBuildNinjaContent -replace [regex]::Escape('"C:/msys64/mingw64/bin/../lib/libz.a"'), ('"{0}"' -f $bundleZlibReleaseLib)
        $releaseSanitizedBuildNinjaContent = $releaseSanitizedBuildNinjaContent -replace [regex]::Escape('"C:/msys64/mingw64/bin/../lib/../lib/libffi.a"'), ('"{0}"' -f (($libffiImportLibPath) -replace "\\", "/"))
        $releaseSanitizedBuildNinjaContent = $releaseSanitizedBuildNinjaContent -replace '"[A-Za-z]:/[^"\r\n]*/python\.exe"', ('"{0}"' -f $normalizedPythonPath)
        $releaseSanitizedBuildNinjaContent = $releaseSanitizedBuildNinjaContent -replace '[A-Za-z]\$:/[^ \r\n"]*/python\.exe', $ninjaPythonPath
        $releaseSanitizedBuildNinjaContent = Patch-CairoGlibMkenumsCommands -BuildDir $layout.BuildRelease -BuildNinjaContent $releaseSanitizedBuildNinjaContent
        if ($releaseSanitizedBuildNinjaContent -ne $releaseBuildNinjaContent) {
            Set-Content -LiteralPath $releaseBuildNinja -Value $releaseSanitizedBuildNinjaContent -Encoding ascii -NoNewline
            Write-Warn "Sanitized Cairo release build.ninja for MSVC linker and bundled zlib/libffi/python usage"
        }
    }
    Invoke-LoggedCommand -Label "cairo ninja release" -FilePath "ninja" -Arguments @("-C", $layout.BuildRelease) -WorkingDirectory $script:ProjectRoot | Out-Null
    Invoke-LoggedCommand -Label "cairo install release" -FilePath "ninja" -Arguments @("-C", $layout.BuildRelease, "install") -WorkingDirectory $script:ProjectRoot | Out-Null

    Invoke-LoggedCommand -Label "cairo meson setup debug" -FilePath "meson" -Arguments $debugArgs -WorkingDirectory $script:ProjectRoot | Out-Null
    $debugBuildNinja = Join-Path $layout.BuildDebug "build.ninja"
    if (Test-Path -LiteralPath $debugBuildNinja -PathType Leaf) {
        $debugBuildNinjaContent = Get-Content -LiteralPath $debugBuildNinja -Raw
        $debugSanitizedBuildNinjaContent = $debugBuildNinjaContent -replace '"/release"\s*', ''
        $debugSanitizedBuildNinjaContent = $debugSanitizedBuildNinjaContent -replace [regex]::Escape('"-IC:/msys64/mingw64/bin/../include"'), ('"-I{0}" "-IC:/msys64/mingw64/bin/../include"' -f $bundleZlibInclude)
        $debugSanitizedBuildNinjaContent = $debugSanitizedBuildNinjaContent -replace [regex]::Escape('"C:/msys64/mingw64/bin/../lib/libz.a"'), ('"{0}"' -f $bundleZlibDebugLib)
        $debugSanitizedBuildNinjaContent = $debugSanitizedBuildNinjaContent -replace [regex]::Escape('"C:/msys64/mingw64/bin/../lib/../lib/libffi.a"'), ('"{0}"' -f (($libffiImportLibPath) -replace "\\", "/"))
        $debugSanitizedBuildNinjaContent = $debugSanitizedBuildNinjaContent -replace '"[A-Za-z]:/[^"\r\n]*/python\.exe"', ('"{0}"' -f $normalizedPythonPath)
        $debugSanitizedBuildNinjaContent = $debugSanitizedBuildNinjaContent -replace '[A-Za-z]\$:/[^ \r\n"]*/python\.exe', $ninjaPythonPath
        $debugSanitizedBuildNinjaContent = Patch-CairoGlibMkenumsCommands -BuildDir $layout.BuildDebug -BuildNinjaContent $debugSanitizedBuildNinjaContent
        if ($debugSanitizedBuildNinjaContent -ne $debugBuildNinjaContent) {
            Set-Content -LiteralPath $debugBuildNinja -Value $debugSanitizedBuildNinjaContent -Encoding ascii -NoNewline
            Write-Warn "Sanitized Cairo debug build.ninja for MSVC linker and bundled zlib/libffi/python usage"
        }
    }
    Invoke-LoggedCommand -Label "cairo ninja debug" -FilePath "ninja" -Arguments @("-C", $layout.BuildDebug) -WorkingDirectory $script:ProjectRoot | Out-Null
    Invoke-LoggedCommand -Label "cairo install debug" -FilePath "ninja" -Arguments @("-C", $layout.BuildDebug, "install") -WorkingDirectory $script:ProjectRoot | Out-Null

    Restore-MesonInstallOutputs -BuildDir $layout.BuildRelease -StageDir $layout.StageRelease
    Restore-MesonInstallOutputs -BuildDir $layout.BuildDebug -StageDir $layout.StageDebug

    Copy-DirectoryContent -Source (Join-Path $layout.StageRelease "include") -Destination (Join-Path $script:BundleDir "include")
    Copy-DirectoryContent -Source (Join-Path $layout.StageRelease "lib\glib-2.0\include") -Destination (Join-Path $script:BundleDir "lib\glib-2.0\include")
    Copy-MatchingFiles -SearchRoots @((Join-Path $layout.StageRelease "bin"), (Join-Path $layout.StageRelease "lib")) -Patterns @("*.dll", "*.lib", "*.pdb") -Destination (Join-Path $script:BundleDir "lib")
    Copy-MatchingFiles -SearchRoots @((Join-Path $layout.StageDebug "bin"), (Join-Path $layout.StageDebug "lib")) -Patterns @("*.dll", "*.lib", "*.pdb") -Destination (Join-Path $script:BundleDir "debug\lib")
    Copy-Item -LiteralPath $libffiDllPath -Destination (Join-Path $script:BundleDir "lib\libffi-8.dll") -Force
    Copy-Item -LiteralPath $libffiDllPath -Destination (Join-Path $script:BundleDir "debug\lib\libffi-8.dll") -Force
    Copy-Item -LiteralPath $libffiImportLibPath -Destination (Join-Path $script:BundleDir "lib\libffi.lib") -Force
    Copy-Item -LiteralPath $libffiImportLibPath -Destination (Join-Path $script:BundleDir "debug\lib\libffi.lib") -Force

    Write-Ok "cairo/glib bundle staged."
}

function Build-LibZip {
    param([pscustomobject]$Dependency)

    Enter-VsBuildEnvironment
    $sourcePath = Get-SourceTree $Dependency
    if ($DownloadOnly) { return }

    $layout = New-DualBuildLayout "libzip"
    $commonArgs = @(
        "-DBUILD_SHARED_LIBS=ON",
        "-DBUILD_TOOLS=OFF",
        "-DBUILD_REGRESS=OFF",
        "-DBUILD_EXAMPLES=OFF",
        "-DENABLE_BZIP2=OFF",
        "-DENABLE_LZMA=OFF",
        "-DENABLE_ZSTD=OFF",
        "-DENABLE_COMMONCRYPTO=OFF",
        "-DENABLE_GNUTLS=OFF",
        "-DENABLE_MBEDTLS=OFF",
        "-DENABLE_OPENSSL=OFF",
        "-DZLIB_INCLUDE_DIR=$([System.IO.Path]::Combine($script:BundleDir, 'include', 'zlib'))",
        "-DZLIB_LIBRARY=$([System.IO.Path]::Combine($script:BundleDir, 'lib', 'zlib.lib'))",
        "-DZLIB_LIBRARY_RELEASE=$([System.IO.Path]::Combine($script:BundleDir, 'lib', 'zlib.lib'))",
        "-DZLIB_LIBRARY_DEBUG=$([System.IO.Path]::Combine($script:BundleDir, 'debug', 'lib', 'zlibd.lib'))"
    )
    Invoke-CMakeInstallPair -Label "libzip" -SourcePath $sourcePath -Layout $layout -CommonArguments $commonArgs -ReleaseConfig "Release" -DebugConfig "Debug"

    Copy-DirectoryContent -Source (Join-Path $layout.StageRelease "include") -Destination (Join-Path $script:BundleDir "include")
    Copy-MatchingFiles -SearchRoots @((Join-Path $layout.StageRelease "bin"), (Join-Path $layout.StageRelease "lib")) -Patterns @("zip*.dll", "zip*.lib", "zip*.pdb") -Destination (Join-Path $script:BundleDir "lib")
    Copy-MatchingFiles -SearchRoots @((Join-Path $layout.StageDebug "bin"), (Join-Path $layout.StageDebug "lib")) -Patterns @("zip*.dll", "zip*.lib", "zip*.pdb") -Destination (Join-Path $script:BundleDir "debug\lib")

    Write-Ok "libzip release and debug artefacts staged."
}

function Build-AntlrRuntime {
    param([pscustomobject]$Dependency)

    Enter-VsBuildEnvironment
    $antlrToolJar = Ensure-AntlrToolJarDownloaded $Dependency
    $sourcePath = Get-SourceTree $Dependency
    if ($DownloadOnly) { return }

    $cmakeRoot = $sourcePath
    if (-not (Test-Path -LiteralPath (Join-Path $cmakeRoot "CMakeLists.txt") -PathType Leaf) -and
        (Test-Path -LiteralPath (Join-Path $cmakeRoot "runtime\Cpp\CMakeLists.txt") -PathType Leaf)) {
        $cmakeRoot = Join-Path $cmakeRoot "runtime\Cpp"
    }

    $layout = New-DualBuildLayout "antlr4"
    Ensure-AntlrRuntimeCompatibilityPatch -SourcePath $sourcePath
    $commonArgs = @(
        "-DBUILD_SHARED_LIBS=ON",
        "-DANTLR4_INSTALL=ON",
        "-DANTLR_BUILD_CPP_TESTS=OFF",
        "-DWITH_DEMO=OFF"
    )

    $configureReleaseArgs = @(
        "-S", $cmakeRoot,
        "-B", $layout.BuildRelease,
        "-G", $script:ResolvedGenerator, "-T", $script:ResolvedToolset,
        "-A", "x64",
        "-DCMAKE_INSTALL_PREFIX=$($layout.StageRelease)"
    ) + $commonArgs
    Invoke-LoggedCommand -Label "antlr4-runtime configure release" -FilePath "cmake" -Arguments $configureReleaseArgs -WorkingDirectory $script:ProjectRoot | Out-Null
    Invoke-LoggedCommand -Label "antlr4-runtime build release" -FilePath "cmake" -Arguments @(
        "--build", $layout.BuildRelease,
        "--target", "antlr4_shared", "antlr4_static",
        "--config", "Release",
        "--parallel", $Jobs
    ) -WorkingDirectory $script:ProjectRoot | Out-Null
    Invoke-LoggedCommand -Label "antlr4-runtime install release" -FilePath "cmake" -Arguments @(
        "--install", $layout.BuildRelease,
        "--config", "Release",
        "--prefix", $layout.StageRelease
    ) -WorkingDirectory $script:ProjectRoot | Out-Null

    $configureDebugArgs = @(
        "-S", $cmakeRoot,
        "-B", $layout.BuildDebug,
        "-G", $script:ResolvedGenerator, "-T", $script:ResolvedToolset,
        "-A", "x64",
        "-DCMAKE_INSTALL_PREFIX=$($layout.StageDebug)"
    ) + $commonArgs
    Invoke-LoggedCommand -Label "antlr4-runtime configure debug" -FilePath "cmake" -Arguments $configureDebugArgs -WorkingDirectory $script:ProjectRoot | Out-Null
    Invoke-LoggedCommand -Label "antlr4-runtime build debug" -FilePath "cmake" -Arguments @(
        "--build", $layout.BuildDebug,
        "--target", "antlr4_shared", "antlr4_static",
        "--config", "Debug",
        "--parallel", $Jobs
    ) -WorkingDirectory $script:ProjectRoot | Out-Null
    Invoke-LoggedCommand -Label "antlr4-runtime install debug" -FilePath "cmake" -Arguments @(
        "--install", $layout.BuildDebug,
        "--config", "Debug",
        "--prefix", $layout.StageDebug
    ) -WorkingDirectory $script:ProjectRoot | Out-Null

    $antlrIncludeDest = Join-Path $script:BundleDir "include\antlr4-runtime"
    $antlrBinDest = Join-Path $script:BundleDir "bin"
    $antlrReleaseRoots = @((Join-Path $layout.StageRelease "bin"), (Join-Path $layout.StageRelease "lib"), (Join-Path $layout.BuildRelease "runtime"), $layout.BuildRelease)
    $antlrDebugRoots = @((Join-Path $layout.StageDebug "bin"), (Join-Path $layout.StageDebug "lib"), (Join-Path $layout.BuildDebug "runtime"), $layout.BuildDebug)

    Reset-Directory $antlrIncludeDest
    Ensure-Directory $antlrBinDest
    Copy-DirectoryContent -Source (Join-Path $layout.StageRelease "include\antlr4-runtime") -Destination $antlrIncludeDest
    Copy-MatchingFiles -SearchRoots $antlrReleaseRoots -Patterns @("antlr4-runtime*.dll", "antlr4-runtime*.lib", "antlr4-runtime*.pdb") -Destination (Join-Path $script:BundleDir "lib")
    Copy-MatchingFiles -SearchRoots $antlrDebugRoots -Patterns @("antlr4-runtime*.dll", "antlr4-runtime*.lib", "antlr4-runtime*.pdb") -Destination (Join-Path $script:BundleDir "debug\lib")
    Copy-Item -LiteralPath $antlrToolJar -Destination (Join-Path $antlrBinDest (Get-AntlrToolJarFileName $Dependency)) -Force

    Write-Ok "ANTLR4 runtime and tool jar staged."
}

function Build-LibSsh {
    param([pscustomobject]$Dependency)

    Enter-VsBuildEnvironment
    $sourcePath = Get-SourceTree $Dependency
    if ($DownloadOnly) { return }

    $layout = New-DualBuildLayout "libssh"
    $commonArgs = @(
        "-DBUILD_SHARED_LIBS=ON",
        "-DWITH_EXAMPLES=OFF",
        "-DWITH_SERVER=ON",
        "-DWITH_GSSAPI=OFF",
        "-DUNIT_TESTING=OFF",
        "-DCMAKE_PREFIX_PATH=$script:BundleDir",
        "-DOPENSSL_ROOT_DIR=$script:BundleDir",
        "-DZLIB_INCLUDE_DIR=$([System.IO.Path]::Combine($script:BundleDir, 'include', 'zlib'))",
        "-DZLIB_LIBRARY_RELEASE=$([System.IO.Path]::Combine($script:BundleDir, 'lib', 'zlib.lib'))",
        "-DZLIB_LIBRARY_DEBUG=$([System.IO.Path]::Combine($script:BundleDir, 'debug', 'lib', 'zlibd.lib'))"
    )
    Invoke-CMakeInstallPair -Label "libssh" -SourcePath $sourcePath -Layout $layout -CommonArguments $commonArgs -ReleaseConfig "Release" -DebugConfig "Debug"

    Copy-DirectoryContent -Source (Join-Path $layout.StageRelease "include") -Destination (Join-Path $script:BundleDir "include")
    $libsshIncludeDest = Join-Path $script:BundleDir "include\libssh"
    Copy-MatchingFiles -SearchRoots @((Join-Path $sourcePath "include\libssh")) -Patterns @("*.h", "*.hpp") -Destination $libsshIncludeDest
    Copy-MatchingFiles -SearchRoots @((Join-Path $layout.StageRelease "include\libssh"), $layout.BuildRelease) -Patterns @("libssh_version.h") -Destination $libsshIncludeDest
    Copy-MatchingFiles -SearchRoots @((Join-Path $layout.StageRelease "bin"), (Join-Path $layout.StageRelease "lib")) -Patterns @("ssh*.dll", "ssh*.lib", "ssh*.pdb", "libssh*.dll", "libssh*.lib", "libssh*.pdb") -Destination (Join-Path $script:BundleDir "lib")
    Copy-MatchingFiles -SearchRoots @((Join-Path $layout.StageDebug "bin"), (Join-Path $layout.StageDebug "lib")) -Patterns @("ssh*.dll", "ssh*.lib", "ssh*.pdb", "libssh*.dll", "libssh*.lib", "libssh*.pdb") -Destination (Join-Path $script:BundleDir "debug\lib")

    Write-Ok "libssh release and debug artefacts staged."
}

function Build-Proj {
    param([pscustomobject]$Dependency)

    Enter-VsBuildEnvironment
    $sourcePath = Get-SourceTree $Dependency
    if ($DownloadOnly) { return }

    $layout = New-DualBuildLayout "proj"
    foreach ($path in @($layout.BuildRelease, $layout.BuildDebug, $layout.StageRelease, $layout.StageDebug)) {
        Reset-Directory $path
    }
    Ensure-SqliteBundleOutputs
    $sqliteHeaderRoot = Get-SqliteHeaderRoot
    $sqliteCli = Ensure-SqliteCliShim
    $commonArgs = @(
        "-DBUILD_SHARED_LIBS=ON",
        "-DBUILD_APPS=OFF",
        "-DBUILD_PROJINFO=OFF",
        "-DBUILD_PROJSYNC=OFF",
        "-DBUILD_TESTING=OFF",
        "-DBUILD_CCT=OFF",
        "-DBUILD_CS2CS=OFF",
        "-DBUILD_GEOD=OFF",
        "-DBUILD_GIE=OFF",
        "-DENABLE_TIFF=OFF",
        "-DENABLE_CURL=OFF",
        "-DEXE_SQLITE3=$sqliteCli",
        "-DCMAKE_PREFIX_PATH=$script:BundleDir",
        "-DSQLite3_INCLUDE_DIR=$sqliteHeaderRoot",
        "-DSQLite3_LIBRARY=$([System.IO.Path]::Combine($script:BundleDir, 'lib', 'sqlite3.lib'))",
        "-DSQLite3_LIBRARY_RELEASE=$([System.IO.Path]::Combine($script:BundleDir, 'lib', 'sqlite3.lib'))",
        "-DSQLite3_LIBRARY_DEBUG=$([System.IO.Path]::Combine($script:BundleDir, 'debug', 'lib', 'sqlite3_d.lib'))"
    )

    $configureReleaseArgs = @(
        "-S", $sourcePath,
        "-B", $layout.BuildRelease,
        "-G", $script:ResolvedGenerator, "-T", $script:ResolvedToolset,
        "-A", "x64",
        "-DCMAKE_INSTALL_PREFIX=$($layout.StageRelease)"
    ) + $commonArgs
    Invoke-LoggedCommand -Label "proj configure release" -FilePath "cmake" -Arguments $configureReleaseArgs -WorkingDirectory $script:ProjectRoot | Out-Null
    Invoke-LoggedCommand -Label "proj build release" -FilePath "cmake" -Arguments @(
        "--build", $layout.BuildRelease,
        "--config", "Release",
        "--parallel", $Jobs
    ) -WorkingDirectory $script:ProjectRoot | Out-Null
    Invoke-LoggedCommand -Label "proj install release" -FilePath "cmake" -Arguments @(
        "--install", $layout.BuildRelease,
        "--config", "Release",
        "--prefix", $layout.StageRelease
    ) -WorkingDirectory $script:ProjectRoot | Out-Null

    $configureDebugArgs = @(
        "-S", $sourcePath,
        "-B", $layout.BuildDebug,
        "-G", $script:ResolvedGenerator, "-T", $script:ResolvedToolset,
        "-A", "x64",
        "-DCMAKE_INSTALL_PREFIX=$($layout.StageDebug)"
    ) + $commonArgs
    Invoke-LoggedCommand -Label "proj configure debug" -FilePath "cmake" -Arguments $configureDebugArgs -WorkingDirectory $script:ProjectRoot | Out-Null
    Invoke-LoggedCommand -Label "proj build debug" -FilePath "cmake" -Arguments @(
        "--build", $layout.BuildDebug,
        "--config", "Debug",
        "--parallel", $Jobs
    ) -WorkingDirectory $script:ProjectRoot | Out-Null
    Invoke-LoggedCommand -Label "proj install debug" -FilePath "cmake" -Arguments @(
        "--install", $layout.BuildDebug,
        "--config", "Debug",
        "--prefix", $layout.StageDebug
    ) -WorkingDirectory $script:ProjectRoot | Out-Null

    Copy-DirectoryContent -Source (Join-Path $layout.StageRelease "include") -Destination (Join-Path $script:BundleDir "include")
    Copy-DirectoryContent -Source (Join-Path $layout.StageRelease "share\proj") -Destination (Join-Path $script:BundleDir "share\proj")
    Copy-MatchingFiles -SearchRoots @((Join-Path $layout.StageRelease "bin"), (Join-Path $layout.StageRelease "lib"), (Join-Path $layout.BuildRelease "bin"), (Join-Path $layout.BuildRelease "lib")) -Patterns @("proj*.dll", "proj*.lib", "proj*.pdb") -Destination (Join-Path $script:BundleDir "lib")
    Copy-MatchingFiles -SearchRoots @((Join-Path $layout.StageDebug "bin"), (Join-Path $layout.StageDebug "lib"), (Join-Path $layout.BuildDebug "bin"), (Join-Path $layout.BuildDebug "lib")) -Patterns @("proj*.dll", "proj*.lib", "proj*.pdb") -Destination (Join-Path $script:BundleDir "debug\lib")

    Write-Ok "PROJ release and debug artefacts staged."
}

function Build-Gdal {
    param([pscustomobject]$Dependency)

    Enter-VsBuildEnvironment
    $sourcePath = Get-SourceTree $Dependency
    if ($DownloadOnly) { return }

    $layout = New-DualBuildLayout "gdal"
    foreach ($path in @($layout.BuildRelease, $layout.BuildDebug, $layout.StageRelease, $layout.StageDebug)) {
        Reset-Directory $path
    }
    $commonArgs = @(
        "-DBUILD_SHARED_LIBS=ON",
        "-DBUILD_APPS=ON",
        "-DBUILD_PYTHON_BINDINGS=OFF",
        "-DBUILD_TESTING=OFF",
        "-DGDAL_BUILD_OPTIONAL_DRIVERS=OFF",
        "-DOGR_BUILD_OPTIONAL_DRIVERS=OFF",
        "-DGDAL_USE_CURL=OFF",
        "-DGDAL_USE_SQLITE3=OFF",
        "-DGDAL_USE_LIBXML2=ON",
        "-DGDAL_USE_ZLIB=ON",
        "-DGDAL_USE_PNG=OFF",
        "-DGDAL_USE_JPEG=OFF",
        "-DGDAL_USE_TIFF=OFF",
        "-DGDAL_USE_GEOS=OFF",
        "-DGDAL_USE_ICONV=OFF",
        "-DGDAL_USE_EXPAT=OFF",
        "-DGDAL_USE_PROJ=ON",
        "-DCMAKE_PREFIX_PATH=$script:BundleDir",
        "-DZLIB_INCLUDE_DIR=$([System.IO.Path]::Combine($script:BundleDir, 'include', 'zlib'))",
        "-DZLIB_LIBRARY=$([System.IO.Path]::Combine($script:BundleDir, 'lib', 'zlib.lib'))",
        "-DLIBXML2_INCLUDE_DIR=$([System.IO.Path]::Combine($script:BundleDir, 'include', 'libxml2'))",
        "-DLIBXML2_LIBRARY=$([System.IO.Path]::Combine($script:BundleDir, 'lib', 'libxml2.lib'))",
        "-DPROJ_INCLUDE_DIR=$([System.IO.Path]::Combine($script:BundleDir, 'include'))",
        "-DPROJ_LIBRARY=$([System.IO.Path]::Combine($script:BundleDir, 'lib', 'proj.lib'))"
    )

    $configureReleaseArgs = @(
        "-S", $sourcePath,
        "-B", $layout.BuildRelease,
        "-G", $script:ResolvedGenerator, "-T", $script:ResolvedToolset,
        "-A", "x64",
        "-DCMAKE_INSTALL_PREFIX=$($layout.StageRelease)"
    ) + $commonArgs
    Invoke-LoggedCommand -Label "gdal configure release" -FilePath "cmake" -Arguments $configureReleaseArgs -WorkingDirectory $script:ProjectRoot | Out-Null
    Invoke-LoggedCommand -Label "gdal build release" -FilePath "cmake" -Arguments @(
        "--build", $layout.BuildRelease,
        "--config", "Release",
        "--parallel", $Jobs
    ) -WorkingDirectory $script:ProjectRoot | Out-Null
    Invoke-LoggedCommand -Label "gdal install release" -FilePath "cmake" -Arguments @(
        "--install", $layout.BuildRelease,
        "--config", "Release",
        "--prefix", $layout.StageRelease
    ) -WorkingDirectory $script:ProjectRoot | Out-Null

    $configureDebugArgs = @(
        "-S", $sourcePath,
        "-B", $layout.BuildDebug,
        "-G", $script:ResolvedGenerator, "-T", $script:ResolvedToolset,
        "-A", "x64",
        "-DCMAKE_INSTALL_PREFIX=$($layout.StageDebug)"
    ) + $commonArgs
    Invoke-LoggedCommand -Label "gdal configure debug" -FilePath "cmake" -Arguments $configureDebugArgs -WorkingDirectory $script:ProjectRoot | Out-Null
    Invoke-LoggedCommand -Label "gdal build debug" -FilePath "cmake" -Arguments @(
        "--build", $layout.BuildDebug,
        "--config", "Debug",
        "--parallel", $Jobs
    ) -WorkingDirectory $script:ProjectRoot | Out-Null
    Invoke-LoggedCommand -Label "gdal install debug" -FilePath "cmake" -Arguments @(
        "--install", $layout.BuildDebug,
        "--config", "Debug",
        "--prefix", $layout.StageDebug
    ) -WorkingDirectory $script:ProjectRoot | Out-Null

    $gdalIncludeDest = Join-Path $script:BundleDir "include"
    $gdalHeaderRoots = @(
        (Join-Path $layout.StageRelease "include"),
        (Join-Path $sourcePath "alg"),
        (Join-Path $sourcePath "gcore"),
        (Join-Path $sourcePath "ogr"),
        (Join-Path $sourcePath "ogr\ogrsf_frmts"),
        (Join-Path $sourcePath "port")
    )
    $gdalGeneratedHeaderRoots = @(
        (Join-Path $layout.BuildRelease "gcore"),
        (Join-Path $layout.BuildRelease "port"),
        (Join-Path $layout.BuildDebug "gcore"),
        (Join-Path $layout.BuildDebug "port")
    )

    Copy-DirectoryContent -Source (Join-Path $layout.StageRelease "include") -Destination $gdalIncludeDest
    Copy-MatchingFiles -SearchRoots $gdalHeaderRoots -Patterns @(
        "gdal*.h",
        "ogr*.h",
        "cpl*.h"
    ) -Destination $gdalIncludeDest
    Copy-MatchingFiles -SearchRoots $gdalGeneratedHeaderRoots -Patterns @(
        "gdal_version.h",
        "cpl_config.h"
    ) -Destination $gdalIncludeDest
    Copy-DirectoryContent -Source (Join-Path $layout.StageRelease "share\gdal") -Destination (Join-Path $script:BundleDir "share\gdal")
    Copy-MatchingFiles -SearchRoots @((Join-Path $layout.StageRelease "bin"), (Join-Path $layout.StageRelease "lib"), (Join-Path $layout.BuildRelease "Release"), $layout.BuildRelease) -Patterns @("gdal*.dll", "gdal*.lib", "gdal*.pdb") -Destination (Join-Path $script:BundleDir "lib")
    Copy-MatchingFiles -SearchRoots @((Join-Path $layout.StageDebug "bin"), (Join-Path $layout.StageDebug "lib"), (Join-Path $layout.BuildDebug "Debug"), $layout.BuildDebug) -Patterns @("gdal*.dll", "gdal*.lib", "gdal*.pdb") -Destination (Join-Path $script:BundleDir "debug\lib")
    Copy-MatchingFiles -SearchRoots @((Join-Path $layout.StageRelease "bin"), $layout.BuildRelease) -Patterns @("ogrinfo.exe", "ogr2ogr.exe") -Destination (Join-Path $script:BundleDir "bin")

    Write-Ok "GDAL release and debug artefacts staged."
}

function Build-BoostHeaders {
    param([pscustomobject]$Dependency)

    $sourcePath = Get-SourceTree $Dependency
    if ($DownloadOnly) { return }

    $dest = Join-Path $script:BundleDir "include\boost"
    Reset-Directory $dest

    $headerRoots = @()
    $topLevelBoostDir = Join-Path $sourcePath "boost"
    if (Test-Path -LiteralPath $topLevelBoostDir -PathType Container) {
        $headerRoots += $topLevelBoostDir
    }
    else {
        $headerRoots = @(
            Get-ChildItem -LiteralPath (Join-Path $sourcePath "libs") -Directory -Recurse -ErrorAction SilentlyContinue |
                ForEach-Object { Join-Path $_.FullName "include\boost" } |
                Where-Object { Test-Path -LiteralPath $_ -PathType Container }
        )
    }

    $headerRoots = @($headerRoots | Sort-Object -Unique)

    if (-not $headerRoots -or $headerRoots.Count -eq 0) {
        throw "Boost headers were not found under $sourcePath"
    }

    foreach ($headerRoot in $headerRoots) {
        Copy-DirectoryContent -Source $headerRoot -Destination $dest
    }

    Write-Ok "Boost headers staged."
}

function Build-RapidJsonHeaders {
    param([pscustomobject]$Dependency)

    $sourcePath = Get-SourceTree $Dependency
    if ($DownloadOnly) { return }

    $rapidDir = Join-Path $sourcePath "include\rapidjson"
    if (-not (Test-Path -LiteralPath $rapidDir -PathType Container)) {
        throw "RapidJSON headers were not found under $rapidDir"
    }

    $dest = Join-Path $script:BundleDir "include\rapidjson"
    Reset-Directory $dest
    Copy-DirectoryContent -Source $rapidDir -Destination $dest
    Write-Ok "RapidJSON headers staged."
}

function Build-GoogleTest {
    param([pscustomobject]$Dependency)

    Enter-VsBuildEnvironment
    $sourcePath = Get-SourceTree $Dependency
    if ($DownloadOnly) { return }

    $layout = New-DualBuildLayout "googletest"
    foreach ($path in @($layout.StageRelease, $layout.StageDebug)) {
        Reset-Directory $path
    }

    $commonArgs = @(
        "-DBUILD_GMOCK=ON",
        "-DINSTALL_GTEST=ON",
        "-Dgtest_force_shared_crt=ON",
        "-DBUILD_SHARED_LIBS=OFF"
    )

    Invoke-CMakeInstallPair -Label "googletest" -SourcePath $sourcePath -Layout $layout -CommonArguments $commonArgs

    $includeDest = Join-Path $script:BundleDir "include\googletest"
    Reset-Directory $includeDest
    Copy-DirectoryContent -Source (Join-Path $layout.StageRelease "include") -Destination $includeDest

    Copy-MatchingFiles -SearchRoots @((Join-Path $layout.StageRelease "lib"), $layout.BuildRelease) -Patterns @(
        "gtest.lib",
        "gmock.lib",
        "gtest_main.lib",
        "gmock_main.lib"
    ) -Destination (Join-Path $script:BundleDir "lib")
    Copy-MatchingFiles -SearchRoots @((Join-Path $layout.StageDebug "lib"), $layout.BuildDebug) -Patterns @(
        "gtest.lib",
        "gmock.lib",
        "gtest_main.lib",
        "gmock_main.lib"
    ) -Destination (Join-Path $script:BundleDir "debug\lib")

    Write-Ok "GoogleTest and Google Mock release and debug libraries staged."
}

function Build-HtmlRenderer {
    param([pscustomobject]$Dependency)

    Enter-VsBuildEnvironment
    $sourcePath = Get-SourceTree $Dependency
    if ($DownloadOnly) { return }

    $projectFile = Join-Path $sourcePath "Source\HtmlRenderer\HtmlRenderer.csproj"
    if (-not (Test-Path -LiteralPath $projectFile -PathType Leaf)) {
        throw "HTML Renderer project file was not found: $projectFile"
    }

    $msbuildExe = Get-Command "MSBuild.exe" -ErrorAction SilentlyContinue
    if ($msbuildExe) {
        $msbuildPath = $msbuildExe.Source
    }
    else {
        $vsPath = Get-VSInstallPath
        $msbuildPath = Join-Path $vsPath "MSBuild\Current\Bin\MSBuild.exe"
        if (-not (Test-Path -LiteralPath $msbuildPath -PathType Leaf)) {
            throw "MSBuild.exe was not found under $vsPath"
        }
    }

    Invoke-LoggedCommand -Label "htmlrenderer build release" -FilePath $msbuildPath -WorkingDirectory $sourcePath -Arguments @(
        $projectFile,
        "/p:Configuration=Release",
        "/p:Platform=AnyCPU",
        "/p:TargetFrameworkVersion=v4.8",
        "/t:Rebuild"
    ) | Out-Null

    Invoke-LoggedCommand -Label "htmlrenderer build debug" -FilePath $msbuildPath -WorkingDirectory $sourcePath -Arguments @(
        $projectFile,
        "/p:Configuration=Debug",
        "/p:Platform=AnyCPU",
        "/p:TargetFrameworkVersion=v4.8",
        "/t:Rebuild"
    ) | Out-Null

    $releaseOutput = Join-Path $sourcePath "Source\HtmlRenderer\bin\ReleaseNet20"
    $debugOutput = Join-Path $sourcePath "Source\HtmlRenderer\bin\DebugNet20"
    if (-not (Test-Path -LiteralPath $releaseOutput -PathType Container)) {
        throw "HTML Renderer release output directory was not produced: $releaseOutput"
    }
    if (-not (Test-Path -LiteralPath $debugOutput -PathType Container)) {
        throw "HTML Renderer debug output directory was not produced: $debugOutput"
    }

    Copy-MatchingFiles -SearchRoots @($releaseOutput) -Patterns @("HtmlRenderer.dll", "HtmlRenderer.pdb", "HtmlRenderer.xml") -Destination (Join-Path $script:BundleDir "lib")
    Copy-MatchingFiles -SearchRoots @($debugOutput) -Patterns @("HtmlRenderer.dll", "HtmlRenderer.pdb", "HtmlRenderer.xml") -Destination (Join-Path $script:BundleDir "debug\lib")

    Write-Ok "HTML Renderer release and debug artefacts staged."
}

function Build-Sqlite {
    param([pscustomobject]$Dependency)

    Enter-VsBuildEnvironment
    $sourcePath = Get-SourceTree $Dependency
    if ($DownloadOnly) { return }

    $buildRel = Join-Path $script:BuildRoot "sqlite\release"
    $buildDbg = Join-Path $script:BuildRoot "sqlite\debug"
    Ensure-Directory $buildRel
    Ensure-Directory $buildDbg

    $sqliteC = Join-Path $sourcePath "sqlite3.c"
    $sqliteH = Join-Path $sourcePath "sqlite3.h"
    $sqliteExtH = Join-Path $sourcePath "sqlite3ext.h"
    if (-not (Test-Path -LiteralPath $sqliteC -PathType Leaf)) {
        throw "sqlite3.c was not found in $sourcePath"
    }

    $commonDefines = @(
        "/DSQLITE_ENABLE_COLUMN_METADATA",
        "/DSQLITE_ENABLE_FTS5",
        "/DSQLITE_ENABLE_JSON1",
        "/DSQLITE_ENABLE_RTREE",
        "/DSQLITE_ENABLE_UNLOCK_NOTIFY",
        "/DSQLITE_THREADSAFE=1",
        "/DSQLITE_API=__declspec(dllexport)"
    )

    $sqliteReleaseArgs = @(
        "/nologo", "/O2", "/MD", "/LD", "/Zi"
    ) + $commonDefines + @(
        $sqliteC,
        "/link", "/NOLOGO",
        "/OUT:sqlite3.dll",
        "/IMPLIB:sqlite3.lib",
        "/PDB:sqlite3.pdb"
    )
    Invoke-LoggedCommand -Label "sqlite release build" -FilePath "cl.exe" -WorkingDirectory $buildRel -Arguments $sqliteReleaseArgs | Out-Null

    $sqliteDebugArgs = @(
        "/nologo", "/Od", "/MDd", "/LD", "/Zi", "/DSQLITE_DEBUG"
    ) + $commonDefines + @(
        $sqliteC,
        "/link", "/NOLOGO",
        "/OUT:sqlite3_d.dll",
        "/IMPLIB:sqlite3_d.lib",
        "/PDB:sqlite3_d.pdb"
    )
    Invoke-LoggedCommand -Label "sqlite debug build" -FilePath "cl.exe" -WorkingDirectory $buildDbg -Arguments $sqliteDebugArgs | Out-Null

    Ensure-Directory (Join-Path $script:BundleDir "include\sqlite")
    Copy-Item -LiteralPath $sqliteH -Destination (Join-Path $script:BundleDir "include\sqlite\sqlite3.h") -Force
    if (Test-Path -LiteralPath $sqliteExtH -PathType Leaf) {
        Copy-Item -LiteralPath $sqliteExtH -Destination (Join-Path $script:BundleDir "include\sqlite\sqlite3ext.h") -Force
    }

    foreach ($item in @("sqlite3.dll", "sqlite3.lib", "sqlite3.pdb")) {
        Copy-Item -LiteralPath (Join-Path $buildRel $item) -Destination (Join-Path $script:BundleDir "lib\$item") -Force
    }
    foreach ($item in @("sqlite3_d.dll", "sqlite3_d.lib", "sqlite3_d.pdb")) {
        Copy-Item -LiteralPath (Join-Path $buildDbg $item) -Destination (Join-Path $script:BundleDir "debug\lib\$item") -Force
    }

    Write-Ok "SQLite release and debug artefacts staged."
}

function Build-Vsqlitepp {
    param([pscustomobject]$Dependency)

    Enter-VsBuildEnvironment
    $sourcePath = Get-SourceTree $Dependency
    if ($DownloadOnly) { return }

    $headerSourceRoot = Join-Path $sourcePath "include\sqlite"
    if (-not (Test-Path -LiteralPath $headerSourceRoot -PathType Container)) {
        throw "vsqlitepp headers were not found under $headerSourceRoot"
    }

    $sourceRoot = Join-Path $sourcePath "src\sqlite"
    $sourceCandidates = @(
        Get-ChildItem -LiteralPath $sourceRoot -File -ErrorAction SilentlyContinue |
            Where-Object { $_.Extension -in @(".cpp", ".cc", ".cxx") }
    )

    if (-not $sourceCandidates) {
        throw "No vsqlitepp implementation source files were found under $sourceRoot"
    }

    $headerDest = Join-Path $script:BundleDir "include\sqlite"
    Ensure-Directory $headerDest
    Copy-DirectoryContent -Source $headerSourceRoot -Destination $headerDest

    $sqliteHeaderRoot = Get-SqliteHeaderRoot
    foreach ($sqliteHeaderName in @("sqlite3.h", "sqlite3ext.h")) {
        $sqliteHeaderPath = Join-Path $sqliteHeaderRoot $sqliteHeaderName
        $sqliteHeaderDestination = Join-Path $headerDest $sqliteHeaderName
        if (Test-Path -LiteralPath $sqliteHeaderPath -PathType Leaf) {
            $sourceResolved = [System.IO.Path]::GetFullPath($sqliteHeaderPath)
            $destinationResolved = [System.IO.Path]::GetFullPath($sqliteHeaderDestination)
            if ($sourceResolved -ne $destinationResolved) {
                Copy-Item -LiteralPath $sqliteHeaderPath -Destination $sqliteHeaderDestination -Force
            }
        }
    }

    $buildRel = Join-Path $script:BuildRoot "vsqlitepp\release"
    $buildDbg = Join-Path $script:BuildRoot "vsqlitepp\debug"
    Reset-Directory $buildRel
    Reset-Directory $buildDbg

    $includeArgs = @(
        ('/I"{0}"' -f (Join-Path $sourcePath "include"))
        ('/I"{0}"' -f (Join-Path $sourcePath "include\sqlite"))
        ('/I"{0}"' -f (Join-Path $sourcePath "include\sqlite\private"))
        ('/I"{0}"' -f (Join-Path $script:BundleDir "include"))
        ('/I"{0}"' -f (Join-Path $script:BundleDir "include\sqlite"))
    )

    $releaseObjects = @()
    foreach ($source in $sourceCandidates) {
        $objectPath = Join-Path $buildRel ($source.BaseName + ".obj")
        $responsePath = Join-Path $buildRel ($source.BaseName + ".rsp")
        $vsqliteReleaseCompileArgs = @(
            "/nologo", "/O2", "/MD", "/EHsc", "/Zi", "/c",
            ('/Fo"{0}"' -f $objectPath)
        ) + $includeArgs + @(('"{0}"' -f $source.FullName))
        Set-Content -LiteralPath $responsePath -Value ($vsqliteReleaseCompileArgs -join [Environment]::NewLine) -Encoding ascii
        Invoke-LoggedCommand -Label ("vsqlitepp release compile {0}" -f $source.Name) -FilePath "cl.exe" -WorkingDirectory $buildRel -Arguments @("@$responsePath") | Out-Null
        $releaseObjects += $objectPath
    }
    if ($releaseObjects.Count -ne $sourceCandidates.Count) {
        throw "vsqlitepp release compile did not produce the expected number of object files."
    }
    $releaseLibResponsePath = Join-Path $buildRel "vsqlitepp.rsp"
    $vsqliteReleaseLibArgs = @('/NOLOGO', '/OUT:"vsqlitepp.lib"') + @($releaseObjects | ForEach-Object { ('"{0}"' -f $_) })
    Set-Content -LiteralPath $releaseLibResponsePath -Value ($vsqliteReleaseLibArgs -join [Environment]::NewLine) -Encoding ascii
    Invoke-LoggedCommand -Label "vsqlitepp release lib" -FilePath "lib.exe" -WorkingDirectory $buildRel -Arguments @("@$releaseLibResponsePath") | Out-Null

    $debugObjects = @()
    foreach ($source in $sourceCandidates) {
        $objectPath = Join-Path $buildDbg ($source.BaseName + ".obj")
        $responsePath = Join-Path $buildDbg ($source.BaseName + ".rsp")
        $vsqliteDebugCompileArgs = @(
            "/nologo", "/Od", "/MDd", "/EHsc", "/Zi", "/c",
            ('/Fo"{0}"' -f $objectPath)
        ) + $includeArgs + @(('"{0}"' -f $source.FullName))
        Set-Content -LiteralPath $responsePath -Value ($vsqliteDebugCompileArgs -join [Environment]::NewLine) -Encoding ascii
        Invoke-LoggedCommand -Label ("vsqlitepp debug compile {0}" -f $source.Name) -FilePath "cl.exe" -WorkingDirectory $buildDbg -Arguments @("@$responsePath") | Out-Null
        $debugObjects += $objectPath
    }
    if ($debugObjects.Count -ne $sourceCandidates.Count) {
        throw "vsqlitepp debug compile did not produce the expected number of object files."
    }
    $debugLibResponsePath = Join-Path $buildDbg "vsqlitepp.rsp"
    $vsqliteDebugLibArgs = @('/NOLOGO', '/OUT:"vsqlitepp.lib"') + @($debugObjects | ForEach-Object { ('"{0}"' -f $_) })
    Set-Content -LiteralPath $debugLibResponsePath -Value ($vsqliteDebugLibArgs -join [Environment]::NewLine) -Encoding ascii
    Invoke-LoggedCommand -Label "vsqlitepp debug lib" -FilePath "lib.exe" -WorkingDirectory $buildDbg -Arguments @("@$debugLibResponsePath") | Out-Null

    Copy-Item -LiteralPath (Join-Path $buildRel "vsqlitepp.lib") -Destination (Join-Path $script:BundleDir "lib\vsqlitepp.lib") -Force
    Copy-Item -LiteralPath (Join-Path $buildDbg "vsqlitepp.lib") -Destination (Join-Path $script:BundleDir "debug\lib\vsqlitepp.lib") -Force

    Write-Ok "vsqlitepp headers and libraries staged."
}

function Build-Python {
    param([pscustomobject]$Dependency)

    Enter-VsBuildEnvironment
    $sourcePath = Get-SourceTree $Dependency
    if ($DownloadOnly) { return }

    $pcBuild = Join-Path $sourcePath "PCbuild"
    if (-not (Test-Path -LiteralPath $pcBuild -PathType Container)) {
        throw "Python PCbuild directory not found: $pcBuild"
    }

    Ensure-PythonSourceArchiveCompatibilityPatch -SourcePath $sourcePath
    $pythonPlatformToolset = $script:ResolvedToolset
    Set-Content -LiteralPath (Join-Path $pcBuild "MSBuild.rsp") -Value ("/p:PlatformToolset={0}" -f $pythonPlatformToolset)

    $msbuildExe = Get-Command "MSBuild.exe" -ErrorAction SilentlyContinue
    if ($msbuildExe) {
        $msbuildPath = $msbuildExe.Source
    }
    else {
        $vsPath = Get-VSInstallPath
        $msbuildPath = Join-Path $vsPath "MSBuild\Current\Bin\MSBuild.exe"
        if (-not (Test-Path -LiteralPath $msbuildPath -PathType Leaf)) {
            throw "MSBuild.exe was not found under $vsPath"
        }
    }

    Invoke-LoggedCommand -Label "python release build" -FilePath (Join-Path $pcBuild "build.bat") -Arguments @("-c", "Release", "-p", "x64", "-t", "Build") -WorkingDirectory $pcBuild | Out-Null
    Invoke-LoggedCommand -Label "python debug build" -FilePath (Join-Path $pcBuild "build.bat") -Arguments @("-c", "Debug", "-p", "x64", "-t", "Build") -WorkingDirectory $pcBuild | Out-Null

    $amd64Dir = Join-Path $pcBuild "amd64"
    $pythonExecutableBuilds = @(
        [pscustomobject]@{ Label = "python release executable"; Project = "python.vcxproj"; Configuration = "Release"; Expected = "python.exe" },
        [pscustomobject]@{ Label = "python debug executable"; Project = "python.vcxproj"; Configuration = "Debug"; Expected = "python_d.exe" },
        [pscustomobject]@{ Label = "pythonw release executable"; Project = "pythonw.vcxproj"; Configuration = "Release"; Expected = "pythonw.exe" },
        [pscustomobject]@{ Label = "pythonw debug executable"; Project = "pythonw.vcxproj"; Configuration = "Debug"; Expected = "pythonw_d.exe" }
    )
    foreach ($pythonExecutableBuild in $pythonExecutableBuilds) {
        $expectedExecutable = Join-Path $amd64Dir $pythonExecutableBuild.Expected
        if (Test-Path -LiteralPath $expectedExecutable -PathType Leaf) {
            continue
        }

        Invoke-LoggedCommand -Label $pythonExecutableBuild.Label -FilePath $msbuildPath -Arguments @(
            (Join-Path $pcBuild $pythonExecutableBuild.Project),
            "/t:Build",
            "/p:Configuration=$($pythonExecutableBuild.Configuration)",
            "/p:Platform=x64",
            "/p:PlatformToolset=$pythonPlatformToolset",
            "/v:m",
            "/nologo"
        ) -WorkingDirectory $pcBuild | Out-Null

        if (-not (Test-Path -LiteralPath $expectedExecutable -PathType Leaf)) {
            throw "Python executable was not produced: $expectedExecutable"
        }
    }

    $pythonDest = Join-Path $script:BundleDir "python"
    Ensure-Directory $pythonDest
    Ensure-Directory (Join-Path $pythonDest "libs")
    Ensure-Directory (Join-Path $pythonDest "DLLs")
    Ensure-Directory (Join-Path $pythonDest "Include")

    Copy-Item -Path (Join-Path $amd64Dir "*.dll") -Destination $pythonDest -Force
    Copy-Item -Path (Join-Path $amd64Dir "*.exe") -Destination $pythonDest -Force
    Copy-Item -Path (Join-Path $amd64Dir "*.pdb") -Destination $pythonDest -Force -ErrorAction SilentlyContinue
    Copy-Item -Path (Join-Path $amd64Dir "*.lib") -Destination (Join-Path $pythonDest "libs") -Force
    Copy-Item -Path (Join-Path $amd64Dir "*.pyd") -Destination (Join-Path $pythonDest "DLLs") -Force

    Copy-DirectoryContent -Source (Join-Path $sourcePath "Lib") -Destination (Join-Path $pythonDest "Lib")
    Copy-DirectoryContent -Source (Join-Path $sourcePath "Tools") -Destination (Join-Path $pythonDest "Tools")
    Copy-DirectoryContent -Source (Join-Path $sourcePath "Include") -Destination (Join-Path $pythonDest "Include")

    $pyConfig = Join-Path $sourcePath "PC\pyconfig.h"
    if (Test-Path -LiteralPath $pyConfig -PathType Leaf) {
        Copy-Item -LiteralPath $pyConfig -Destination (Join-Path $pythonDest "Include\pyconfig.h") -Force
    }

    Write-Ok "Python bundle staged."
}

function Ensure-WinBison {
    $existing = Get-Command "win_bison.exe" -ErrorAction SilentlyContinue
    if ($existing) {
        return $existing.Source
    }

    $existing = Get-Command "win_bison" -ErrorAction SilentlyContinue
    if ($existing) {
        return $existing.Source
    }

    $toolVersion = "2.5.25"
    $toolDir = Join-Path $script:ToolsRoot "winflexbison-$toolVersion"
    $toolExe = Join-Path $toolDir "win_bison.exe"
    if (Test-Path -LiteralPath $toolExe -PathType Leaf) {
        return $toolExe
    }

    $archive = Join-Path $script:DownloadRoot "winflexbison-$toolVersion.zip"
    Download-File -Sources @("https://github.com/lexxmark/winflexbison/releases/download/v$toolVersion/win_flex_bison-$toolVersion.zip") -Destination $archive
    Expand-ArchiveSmart -ArchivePath $archive -Destination $toolDir

    if (-not (Test-Path -LiteralPath $toolExe -PathType Leaf)) {
        throw "win_bison.exe was not found after extracting $archive"
    }

    return $toolExe
}

function Build-MySQLServer {
    param([pscustomobject]$Dependency)

    Enter-VsBuildEnvironment
    $sourcePath = Get-SourceTree $Dependency
    if ($DownloadOnly) { return }

    $bisonExe = Ensure-WinBison
    $buildRel = Join-Path $script:BuildRoot "mysql\release"
    $buildDbg = Join-Path $script:BuildRoot "mysql\debug"
    $stageRel = Join-Path $script:BuildRoot "mysql\stage-release"
    $stageDbg = Join-Path $script:BuildRoot "mysql\stage-debug"
    foreach ($path in @($buildRel, $buildDbg, $stageRel, $stageDbg)) {
        Reset-Directory $path
    }

    $boostDownloadDir = Join-Path $script:ToolsRoot "mysql-boost"
    Ensure-Directory $boostDownloadDir

    $commonArgs = @(
        "-S", $sourcePath,
        "-G", $script:ResolvedGenerator, "-T", $script:ResolvedToolset,
        "-A", "x64",
        "-DBISON_EXECUTABLE=$bisonExe",
        "-DDOWNLOAD_BOOST=1",
        "-DWITH_BOOST=$boostDownloadDir",
        "-DWITH_ROUTER=OFF",
        "-DWITH_UNIT_TESTS=OFF",
        "-DWITH_MEB=OFF",
        "-DWITH_INNODB_MEMCACHED=OFF",
        "-DWITH_AUTHENTICATION_CLIENT_PLUGINS=ON",
        "-DCMAKE_PREFIX_PATH=$script:BundleDir",
        "-DWITH_SSL=$script:BundleDir"
    )

    $mysqlConfigureReleaseArgs = @("-B", $buildRel) + $commonArgs + @("-DCMAKE_INSTALL_PREFIX=$stageRel")
    Invoke-LoggedCommand -Label "mysql configure release" -FilePath "cmake" -WorkingDirectory $buildRel -Arguments $mysqlConfigureReleaseArgs | Out-Null
    Invoke-WithPathPrefix -Prefixes @(
        (Join-Path $script:BundleDir "lib"),
        (Join-Path $script:BundleDir "bin")
    ) -Action {
        Invoke-LoggedCommand -Label "mysql build release" -FilePath "cmake" -WorkingDirectory $buildRel -Arguments @("--build", $buildRel, "--target", "mysqlclient", "libmysql", "mysql", "mysqldump", "INFO_BIN", "INFO_SRC", "--config", "RelWithDebInfo", "--parallel", $Jobs) | Out-Null
    }

    $mysqlConfigureDebugArgs = @("-B", $buildDbg) + $commonArgs + @("-DCMAKE_INSTALL_PREFIX=$stageDbg")
    Invoke-LoggedCommand -Label "mysql configure debug" -FilePath "cmake" -WorkingDirectory $buildDbg -Arguments $mysqlConfigureDebugArgs | Out-Null
    Invoke-WithPathPrefix -Prefixes @(
        (Join-Path $script:BundleDir "debug\lib"),
        (Join-Path $script:BundleDir "lib"),
        (Join-Path $script:BundleDir "bin")
    ) -Action {
        Invoke-LoggedCommand -Label "mysql build debug" -FilePath "cmake" -WorkingDirectory $buildDbg -Arguments @("--build", $buildDbg, "--target", "mysqlclient", "libmysql", "mysql", "mysqldump", "INFO_BIN", "INFO_SRC", "--config", "Debug", "--parallel", $Jobs) | Out-Null
    }

    $mysqlIncludeDir = Join-Path $script:BundleDir "include\mysql"
    $mysqlNestedIncludeDir = Join-Path $mysqlIncludeDir "mysql"
    $mysqlHeaderPatterns = @("mysql.h", "mysql_com.h", "mysql_version.h", "mysqld_error.h", "field_types.h", "mysql_time.h", "mysqlx_*.h", "errmsg.h")
    Ensure-Directory $mysqlIncludeDir
    Copy-DirectoryContent -Source (Join-Path $stageRel "include\mysql") -Destination $mysqlIncludeDir
    Copy-DirectoryContent -Source (Join-Path $sourcePath "include\mysql") -Destination $mysqlNestedIncludeDir
    Copy-DirectoryContent -Source (Join-Path $stageRel "include\mysql\mysql") -Destination $mysqlNestedIncludeDir
    foreach ($root in @($stageRel, $sourcePath, $buildRel)) {
        Copy-FirstMatch -SearchRoot $root -Patterns $mysqlHeaderPatterns -Destination $mysqlIncludeDir -AllowMany | Out-Null
    }
    Copy-FirstMatch -SearchRoot $stageRel -Patterns $mysqlHeaderPatterns -Destination (Join-Path $script:BundleDir "include") -AllowMany | Out-Null
    Copy-FirstMatch -SearchRoot $sourcePath -Patterns $mysqlHeaderPatterns -Destination (Join-Path $script:BundleDir "include") -AllowMany | Out-Null
    Copy-FirstMatch -SearchRoot $buildRel -Patterns @("mysql_version.h", "mysqld_error.h", "errmsg.h") -Destination (Join-Path $script:BundleDir "include") -AllowMany | Out-Null
    # mysql.h and mysql_com.h include these; they are part of the client
    # headers a CMake install would provide.
    foreach ($header in @("my_command.h", "my_compress.h", "my_list.h")) {
        foreach ($destination in @($mysqlIncludeDir, (Join-Path $script:BundleDir "include"))) {
            Copy-Item -LiteralPath (Join-Path $sourcePath "include\$header") -Destination (Join-Path $destination $header) -Force
        }
    }

    $releaseSearchRoots = @($stageRel, (Join-Path $buildRel "library_output_directory"), (Join-Path $buildRel "archive_output_directory"), (Join-Path $buildRel "runtime_output_directory"), $buildRel)
    $debugSearchRoots = @($stageDbg, (Join-Path $buildDbg "library_output_directory"), (Join-Path $buildDbg "archive_output_directory"), (Join-Path $buildDbg "runtime_output_directory"), $buildDbg)
    foreach ($root in $releaseSearchRoots) {
        Copy-FirstMatch -SearchRoot $root -Patterns @("mysqlclient.lib", "libmysql.dll", "libmysql.lib", "libmysql.pdb") -Destination (Join-Path $script:BundleDir "lib") -AllowMany | Out-Null
    }
    foreach ($root in $debugSearchRoots) {
        Copy-FirstMatch -SearchRoot $root -Patterns @("mysqlclient.lib", "libmysql.dll", "libmysql.lib", "libmysql.pdb") -Destination (Join-Path $script:BundleDir "debug\lib") -AllowMany | Out-Null
    }
    foreach ($root in $releaseSearchRoots) {
        Copy-FirstMatch -SearchRoot $root -Patterns @("mysql.exe", "mysqldump.exe") -Destination (Join-Path $script:BundleDir "bin") -AllowMany | Out-Null
        Copy-FirstMatch -SearchRoot $root -Patterns @("authentication_*.dll", "mysql_native_password.dll") -Destination (Join-Path $script:BundleDir "lib") -AllowMany | Out-Null
    }

    Write-Ok "MySQL client bundle staged."
}

function Build-ConnectorCpp {
    param([pscustomobject]$Dependency)

    Enter-VsBuildEnvironment
    $sourcePath = Get-SourceTree $Dependency
    if ($DownloadOnly) { return }

    $buildRel = Join-Path $script:BuildRoot "connectorcpp\release"
    $buildDbg = Join-Path $script:BuildRoot "connectorcpp\debug"
    $stageRel = Join-Path $script:BuildRoot "connectorcpp\stage-release"
    $stageDbg = Join-Path $script:BuildRoot "connectorcpp\stage-debug"
    $mysqlHeaders = Join-Path $script:BuildRoot "connectorcpp\mysql-include"
    $jdbcShim = Join-Path $script:BuildRoot "connectorcpp\jdbc-standalone-shim.cmake"
    foreach ($path in @($buildRel, $buildDbg, $stageRel, $stageDbg)) {
        Reset-Directory $path
    }
    Reset-Directory $mysqlHeaders

    $mysqlSourceRoot = Get-ChildItem -LiteralPath $script:SourceRoot -Directory -Filter "mysql-server-*" -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending |
        Select-Object -First 1
    if (-not $mysqlSourceRoot) {
        throw "mysql-server sources are required before building mysql-connector-cpp."
    }

    $mysqlSourceInclude = Join-Path $mysqlSourceRoot.FullName "include"
    if (-not (Test-Path -LiteralPath $mysqlSourceInclude -PathType Container)) {
        throw "MySQL source include directory not found: $mysqlSourceInclude"
    }

    Copy-DirectoryContent -Source $mysqlSourceInclude -Destination $mysqlHeaders
    Copy-DirectoryContent -Source (Join-Path $script:BundleDir "include\mysql") -Destination $mysqlHeaders
    Set-Content -LiteralPath $jdbcShim -Value @(
        "function(add_version_info)"
        "endfunction()"
    )

    $commonArgs = @(
        "-S", (Join-Path $sourcePath "jdbc"),
        "-G", $script:ResolvedGenerator, "-T", $script:ResolvedToolset,
        "-A", "x64",
        "-DWITH_TESTS=OFF",
        "-DMYSQL_INCLUDE_DIR=$mysqlHeaders",
        "-DMYSQL_LIB_DIR=$(Join-Path $script:BundleDir 'lib')",
        "-DOPENSSL_ROOT_DIR=$script:BundleDir",
        "-DINSTALL_INCLUDE_DIR=include",
        "-DINSTALL_LIB_DIR=lib",
        "-DINSTALL_LIB_DIR_DEBUG=debug/lib",
        "-DINSTALL_LIB_DIR_STATIC=lib",
        "-DINSTALL_LIB_DIR_STATIC_DEBUG=debug/lib",
        "-DLIB_NAME_BASE=mysqlcppconn",
        "-DLIB_NAME=mysqlcppconn-10-vs14",
        "-DJDBC_ABI_VERSION_MAJOR=10",
        "-DVS=vs14",
        "-DCMAKE_PROJECT_INCLUDE_BEFORE=$jdbcShim",
        "-DCMAKE_INSTALL_MESSAGE=LAZY"
    )

    $connectorCppConfigureReleaseArgs = @("-B", $buildRel) + $commonArgs + @("-DCMAKE_INSTALL_PREFIX=$stageRel")
    Invoke-LoggedCommand -Label "connectorcpp configure release" -FilePath "cmake" -WorkingDirectory $buildRel -Arguments $connectorCppConfigureReleaseArgs | Out-Null
    Invoke-WithPathPrefix -Prefixes @(
        (Join-Path $script:BundleDir "lib"),
        (Join-Path $script:BundleDir "bin")
    ) -Action {
        Invoke-LoggedCommand -Label "connectorcpp build release" -FilePath "cmake" -WorkingDirectory $buildRel -Arguments @("--build", $buildRel, "--config", "RelWithDebInfo", "--parallel", $Jobs) | Out-Null
        Invoke-LoggedCommand -Label "connectorcpp install release" -FilePath "cmake" -WorkingDirectory $buildRel -Arguments @("--install", $buildRel, "--config", "RelWithDebInfo", "--prefix", $stageRel) | Out-Null
    }

    $connectorCppConfigureDebugArgs = @("-B", $buildDbg) + $commonArgs + @("-DCMAKE_INSTALL_PREFIX=$stageDbg")
    Invoke-LoggedCommand -Label "connectorcpp configure debug" -FilePath "cmake" -WorkingDirectory $buildDbg -Arguments $connectorCppConfigureDebugArgs | Out-Null
    Invoke-WithPathPrefix -Prefixes @(
        (Join-Path $script:BundleDir "debug\lib"),
        (Join-Path $script:BundleDir "lib"),
        (Join-Path $script:BundleDir "bin")
    ) -Action {
        Invoke-LoggedCommand -Label "connectorcpp build debug" -FilePath "cmake" -WorkingDirectory $buildDbg -Arguments @("--build", $buildDbg, "--config", "Debug", "--parallel", $Jobs) | Out-Null
        Invoke-LoggedCommand -Label "connectorcpp install debug" -FilePath "cmake" -WorkingDirectory $buildDbg -Arguments @("--install", $buildDbg, "--config", "Debug", "--prefix", $stageDbg) | Out-Null
    }

    Ensure-Directory (Join-Path $script:BundleDir "include\cppconn")
    foreach ($sourceDir in @(
        (Join-Path $stageRel "include\cppconn"),
        (Join-Path $buildRel "include\jdbc\cppconn"),
        (Join-Path $sourcePath "jdbc\cppconn")
    )) {
        Copy-DirectoryContent -Source $sourceDir -Destination (Join-Path $script:BundleDir "include\cppconn")
    }
    Ensure-Directory (Join-Path $script:BundleDir "include\mysql")
    if (Test-Path -LiteralPath (Join-Path $sourcePath "include\mysql\jdbc.h") -PathType Leaf) {
        Copy-Item -LiteralPath (Join-Path $sourcePath "include\mysql\jdbc.h") -Destination (Join-Path $script:BundleDir "include\mysql\jdbc.h") -Force
    }
    foreach ($root in @($stageRel, $buildRel, $sourcePath)) {
        Copy-FirstMatch -SearchRoot $root -Patterns @("mysql_connection.h", "mysql_driver.h", "mysql_error.h", "jdbc.h") -Destination (Join-Path $script:BundleDir "include") -AllowMany | Out-Null
    }

    foreach ($root in @($stageRel, $buildRel)) {
        Copy-FirstMatch -SearchRoot $root -Patterns @("mysqlcppconn*.lib", "mysqlcppconn*.dll", "mysqlcppconn*.pdb") -Destination (Join-Path $script:BundleDir "lib") -AllowMany | Out-Null
    }
    foreach ($root in @($stageDbg, $buildDbg)) {
        Copy-FirstMatch -SearchRoot $root -Patterns @("mysqlcppconn*.lib", "mysqlcppconn*.dll", "mysqlcppconn*.pdb") -Destination (Join-Path $script:BundleDir "debug\lib") -AllowMany | Out-Null
    }

    Write-Ok "Connector/C++ bundle staged."
}

function Get-SelectedDependencies {
    if (-not $script:DepManifest -or $script:DepManifest.Count -eq 0) {
        $script:DepManifest = @(Load-DependencyManifest)
    }

    if (-not $script:SelectedOnly -or $script:SelectedOnly.Count -eq 0) {
        return $script:DepManifest
    }

    $selected = @($script:DepManifest | Where-Object { $script:SelectedOnly -contains $_.Name })
    $missing = @($script:SelectedOnly | Where-Object { $_ -notin $selected.Name })
    if ($missing) {
        throw ("Unknown dependency name(s): {0}" -f ($missing -join ", "))
    }

    return $selected
}

function Test-DependencyArchiveNeedsTar {
    param([pscustomobject]$Dependency)

    $sources = @()
    if ($Dependency.PSObject.Properties["Urls"]) {
        $sources = @($Dependency.Urls)
    }
    elseif ($Dependency.PSObject.Properties["Url"]) {
        $sources = @($Dependency.Url)
    }

    foreach ($source in $sources) {
        if ([string]::IsNullOrWhiteSpace($source)) {
            continue
        }

        $candidate = $source
        if ($candidate -match '^[a-zA-Z][a-zA-Z0-9+.-]*://') {
            try {
                $candidate = ([Uri]$candidate).AbsolutePath
            }
            catch {
                $candidate = $source
            }
        }

        if ($candidate -match '\.(tar\.gz|tgz|tar\.xz|txz|tar\.bz2|tbz2|tar)$') {
            return $true
        }
    }

    return $false
}

function Get-RequiredHostTools {
    param([pscustomobject[]]$Dependencies)

    $required = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)

    foreach ($dependency in $Dependencies) {
        switch ($dependency.Type) {
            "openssl" {
                [void]$required.Add("perl")
                [void]$required.Add("nasm")
            }
            "zlib" { [void]$required.Add("cmake") }
            "libxml2" { [void]$required.Add("cmake") }
            "cairo" {
                [void]$required.Add("meson")
                [void]$required.Add("ninja")
            }
            "libzip" { [void]$required.Add("cmake") }
            "antlr4" { [void]$required.Add("cmake") }
            "libssh" { [void]$required.Add("cmake") }
            "proj" { [void]$required.Add("cmake") }
            "gdal" { [void]$required.Add("cmake") }
            "googletest" { [void]$required.Add("cmake") }
            "mysql" { [void]$required.Add("cmake") }
            "connector-cpp" { [void]$required.Add("cmake") }
        }

        if (-not $BuildOnly -and (Test-DependencyArchiveNeedsTar -Dependency $dependency)) {
            [void]$required.Add("tar")
        }
    }

    return @($required | Sort-Object)
}

function Get-RequiredVsTools {
    param([pscustomobject[]]$Dependencies)

    if ($DownloadOnly) {
        return @()
    }

    $required = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)

    foreach ($dependency in $Dependencies) {
        switch ($dependency.Type) {
            "openssl" {
                [void]$required.Add("nmake.exe")
            }
            "zlib" {
                [void]$required.Add("cl.exe")
                [void]$required.Add("lib.exe")
            }
            "sqlite" {
                [void]$required.Add("cl.exe")
                [void]$required.Add("lib.exe")
            }
            "vsqlitepp" {
                [void]$required.Add("cl.exe")
                [void]$required.Add("lib.exe")
            }
            "python" {
                [void]$required.Add("cmd.exe")
            }
            "htmlrenderer" {
                [void]$required.Add("MSBuild.exe")
            }
            default {
                if ($dependency.Type -in @("libxml2", "cairo", "libzip", "antlr4", "libssh", "proj", "gdal", "googletest", "mysql", "connector-cpp")) {
                    [void]$required.Add("MSBuild.exe")
                }
            }
        }
    }

    return @($required | Sort-Object)
}

function Check-HostTools {
    param([pscustomobject[]]$Dependencies)

    Write-Section "Host Tools"
    $required = @(Get-RequiredHostTools -Dependencies $Dependencies)
    $missing = @()
    foreach ($tool in $required) {
        if (Test-CommandAvailable $tool) {
            Write-Ok ("{0,-10} {1}" -f $tool, (Get-Command $tool).Source)
        }
        else {
            Write-Fail ("{0,-10} not found" -f $tool)
            $missing += $tool
        }
    }

    if ($missing) {
        throw ("Missing required tools: {0}" -f ($missing -join ", "))
    }

    $requiredVsTools = @(Get-RequiredVsTools -Dependencies $Dependencies)
    if ($requiredVsTools.Count -gt 0) {
        Enter-VsBuildEnvironment
        foreach ($tool in $requiredVsTools) {
            if (Test-CommandAvailable $tool) {
                Write-Ok ("{0,-10} {1}" -f $tool, (Get-Command $tool).Source)
            }
            else {
                throw "Required Visual Studio tool not found after loading the build environment: $tool"
            }
        }
    }
}

function Process-Dependency {
    param([pscustomobject]$Dependency)

    if ($Clean -and (Test-DependencyBuilt $Dependency)) {
        Remove-Item -LiteralPath (Get-StampPath $Dependency) -Force
    }

    if (Test-DependencyBuilt $Dependency) {
        if (-not (Test-DependencyOutputsPresent $Dependency)) {
            Write-Warn ("Stamp exists but staged outputs are missing - rebuilding {0}" -f (Get-DependencySlug $Dependency))
            Remove-Item -LiteralPath (Get-StampPath $Dependency) -Force
        }
        else {
            Write-Ok ("Already built - skipping {0}" -f (Get-DependencySlug $Dependency))
            return
        }
    }

    if (Test-DependencyBuilt $Dependency) {
        Write-Ok ("Already built - skipping {0}" -f (Get-DependencySlug $Dependency))
        return
    }

    switch ($Dependency.Type) {
        "openssl" { Build-OpenSsl $Dependency }
        "zlib" { Build-ZlibSource $Dependency }
        "libxml2" { Build-LibXml2 $Dependency }
        "cairo" { Build-CairoBundle $Dependency }
        "libzip" { Build-LibZip $Dependency }
        "antlr4" { Build-AntlrRuntime $Dependency }
        "libssh" { Build-LibSsh $Dependency }
        "proj" { Build-Proj $Dependency }
        "gdal" { Build-Gdal $Dependency }
        "googletest" { Build-GoogleTest $Dependency }
        "htmlrenderer" { Build-HtmlRenderer $Dependency }
        "header-only" {
            switch ($Dependency.Name) {
                "boost" { Build-BoostHeaders $Dependency }
                "rapidjson" { Build-RapidJsonHeaders $Dependency }
                default { throw "No header-only handler registered for $($Dependency.Name)" }
            }
        }
        "sqlite" { Build-Sqlite $Dependency }
        "vsqlitepp" { Build-Vsqlitepp $Dependency }
        "python" { Build-Python $Dependency }
        "mysql" { Build-MySQLServer $Dependency }
        "connector-cpp" { Build-ConnectorCpp $Dependency }
        default { throw "Unsupported dependency type: $($Dependency.Type)" }
    }

    if (-not $DownloadOnly) {
        if (-not (Test-DependencyOutputsPresent $Dependency)) {
            throw ("Dependency completed without staging the expected bundle outputs: {0}" -f (Get-DependencySlug $Dependency))
        }
        Mark-DependencyBuilt $Dependency
    }
}

function Show-Configuration([pscustomobject[]]$Dependencies) {
    Write-Section "Configuration"
    Write-Info ("Project root     : {0}" -f $script:ProjectRoot)
    Write-Info ("Bundle directory : {0}" -f $script:BundleDir)
    Write-Info ("Downloads        : {0}" -f $script:DownloadRoot)
    Write-Info ("Build root       : {0}" -f $script:BuildRoot)
    Write-Info ("Generator        : {0}" -f $script:ResolvedGenerator)
    if ($script:VsPath) {
        Write-Info ("Visual Studio    : {0}" -f $script:VsPath)
    }
    Write-Info ("Jobs             : {0}" -f $Jobs)
    if ($Clean) { Write-Warn "--Clean enabled: previous build outputs may be removed." }
    if ($DownloadOnly) { Write-Info "Mode             : download only" }
    if ($BuildOnly) { Write-Info "Mode             : build only" }

    Write-Section ("Dependencies ({0})" -f $Dependencies.Count)
    foreach ($dep in $Dependencies) {
        $source = if ($dep.PSObject.Properties["Urls"] -and $dep.Urls.Count -gt 0) {
            if ($dep.Urls.Count -gt 1) {
                "{0} (+{1} fallback{2})" -f $dep.Urls[0], ($dep.Urls.Count - 1), $(if (($dep.Urls.Count - 1) -eq 1) { "" } else { "s" })
            }
            else {
                $dep.Urls[0]
            }
        }
        elseif ($dep.PSObject.Properties["Url"]) {
            $dep.Url
        }
        elseif ($dep.PSObject.Properties["DisplayUrl"]) {
            $dep.DisplayUrl
        }
        else {
            "-"
        }
        Write-Detail ("{0,-22} {1,-12} {2}" -f (Get-DependencySlug $dep), $dep.Type, $source)
    }
}

function Show-Summary {
    Write-Host ""
    Write-Host "+------------------------------------------------------------------+" -ForegroundColor DarkYellow
    Write-Host "|               3rd-party bundle finished successfully             |" -ForegroundColor Gray
    Write-Host "+------------------------------------------------------------------+" -ForegroundColor DarkYellow
    Write-Host ""
    Write-Detail ("Bundle root        : {0}" -f $script:BundleDir)
    Write-Detail ("Set env var        : MSS_3DPARTY_PATH={0}" -f $script:BundleDir)
    Write-Detail ("Legacy doc alias   : WB_3DPARTY_LIB={0}" -f $script:BundleDir)
    Write-Detail ("Important folders  : bin, include, lib, debug\lib, python")
    Write-Host ""
}

function Main {
    try {
        Write-Banner
        Initialize-BundleLayout
        $script:VsPath = Get-VSInstallPath
        $script:ResolvedGenerator = Resolve-CMakeGenerator -ResolvedVsPath $script:VsPath
        $script:ResolvedToolset = if ($PlatformToolset -ne "auto") { $PlatformToolset } else { Resolve-VsPlatformToolset -ResolvedVsPath $script:VsPath }
        Write-Info ("Toolset          : {0}" -f $script:ResolvedToolset)

        if ($Clean) {
            Write-Section "Cleaning"
            foreach ($path in @($script:BuildRoot, $script:SourceRoot, $script:StampRoot)) {
                if (Test-Path -LiteralPath $path -PathType Container) {
                    Write-Warn ("Removing {0}" -f $path)
                    Remove-Item -LiteralPath $path -Recurse -Force
                }
                Ensure-Directory $path
            }
        }

        $dependencies = @(Get-SelectedDependencies)
        Show-Configuration -Dependencies $dependencies
        Check-HostTools -Dependencies $dependencies

        Write-Section "Processing Dependencies"
        $index = 0
        foreach ($dependency in $dependencies) {
            $index++
            Write-StepHeader -Index $index -Total $dependencies.Count -Label (Get-DependencySlug $dependency)
            Process-Dependency -Dependency $dependency
        }

        Show-Summary
    }
    finally {
        Stop-AllActiveProcesses
        Clear-StatusLine
    }
}

Main
