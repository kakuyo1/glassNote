@echo off
setlocal enableextensions

set "SCRIPT_DIR=%~dp0"

set "APP_VERSION=0.1.0"
set "PAUSE_ON_EXIT=0"

if not "%~1"=="" if /I not "%~1"=="--pause" set "APP_VERSION=%~1"
if not "%~2"=="" if /I not "%~2"=="--pause" set "APP_VERSION=%~2"
if /I "%~1"=="--pause" set "PAUSE_ON_EXIT=1"
if /I "%~2"=="--pause" set "PAUSE_ON_EXIT=1"

echo [glassNote] One-click release pipeline for %APP_VERSION%

echo [1/2] Package release artifacts and manifests
call "%SCRIPT_DIR%package_release.bat" "%APP_VERSION%"
if errorlevel 1 goto :fail_package

echo [2/2] Upload installer and update-manifest.json
call "%SCRIPT_DIR%upload_release_assets.bat" "%APP_VERSION%"
if errorlevel 1 goto :fail_upload

echo.
echo [DONE] Full release pipeline completed.
echo        Version: %APP_VERSION%
echo        Outputs: dist\glassNote-%APP_VERSION%-win64-setup.exe
echo                 dist\update-manifest.json
echo                 dist\update-manifest-%APP_VERSION%.json
if "%PAUSE_ON_EXIT%"=="1" pause
exit /b 0

:fail_package
echo.
echo [FAILED] Packaging step failed.
echo          Check dist\package_release.log
if "%PAUSE_ON_EXIT%"=="1" pause
exit /b 1

:fail_upload
echo.
echo [FAILED] Upload step failed.
echo          Check dist\upload_release_assets.log
if "%PAUSE_ON_EXIT%"=="1" pause
exit /b 1
