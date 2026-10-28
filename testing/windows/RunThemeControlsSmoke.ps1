param(
  [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
  [string]$OutputDirectory,
  [ValidateSet("ThemeControlsSmoke", "UiScaleSmoke", "MainFormStartupSmoke", "ConnectionDialogSmoke")][string]$TestName = "ThemeControlsSmoke"
)
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$outputDir = if ($OutputDirectory) { [IO.Path]::GetFullPath($OutputDirectory) } else { Join-Path $repoRoot "bin\x64\$Configuration" }
$compiler = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
$outputExe = Join-Path $outputDir "$TestName.exe"
$references = @('base.wr.dll', 'WBControls.dll', 'MySQLCsUtilities.dll', 'wbpublic.wr.dll', 'Aga.Controls.dll')
$arguments = @('/nologo', '/platform:x64', '/target:exe', "/out:$outputExe", '/r:System.Drawing.dll', '/r:System.Windows.Forms.dll')
if ($TestName -eq 'ConnectionDialogSmoke') { $references += 'mforms.wr.dll' }
if ($TestName -eq 'MainFormStartupSmoke') {
  $references += @('SqlStudio.exe', 'wbprivate.wr.dll', 'mforms.wr.dll')
  # MainForm's pre-init constructor loads the Home icon from the working directory.
  Copy-Item -LiteralPath (Join-Path $repoRoot 'images/home/WB_Home.png') -Destination $outputDir -Force
}
foreach ($reference in $references) {
  $path = Join-Path $outputDir $reference
  if (!(Test-Path -LiteralPath $path)) { throw "Build the Windows application first: missing $path" }
  $arguments += "/r:$path"
}
$arguments += "/win32manifest:$repoRoot\frontend\windows\SqlStudio\Properties\app.manifest"
Copy-Item -LiteralPath "$repoRoot\frontend\windows\SqlStudio\App.config" -Destination "$outputExe.config" -Force
$arguments += Join-Path $PSScriptRoot "$TestName.cs"
& $compiler @arguments
if ($LASTEXITCODE -ne 0) { throw 'Theme control test compilation failed.' }
Push-Location $outputDir
try {
  & $outputExe
  if ($LASTEXITCODE -ne 0) { throw 'Theme control tests failed.' }
} finally {
  Pop-Location
}
