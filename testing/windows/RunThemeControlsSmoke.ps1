param(
  [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
  [string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$outputDir = if ($OutputDirectory) { [IO.Path]::GetFullPath($OutputDirectory) } else { Join-Path $repoRoot "bin\x64\$Configuration" }
$compiler = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
$outputExe = Join-Path $outputDir 'ThemeControlsSmoke.exe'
$references = @('base.wr.dll', 'WBControls.dll', 'MySQLCsUtilities.dll', 'wbpublic.wr.dll', 'Aga.Controls.dll')
$arguments = @('/nologo', '/platform:x64', '/target:exe', "/out:$outputExe", '/r:System.Drawing.dll', '/r:System.Windows.Forms.dll')
foreach ($reference in $references) {
  $path = Join-Path $outputDir $reference
  if (!(Test-Path -LiteralPath $path)) { throw "Build the Windows application first: missing $path" }
  $arguments += "/r:$path"
}
$arguments += Join-Path $PSScriptRoot 'ThemeControlsSmoke.cs'
& $compiler @arguments
if ($LASTEXITCODE -ne 0) { throw 'Theme control test compilation failed.' }
Push-Location $outputDir
try {
  & $outputExe
  if ($LASTEXITCODE -ne 0) { throw 'Theme control tests failed.' }
} finally {
  Pop-Location
}
