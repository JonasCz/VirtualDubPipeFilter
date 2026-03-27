@echo off
setlocal

set "SRC_DIR=%~dp0."
set "BUILD_DIR=%~dp0build"
set "DEPLOY_DIR=C:\Users\Jonas\Desktop\VirtualDub2\plugins64"

cmake -B "%BUILD_DIR%" -S "%SRC_DIR%" -G "Visual Studio 17 2022" -A x64
if %errorlevel% neq 0 (
    echo CMake configure failed.
    exit /b 1
)

cmake --build "%BUILD_DIR%" --config Release
if %errorlevel% neq 0 (
    echo Build failed.
    exit /b 1
)

copy /Y "%BUILD_DIR%\Release\PipeFilter.vdf" "%DEPLOY_DIR%\PipeFilter.vdf"
if %errorlevel% neq 0 (
    echo Copy failed.
    exit /b 1
)

echo.
echo Build succeeded: %DEPLOY_DIR%\PipeFilter.vdf
