@echo off
setlocal

set BUILD_DIR=%~1
if "%BUILD_DIR%"=="" set BUILD_DIR=build

set TEST_EXE=%BUILD_DIR%\Debug\glassNoteIntegrationTests.exe
if not exist "%TEST_EXE%" (
    echo Test executable not found: %TEST_EXE%
    echo Build first: cmake --build %BUILD_DIR% --config Debug --target glassNoteIntegrationTests
    exit /b 1
)

"%TEST_EXE%" loadEditSaveReloadFlow -v2
