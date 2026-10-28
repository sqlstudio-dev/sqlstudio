[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [string]$BundleDirectory = $env:MSS_3DPARTY_PATH,
    # Optionally check the DLL staged beside SqlStudio.exe instead of the bundle.
    [string]$RuntimeDirectory
)
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if (-not $BundleDirectory) { throw 'Set MSS_3DPARTY_PATH or pass -BundleDirectory.' }
$bundle = (Resolve-Path -LiteralPath $BundleDirectory).Path
$libDirectory = Join-Path $bundle $(if ($Configuration -eq 'Debug') { 'debug\lib' } else { 'lib' })
if (-not $RuntimeDirectory) { $RuntimeDirectory = $libDirectory }
$runtime = Join-Path (Resolve-Path -LiteralPath $RuntimeDirectory).Path 'antlr4-runtime.dll'
$output = Join-Path $repoRoot "bin\x64\AntlrRuntimeSmoke\$Configuration"
[void][IO.Directory]::CreateDirectory($output)
Copy-Item -LiteralPath $runtime -Destination $output -Force

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio C++ tools were not found.' }
# Import the compiler environment; all file operations remain in PowerShell.
$environment = & $env:ComSpec /d /c "`"$vs\Common7\Tools\VsDevCmd.bat`" -arch=x64 -host_arch=x64 >nul && set"
if ($LASTEXITCODE) { throw 'Could not initialize the compiler environment.' }
foreach ($line in $environment) {
    if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
}
$crt = if ($Configuration -eq 'Debug') { '/MDd' } else { '/MD' }
Push-Location $output
try {
    & cl.exe /nologo /EHsc /std:c++17 $crt "/I$bundle\include\antlr4-runtime" `
        (Join-Path $PSScriptRoot 'AntlrRuntimeSmoke.cpp') /Fe:AntlrRuntimeSmoke.exe `
        /link "/LIBPATH:$libDirectory" antlr4-runtime.lib
    if ($LASTEXITCODE) { throw 'ANTLR smoke test compilation failed.' }
    & (Join-Path $output 'AntlrRuntimeSmoke.exe')
    if ($LASTEXITCODE) { throw "ANTLR ABI smoke test failed ($LASTEXITCODE): $runtime" }
} finally {
    Pop-Location
}
