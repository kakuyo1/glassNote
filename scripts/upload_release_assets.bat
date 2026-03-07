@echo off
setlocal enableextensions

set "SCRIPT_DIR=%~dp0"
set "POWERSHELL_EXE=powershell.exe"

where /q %POWERSHELL_EXE%
if errorlevel 1 (
    echo [ERROR] powershell.exe is not available in PATH.
    exit /b 1
)

set "VERSION_ARG=%~1"
set "PAUSE_SWITCH="
if /I "%~2"=="--pause" set "PAUSE_SWITCH=-PauseOnExit"

"%POWERSHELL_EXE%" -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%upload_release_assets.ps1" -Version "%VERSION_ARG%" %PAUSE_SWITCH%
exit /b %errorlevel%
