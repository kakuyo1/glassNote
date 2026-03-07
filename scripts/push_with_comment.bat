@echo off
setlocal

set SCRIPT_DIR=%~dp0
pushd "%SCRIPT_DIR%.."

if not exist ".git" (
    echo Not a git repository: %CD%
    popd
    exit /b 1
)

git remote get-url origin >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo Remote "origin" is not configured.
    popd
    exit /b 1
)

set /p COMMENT=Input commit comment: 
if "%COMMENT%"=="" (
    echo Comment cannot be empty.
    popd
    exit /b 1
)

git add -A
git diff --cached --quiet
if %ERRORLEVEL% EQU 0 (
    echo No changes to commit.
    popd
    exit /b 0
)

git commit -m "%COMMENT%"
if %ERRORLEVEL% NEQ 0 (
    echo Commit failed.
    popd
    exit /b 1
)

git push -u origin main
if %ERRORLEVEL% NEQ 0 (
    echo Push failed.
    popd
    exit /b 1
)

echo Done.
popd
