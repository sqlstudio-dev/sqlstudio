<#
.SYNOPSIS
Stages Windows runtime files incrementally, using MSS_3DPARTY_PATH.
.EXAMPLE
.\PrepareOutputDir.ps1 . Debug x64
#>
[CmdletBinding()]
param(
    [Parameter(Position = 0)][string]$SolutionDirectory,
    [Parameter(Position = 1)][string]$ConfigurationName,
    [Parameter(Position = 2)][string]$Architecture
)

$ErrorActionPreference = 'Stop'
$script:Missing = [System.Collections.Generic.HashSet[string]]::new()
function Write-Info([string]$Message) { Write-Host "[INFO] $Message" -ForegroundColor Cyan }

function Copy-Files {
    param([string]$Source, [string]$Destination, [switch]$Recurse,
          [string[]]$ExcludeFiles = @(), [string[]]$ExcludeDirectories = @('.svn'))
    if (Test-Path -LiteralPath $Source -PathType Container) {
        $sourceDirectory = $Source
        $pattern = '*'
    } else {
        $sourceDirectory = Split-Path -Path $Source -Parent
        $pattern = Split-Path -Path $Source -Leaf
    }
    if (-not (Test-Path -LiteralPath $sourceDirectory -PathType Container)) {
        [void]$script:Missing.Add($Source)
        return
    }
    if (-not @(Get-ChildItem -LiteralPath $sourceDirectory -Filter $pattern -File -Recurse:$Recurse).Count) {
        [void]$script:Missing.Add($Source)
        return
    }
    [void][System.IO.Directory]::CreateDirectory($Destination)
    $copyArguments = @($sourceDirectory, $Destination, $pattern, '/XO', '/R:0', '/W:0', '/NFL', '/NDL', '/NJH', '/NJS', '/NP')
    if ($Recurse) { $copyArguments += '/E' }
    if ($ExcludeFiles.Count) { $copyArguments += '/XF'; $copyArguments += $ExcludeFiles }
    if ($ExcludeDirectories.Count) { $copyArguments += '/XD'; $copyArguments += $ExcludeDirectories }
    $output = & robocopy.exe @copyArguments 2>&1
    if ($LASTEXITCODE -ge 8) { throw "Copy failed ($LASTEXITCODE): $Source -> $Destination`n$($output -join [Environment]::NewLine)" }
}

try {
    if (-not $SolutionDirectory -or -not $ConfigurationName -or -not $Architecture) {
        throw 'Usage: PrepareOutputDir.ps1 SolutionDirectory ConfigurationName Architecture'
    }
    if (-not $env:MSS_3DPARTY_PATH -or -not (Test-Path -LiteralPath $env:MSS_3DPARTY_PATH -PathType Container)) {
        throw 'MSS_3DPARTY_PATH must point to an existing third-party bundle.'
    }
    $root = (Resolve-Path -LiteralPath $SolutionDirectory).Path.TrimEnd('\', '/')
    $target = Join-Path $root "bin\$Architecture\$ConfigurationName"
    $bundle = $env:MSS_3DPARTY_PATH
    $python = Join-Path $bundle 'Python'
    $pythonLib = Join-Path $python 'lib'
    $debugSuffix = ''
    # CPython's test modules, and _tkinter, which needs Tcl/Tk that is not shipped.
    $pythonExcludes = @('_ctypes_test*.pyd', '_test*.pyd', 'xxlimited*.pyd', '_tkinter*.pyd')
    if ($ConfigurationName -eq 'Debug') { $debugSuffix = '_d' }
    else { $pythonExcludes += '*_d.*' }
    [void][System.IO.Directory]::CreateDirectory($target)
    Write-Info "Preparing output directory: $target"

    Write-Info 'Copy resources, images and scripting libraries ...'
    # Source, destination and recursive flag preserve the distribution layout.
    $resources = @(
        @('res\grt\structs*.xml', 'structs', $true),
        @('images\grt\*.png', 'images\grt', $true),
        @('images\icons\*.png', 'images\icons', $false),
        @('images\icons\SqlStudio.ico', 'images\icons', $false),
        @('images\icons\SqlStudioDoc.ico', '.', $false),
        @('images\ui\*.xpm', 'images\ui', $true),
        @('res\grtdata\*.xml', 'data', $true),
        @('res\wbdata\*.xml', 'data', $true),
        @('res\wbdata\data.db', 'data', $true),
        @('res\mysql.profiles\*.xml', 'mysql.profiles', $false),
        @('res\snippets\*.txt', 'snippets', $false),
        @('res\scripts\script_templates\*.txt', 'script_templates', $false),
        @('res\scripts\sys', 'sys', $true),
        @('res\scripts\vbs\*.vbs', '.', $true),
        @('res\scripts\python\*.py', '.', $true),
        @('res\scripts\snippets\shell_snippets.*', '.', $true),
        @('library\sshtunnel\sshtunnel.py', '.', $true),
        @('res\scripts\shell\*.vbs', '.', $true),
        @('res\scripts\firewall', 'firewall', $true),
        @('library\python\studio\*.py', 'studio', $true),
        @('library\forms\swig\mforms.py', '.', $true),
        @('library\forms\swig\cairo.py', '.', $true),
        @('res\sqlidedata\templates\*', 'modules\data\sqlide', $true),
        @('res\sqlidedata\context-help\*', 'modules\data\sqlide', $true),
        @('samples\models\*', 'extras', $false)
    )
    foreach ($folder in @('toolbar', 'changeset', 'admin', 'migration')) {
        $resources += ,@("images\$folder\*.png", 'images\icons', $true)
    }
    foreach ($folder in @('cursors', 'ui', 'home', 'sql')) {
        $resources += ,@("images\$folder\*.png", "images\$folder", $true)
    }
    foreach ($entry in $resources) {
        Copy-Files (Join-Path $root $entry[0]) (Join-Path $target $entry[1]) -Recurse:$entry[2]
    }

    Write-Info 'Copy executables and Python libraries ...'
    foreach ($exe in @('mysqldump', 'mysql', 'ogrinfo', 'ogr2ogr', 'ccapiserver')) {
        Copy-Files (Join-Path $bundle "bin\$exe.exe") $target
    }
    Copy-Files "$python\python312$debugSuffix.dll" $target
    Copy-Files "$python\python$debugSuffix.exe" $target
    Copy-Files "$pythonLib\*.py" "$target\python\lib"
    foreach ($package in @('multiprocessing', 'email', 'encodings', 'logging', 'json', 'collections',
        'sqlite3', 'xml', 'importlib', 'asyncio', 'concurrent', 'ctypes', 'curses', 'dbm', 'ensurepip',
        'html', 'http', 'idlelib', 'msilib', 'pydoc_data', 'tkinter', 'turtledemo', 'urllib', 'venv',
        'wsgiref', 'xmlrpc', '__phello__', 're', 'tomllib', 'zoneinfo', 'zipfile')) {
        Copy-Files "$pythonLib\$package" "$target\python\lib\$package" -Recurse
    }
    Copy-Files "$pythonLib\lib2to3" "$target\python\lib\lib2to3" -Recurse -ExcludeDirectories @('.svn', 'tests')
    Copy-Files "$python\Dlls\*$debugSuffix.pyd" "$target\python\DLLs" -ExcludeFiles $pythonExcludes
    Copy-Files "$python\pyodbc*$debugSuffix.pyd" "$target\python\site-packages" -ExcludeFiles $pythonExcludes
    Copy-Files "$python\pyodbc$debugSuffix*.pyd" "$target\python\site-packages" -ExcludeFiles $pythonExcludes
    Copy-Files "$python\*sqlite3*$debugSuffix.dll" "$target\python\DLLs" -ExcludeFiles $pythonExcludes
    # OpenSSL as built by CPython, for _ssl and _hashlib; Python loads it from
    # the directory of the extension module.
    Copy-Files "$python\libcrypto-3.dll" "$target\python\DLLs"
    Copy-Files "$python\libssl-3.dll" "$target\python\DLLs"
    Copy-Files "$python\Libs\site-packages\*.pyd" "$target\python\site-packages" -Recurse

    Write-Info 'Copy runtime libraries (including libxml2, PROJ, GDAL and zlib) ...'
    # Release DLLs also support release-built tools in Debug. Prefer Debug for
    # identical filenames, regardless of timestamps. Do not recurse into other configurations.
    $runtimeRoots = @()
    if ($ConfigurationName -eq 'Debug') {
        $runtimeRoots += Join-Path $bundle 'debug\lib'
        $runtimeRoots += Join-Path $bundle 'lib\debug' # Legacy bundle layout.
        $runtimeRoots += Join-Path $bundle 'debug\bin'
    }
    $runtimeRoots += Join-Path $bundle 'lib'
    $runtimeRoots += Join-Path $bundle 'bin'
    $seen = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($runtimeRoot in $runtimeRoots) {
        if (-not (Test-Path -LiteralPath $runtimeRoot -PathType Container)) { continue }
        foreach ($dll in Get-ChildItem -LiteralPath $runtimeRoot -Filter '*.dll' -File) {
            if (-not $seen.Add($dll.Name)) { continue }
            $destination = Join-Path $target $dll.Name
            $existing = Get-Item -LiteralPath $destination -ErrorAction SilentlyContinue
            if (-not $existing -or $existing.Length -ne $dll.Length -or $existing.LastWriteTimeUtc -ne $dll.LastWriteTimeUtc) {
                Copy-Item -LiteralPath $dll.FullName -Destination $destination -Force
                Write-Host "  Copied $($dll.Name)" -ForegroundColor DarkGreen
            }
        }
        Copy-Files (Join-Path $runtimeRoot '*.exe') $target
    }
    foreach ($family in @('libxml2*.dll', 'proj*.dll', 'gdal*.dll', 'zlib*.dll')) {
        if (-not @($seen | Where-Object { $_ -like $family }).Count) {
            throw "Required runtime library is missing from the bundle: $family"
        }
    }
    foreach ($package in @('proj', 'gdal')) {
        Copy-Files "$bundle\share\$package" "$target\share\$package" -Recurse
    }

    Write-Info 'Copy modules and reporting templates ...'
    Copy-Files "$root\backend\wbpublic\sqlide\res\*" "$target\modules\data"
    foreach ($module in @('db.mysql.sqlide', 'db.mysql', 'db.mssql', 'db.generic', 'db.sybase',
        'db.postgresql', 'db.sql92', 'db.sqlanywhere', 'db.sqlite', 'db.msaccess')) {
        Copy-Files "$root\modules\$module\res\*.xml" "$target\modules\data"
    }
    Copy-Files "$root\modules\db.mysql\res\db_mysql_catalog_reporting" "$target\modules\data\db_mysql_catalog_reporting" -Recurse
    Copy-Files "$root\modules\wb.model\res\wb_model_reporting" "$target\modules\data\wb_model_reporting" -Recurse
    foreach ($module in @('db.sybase', 'wb.utils', 'wb.sqlide')) {
        Copy-Files "$root\modules\$module\*" "$target\modules"
    }
    foreach ($module in @('db.mysql', 'db.mssql', 'db.generic', 'db.sql92', 'db.postgresql',
        'db.sqlanywhere', 'db.sqlite', 'db.msaccess')) {
        Copy-Files "$root\modules\$module\*.py" "$target\modules"
    }
    if ($ConfigurationName -ne 'Release_OSS') {
        Copy-Files "$root\plugins\wb.admin\internal" "$target\modules" -Recurse
    }
    foreach ($plugin in @('wb.admin\frontend', 'wb.admin\backend', 'migration', 'migration\frontend',
        'migration\backend', 'migration\dbcopy', 'wb.bugreport', 'wb.query.analysis', 'wb.updater\backend', 'wb.sqlide')) {
        Copy-Files "$root\plugins\$plugin\*.py" "$target\modules"
    }
    # Commercial builds ship the -commercial variants when the tree has them.
    $licenseSuffix = ''
    if ($ConfigurationName -ne 'Release_OSS' -and (Test-Path -LiteralPath "$root\README-commercial.md")) { $licenseSuffix = '-commercial' }
    Copy-Files "$root\README$licenseSuffix.md" $target
    Copy-Files "$root\license$licenseSuffix.txt" $target

    foreach ($missingSource in $script:Missing) {
        Write-Host "[WARN] No files found: $missingSource" -ForegroundColor Yellow
    }
    Write-Host 'Output directory preparation complete.' -ForegroundColor Green
    exit 0
} catch {
    Write-Host "[ERROR] $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
