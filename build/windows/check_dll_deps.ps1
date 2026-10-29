<#
.SYNOPSIS
Checks that every DLL an output binary imports can be found.

.DESCRIPTION
Lists the imports of each .exe, .dll and .pyd under OutputDir with dumpbin and
looks each import up the way the loader would for the installed app: next to
the binary, in the application directory, as an API set, or in System32.
Imports found only in System32 that are not part of Windows (the Visual C++
runtime, for example) are listed separately, because a clean machine may not
have them. Delay-loaded imports are included. Exits with 1 if any import is
missing, so the check can gate a CI job.

.EXAMPLE
.\check_dll_deps.ps1 -OutputDir ..\..\bin\x64\Release
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$OutputDir,
    # Binaries (file names) whose missing imports are reported but tolerated.
    [string[]]$AllowMissingFor = @(),
    # Fail when the Visual C++ runtime comes from System32 instead of OutputDir,
    # for a package that has to ship it.
    [switch]$RequireAppLocalRuntime
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$OutputDir = (Resolve-Path -LiteralPath $OutputDir).Path
$vsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$dumpbin = (& $vsWhere -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find 'VC\Tools\MSVC\**\bin\Hostx64\x64\dumpbin.exe' | Select-Object -First 1)
if (-not $dumpbin) { throw 'dumpbin.exe was not found.' }

$system32 = [Environment]::GetFolderPath('System')
# Runtime DLLs that live in System32 on the build machine but are not part of
# Windows itself; the installer has to provide them.
$redistributable = '^(vcruntime|msvcp|concrt|vcomp|vccorlib|ucrtbased|vcruntime140d|msvcp140d)'

$binaries = Get-ChildItem -LiteralPath $OutputDir -Recurse -File |
    Where-Object { $_.Extension -in '.exe', '.dll', '.pyd' }
$missing = [System.Collections.Generic.List[string]]::new()
$tolerated = [System.Collections.Generic.List[string]]::new()
$fromSystem = [System.Collections.Generic.SortedSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
$managed = 0

foreach ($binary in $binaries) {
    $output = & $dumpbin /nologo /dependents $binary.FullName
    if ($LASTEXITCODE) { throw "dumpbin failed for $($binary.FullName)" }
    # The import names are the indented lines that end in .dll after the
    # "dependencies:" headers.
    $imports = @($output | Where-Object { $_ -match '^\s{4}\S+\.(dll|DLL|drv|sys|exe)\s*$' } | ForEach-Object { $_.Trim() })
    if ($imports.Count -eq 1 -and $imports[0] -ieq 'mscoree.dll') { $managed++; continue }
    foreach ($import in $imports | Sort-Object -Unique) {
        if ($import -match '^(api|ext)-ms-') { continue }
        if (Test-Path -LiteralPath (Join-Path $binary.DirectoryName $import)) { continue }
        if (Test-Path -LiteralPath (Join-Path $OutputDir $import)) { continue }
        if (Test-Path -LiteralPath (Join-Path $system32 $import)) {
            if ($import -match $redistributable) { [void]$fromSystem.Add($import) }
            continue
        }
        $line = '{0} -> {1}' -f $binary.FullName.Substring($OutputDir.Length + 1), $import
        if ($binary.Name -in $AllowMissingFor) { $tolerated.Add($line) } else { $missing.Add($line) }
    }
}

Write-Host ('Checked {0} binaries ({1} managed assemblies skipped).' -f @($binaries).Count, $managed)
if ($fromSystem.Count) {
    Write-Host ('Imports that need the Visual C++ runtime on the target machine: {0}' -f ($fromSystem -join ', '))
}
foreach ($line in $tolerated) { Write-Host "[WARN] Missing (allowed): $line" -ForegroundColor Yellow }
foreach ($line in $missing) { Write-Host "[ERROR] Missing: $line" -ForegroundColor Red }
if ($RequireAppLocalRuntime -and $fromSystem.Count) {
    $line = 'Visual C++ runtime not shipped: ' + ($fromSystem -join ', ')
    Write-Host "[ERROR] $line" -ForegroundColor Red
    $missing.Add($line)
}
if ($missing.Count) {
    Write-Host ('{0} missing imports.' -f $missing.Count) -ForegroundColor Red
    exit 1
}
Write-Host 'All imports resolved.' -ForegroundColor Green
