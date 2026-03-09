@echo off
setlocal enableextensions enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..") do set "REPO_DIR=%%~fI"

set "APP_VERSION=%~1"
if "%APP_VERSION%"=="" set "APP_VERSION=0.1.0"

set "PAUSE_ON_EXIT=0"
if /I "%~2"=="--pause" set "PAUSE_ON_EXIT=1"

set "BUILD_DIR=%REPO_DIR%\build-release"
set "STAGE_DIR=%BUILD_DIR%\stage"
set "DIST_DIR=%REPO_DIR%\dist"
set "WINDEPLOYQT=B:\qtt\6.9.0\msvc2022_64\bin\windeployqt.exe"
set "INNO_SCRIPT=%REPO_DIR%\packaging\glassNote.iss"
set "LOG_FILE=%DIST_DIR%\package_release.log"
set "INSTALLER_NAME=glassNote-%APP_VERSION%-win64-setup.exe"
set "INSTALLER_PATH=%DIST_DIR%\%INSTALLER_NAME%"
set "MANIFEST_PATH=%DIST_DIR%\update-manifest.json"
set "MANIFEST_VERSIONED_PATH=%DIST_DIR%\update-manifest-%APP_VERSION%.json"

set "RELEASE_REPO_URL=%GLASSNOTE_RELEASE_REPO_URL%"
if "%RELEASE_REPO_URL%"=="" set "RELEASE_REPO_URL=https://github.com/kakuyo1/glassNote"
if "%RELEASE_REPO_URL:~-1%"=="/" set "RELEASE_REPO_URL=%RELEASE_REPO_URL:~0,-1%"

set "RELEASE_TAG=%GLASSNOTE_RELEASE_TAG%"
if "%RELEASE_TAG%"=="" set "RELEASE_TAG=V%APP_VERSION%"

set "RELEASE_PAGE_URL=%RELEASE_REPO_URL%/releases/tag/%RELEASE_TAG%"
set "INSTALLER_DOWNLOAD_URL=%RELEASE_REPO_URL%/releases/download/%RELEASE_TAG%/%INSTALLER_NAME%"

if not exist "%DIST_DIR%" mkdir "%DIST_DIR%"

> "%LOG_FILE%" (
    echo [glassNote] Packaging started
    echo [glassNote] Repo: %REPO_DIR%
    echo [glassNote] Version: %APP_VERSION%
)

call :log "[glassNote] Packaging version %APP_VERSION%"
call :log "[INFO] Repo directory: %REPO_DIR%"
call :log "[INFO] Release repository: %RELEASE_REPO_URL%"
call :log "[INFO] Release tag: %RELEASE_TAG%"

where /q cmake.exe
if errorlevel 1 (
    call :log "[ERROR] cmake is not available in PATH."
    goto :fail
)
for /f "delims=" %%I in ('where cmake.exe') do (
    set "CMAKE_EXE=%%~fI"
    goto :cmake_found
)
call :log "[ERROR] cmake lookup failed unexpectedly."
goto :fail

:cmake_found
call :log "[INFO] Using cmake: %CMAKE_EXE%"

where /q cpack.exe
if errorlevel 1 (
    call :log "[ERROR] cpack is not available in PATH."
    goto :fail
)
for /f "delims=" %%I in ('where cpack.exe') do (
    set "CPACK_EXE=%%~fI"
    goto :cpack_found
)
call :log "[ERROR] cpack lookup failed unexpectedly."
goto :fail

:cpack_found
call :log "[INFO] Using cpack: %CPACK_EXE%"

set "ISCC_EXE="
where /q iscc.exe
if not errorlevel 1 (
    for /f "delims=" %%I in ('where iscc.exe') do (
        set "ISCC_EXE=%%~fI"
        goto :iscc_found
    )
)

if exist "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" set "ISCC_EXE=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if not defined ISCC_EXE if exist "%ProgramFiles%\Inno Setup 6\ISCC.exe" set "ISCC_EXE=%ProgramFiles%\Inno Setup 6\ISCC.exe"

:iscc_found
if not defined ISCC_EXE (
    call :log "[ERROR] Inno Setup compiler ISCC.exe was not found."
    call :log "        Checked PATH and default install folders."
    call :log "        Example expected path: C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
    goto :fail
)

if "%ISCC_EXE%"=="" (
    call :log "[ERROR] Inno Setup compiler path resolved to empty value."
    goto :fail
)

if not exist "%ISCC_EXE%" (
    call :log "[ERROR] Inno Setup compiler not found: %ISCC_EXE%"
    goto :fail
)

call :log "[INFO] Using ISCC: %ISCC_EXE%"

if not exist "%WINDEPLOYQT%" (
    call :log "[ERROR] windeployqt not found at: %WINDEPLOYQT%"
    goto :fail
)

if not exist "%INNO_SCRIPT%" (
    call :log "[ERROR] Inno Setup script not found: %INNO_SCRIPT%"
    goto :fail
)

if exist "%STAGE_DIR%" (
    call :log "[INFO] Removing old stage directory: %STAGE_DIR%"
    rmdir /s /q "%STAGE_DIR%"
)

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

call :log "[1/7] Configure CMake (Release)"
cmake -S "%REPO_DIR%" -B "%BUILD_DIR%" -G "Visual Studio 17 2022" -A x64 -DGLASSNOTE_BUILD_VERSION=%APP_VERSION% >> "%LOG_FILE%" 2>&1 || goto :fail

call :log "[2/7] Build glassNote (Release)"
cmake --build "%BUILD_DIR%" --config Release --target glassNote >> "%LOG_FILE%" 2>&1 || goto :fail

call :log "[3/7] Install to staging directory"
cmake --install "%BUILD_DIR%" --config Release --prefix "%STAGE_DIR%" >> "%LOG_FILE%" 2>&1 || goto :fail

if not exist "%STAGE_DIR%\glassNote.exe" (
    call :log "[ERROR] Expected executable missing: %STAGE_DIR%\glassNote.exe"
    goto :fail
)

call :log "[4/7] Deploy Qt runtime dependencies"
"%WINDEPLOYQT%" --release --compiler-runtime --no-translations "%STAGE_DIR%\glassNote.exe" >> "%LOG_FILE%" 2>&1 || goto :fail

call :log "[5/7] Build portable ZIP via CPack"
cpack --config "%BUILD_DIR%\CPackConfig.cmake" -G ZIP -C Release -B "%DIST_DIR%" >> "%LOG_FILE%" 2>&1 || goto :fail

set "ZIP_FOUND=0"
for /f "delims=" %%F in ('dir /b /a:-d "%DIST_DIR%\glassNote-*-portable.zip" 2^>nul') do (
    set "ZIP_FOUND=1"
)

if "%ZIP_FOUND%"=="0" (
    call :log "[WARN] Portable ZIP not found in %DIST_DIR%."
) else (
    call :log "[INFO] Portable ZIP generated in %DIST_DIR%"
)

call :log "[6/7] Build installer via Inno Setup"
"%ISCC_EXE%" "/DAPP_VERSION=%APP_VERSION%" "/DSTAGE_DIR=%STAGE_DIR%" "/DOUTPUT_DIR=%DIST_DIR%" "%INNO_SCRIPT%" >> "%LOG_FILE%" 2>&1 || goto :fail

if not exist "%INSTALLER_PATH%" (
    call :log "[ERROR] Installer not found: %INSTALLER_PATH%"
    goto :fail
)

call :log "[7/7] Compute SHA256 and generate update-manifest.json"
call :compute_sha256 "%INSTALLER_PATH%" INSTALLER_SHA256 || goto :fail

> "%MANIFEST_PATH%" (
    echo {
    echo   "version": "%APP_VERSION%",
    echo   "notes": "See release notes on GitHub.",
    echo   "releasePageUrl": "%RELEASE_PAGE_URL%",
    echo   "windows": {
    echo     "x64": {
    echo       "installerUrl": "%INSTALLER_DOWNLOAD_URL%",
    echo       "sha256": "%INSTALLER_SHA256%"
    echo     }
    echo   }
    echo }
)

copy /y "%MANIFEST_PATH%" "%MANIFEST_VERSIONED_PATH%" >nul
if errorlevel 1 (
    call :log "[ERROR] Failed to create versioned manifest: %MANIFEST_VERSIONED_PATH%"
    goto :fail
)

call :log ""
call :log "[DONE] Packaging finished."
call :log "       Installer output: %INSTALLER_PATH%"
call :log "       Installer SHA256: %INSTALLER_SHA256%"
call :log "       Manifest output: %MANIFEST_PATH%"
call :log "       Versioned manifest: %MANIFEST_VERSIONED_PATH%"
call :log "       Staging directory: %STAGE_DIR%"
call :log "       Log file: %LOG_FILE%"
call :log "       User data in %%AppData%%\glassNote is preserved on uninstall."
goto :success

:fail
call :log ""
call :log "[FAILED] Packaging did not complete."
call :log "         Check log file: %LOG_FILE%"
if "%PAUSE_ON_EXIT%"=="1" pause
exit /b 1

:success
if "%PAUSE_ON_EXIT%"=="1" pause
exit /b 0

:compute_sha256
set "%~2="

set "HASH_VALUE="
set "POWERSHELL_EXE="
for /f "delims=" %%I in ('where powershell 2^>nul') do (
    set "POWERSHELL_EXE=%%~fI"
    goto :hash_try_powershell
)

:hash_try_powershell
if defined POWERSHELL_EXE (
    for /f "usebackq delims=" %%H in (`"%POWERSHELL_EXE%" -NoProfile -ExecutionPolicy Bypass -Command "(Get-FileHash -Algorithm SHA256 -LiteralPath '%~1').Hash.ToLower()" 2^>nul`) do (
        set "HASH_VALUE=%%H"
        goto :hash_done
    )
)

for /f "skip=1 delims=" %%H in ('certutil -hashfile "%~1" SHA256 2^>nul') do (
    set "HASH_LINE=%%H"
    if not "!HASH_LINE!"=="" if /I not "!HASH_LINE:~0,8!"=="CertUtil" (
        set "HASH_LINE=!HASH_LINE: =!"
        set "HASH_VALUE=!HASH_LINE!"
        goto :hash_done
    )
)

:hash_done
if not defined HASH_VALUE (
    call :log "[ERROR] Failed to compute SHA256 for: %~1"
    exit /b 1
)

if "%HASH_VALUE:~63,1%"=="" (
    call :log "[ERROR] Computed SHA256 is too short: %HASH_VALUE%"
    exit /b 1
)

if not "%HASH_VALUE:~64,1%"=="" (
    call :log "[ERROR] Computed SHA256 has unexpected length: %HASH_VALUE%"
    exit /b 1
)

set "%~2=%HASH_VALUE%"
exit /b 0

:log
if "%~1"=="" (
    echo.
    >> "%LOG_FILE%" echo.
    exit /b 0
)
set "MSG=%~1"
echo %MSG%
>> "%LOG_FILE%" echo %MSG%
exit /b 0
