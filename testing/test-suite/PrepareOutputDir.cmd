@echo off
setlocal enableextensions

rem -------------------------------------------------------------------------------
rem Check parameter
if [%1] == [] goto Usage
if [%2] == [] goto Usage
if [%3] == [] goto Usage

set "ESC="
for /f "delims=" %%e in ('echo prompt $E^| cmd') do set "ESC=%%e"

set "C_INFO=%ESC%[96m"
set "C_OK=%ESC%[92m"
set "C_WARN=%ESC%[93m"
set "C_ERR=%ESC%[91m"
set "C_RESET=%ESC%[0m"

call :Info "Preparing test output directory..."

rem -------------------------------------------------------------------------------
rem Set directory variables
set SOLUTION_DIR=%~1
set CONFIGURATION=%~2
set ARCH=%~3

set TEST_ROOT=%~dp0
if "%TEST_ROOT:~-1%"=="\" set TEST_ROOT=%TEST_ROOT:~0,-1%

set TARGET_DIR=%SOLUTION_DIR%bin\%ARCH%\%CONFIGURATION%
set TEST_TARGET_DIR=%TEST_ROOT%\bin\%ARCH%\%CONFIGURATION%
set TEST_DATA_DIR=%TEST_ROOT%\data
set BUNDLE_DIR=%MSS_3DPARTY_PATH%
if "%BUNDLE_DIR%"=="" set BUNDLE_DIR=%WB_3DPARTY_LIB%

call :Info "Solution directory: %SOLUTION_DIR%"
call :Info "Test root directory: %TEST_ROOT%"
call :Info "Configuration: %CONFIGURATION%"
call :Info "Architecture: %ARCH%"
call :Info "App target directory: %TARGET_DIR%"
call :Info "Test target directory: %TEST_TARGET_DIR%"
if not "%BUNDLE_DIR%"=="" call :Info "Bundle directory: %BUNDLE_DIR%"

if not exist "%TARGET_DIR%" (
  call :Warn "App target directory does not exist yet, creating it..."
  mkdir "%TARGET_DIR%" 1>nul 2>nul
)

if not exist "%TEST_TARGET_DIR%" (
  call :Warn "Test target directory does not exist yet, creating it..."
  mkdir "%TEST_TARGET_DIR%" 1>nul 2>nul
)

if not exist "%TEST_DATA_DIR%" (
  call :Warn "No local test data directory found at %TEST_DATA_DIR%"
) else (
  call :Info "Copy test data files to app output ..."
  call :CopyTestData "%TARGET_DIR%"

  call :Info "Copy test data files to test output ..."
  call :CopyTestData "%TEST_TARGET_DIR%"
)

call :Info "Copy runtime dependencies to test output ..."
call :CopyRuntimeDeps "%TARGET_DIR%" "%TEST_TARGET_DIR%"

if not "%BUNDLE_DIR%"=="" (
  call :CopyBundleDeps "%BUNDLE_DIR%\bin" "%TEST_TARGET_DIR%"
  call :CopyBundleDeps "%BUNDLE_DIR%\lib" "%TEST_TARGET_DIR%"
  if /I "%CONFIGURATION%"=="Debug" call :CopyBundleDeps "%BUNDLE_DIR%\debug\lib" "%TEST_TARGET_DIR%"
)

:Done
call :Ok "Test output directory preparation complete."
endlocal & exit /b 0

:CopyTestData
set "COPY_TARGET=%~1"
if not exist "%COPY_TARGET%\data" mkdir "%COPY_TARGET%\data" 1>nul 2>nul
xcopy /i /s /y /d "%TEST_DATA_DIR%\*" "%COPY_TARGET%\data\." 1> nul 2> nul
if errorlevel 1 (
  call :Warn "Copy returned non-zero status for %COPY_TARGET% (files may still be up to date)."
)
exit /b 0

:CopyRuntimeDeps
set "RUNTIME_SOURCE=%~1"
set "RUNTIME_TARGET=%~2"
if /I "%RUNTIME_SOURCE%"=="%RUNTIME_TARGET%" exit /b 0
if not exist "%RUNTIME_SOURCE%" (
  call :Warn "Runtime source not found: %RUNTIME_SOURCE%"
  exit /b 0
)
for %%F in ("*.dll" "*.exe" "*.manifest") do (
  xcopy /i /y /d "%RUNTIME_SOURCE%\%%~F" "%RUNTIME_TARGET%\." 1>nul 2>nul
)
exit /b 0

:CopyBundleDeps
set "BUNDLE_SOURCE=%~1"
set "BUNDLE_TARGET=%~2"
if not exist "%BUNDLE_SOURCE%" exit /b 0
xcopy /i /y /d "%BUNDLE_SOURCE%\*.dll" "%BUNDLE_TARGET%\." 1>nul 2>nul
if errorlevel 1 (
  call :Warn "Bundle copy returned non-zero status for %BUNDLE_SOURCE%"
)
exit /b 0

:Info
echo %C_INFO%[INFO]%C_RESET% %~1
exit /b 0

:Ok
echo %C_OK%[ OK ]%C_RESET% %~1
exit /b 0

:Warn
echo %C_WARN%[WARN]%C_RESET% %~1
exit /b 0

:Err
echo %C_ERR%[ERR ]%C_RESET% %~1
exit /b 0

:Usage
call :Err "Usage: %0 SolutionDirectory ConfigurationName Architecture"
call :Err "Example: %0 \"D:\develop\MySQL\SqlStudio\\\" Debug x64"
endlocal & exit /b 1
