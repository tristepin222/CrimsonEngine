@echo off
setlocal enabledelayedexpansion

:: ============================================================
::  build.bat
::  Builds the engine + editor and assembles the SDK.
::  The game is built inside the editor via File -> Build Settings.
:: ============================================================

set CLEAN_BUILD=0
set RUN_EDITOR=0
set CONFIG=Release

if /I "%1"=="--help" (
    echo Usage: build.bat [options]
    echo.
    echo Options:
    echo   --clean                 Remove build and SDK directories.
    echo   --run                   Build and launch editor with sandbox_game.
    echo   --config Debug         Build in Debug mode.
    echo   --config Release       Build in Release mode. ^(default^)
    exit /b 0
)

:parse_args
if "%~1"=="" goto args_done

if /I "%~1"=="--clean" (
    set CLEAN_BUILD=1
    shift
    goto parse_args
)

if /I "%~1"=="--run" (
    set RUN_EDITOR=1
    shift
    goto parse_args
)

if /I "%~1"=="--config" (
    if "%~2"=="" (
        echo [ERROR] --config requires Debug or Release.
        exit /b 1
    )

    if /I "%~2"=="Debug" (
        set CONFIG=Debug
    ) else if /I "%~2"=="Release" (
        set CONFIG=Release
    ) else (
        echo [ERROR] Invalid configuration: %~2
        echo [ERROR] Expected Debug or Release.
        exit /b 1
    )

    shift
    shift
    goto parse_args
)

echo [ERROR] Unknown option: %~1
exit /b 1

:args_done

echo [INFO] Build configuration: !CONFIG!

if !CLEAN_BUILD! equ 1 (
    echo [INFO] Cleaning build and SDK directories...
    if exist build_engine rmdir /s /q build_engine
    if exist build rmdir /s /q build
    if exist sdk rmdir /s /q sdk
    if exist sandbox_game\build rmdir /s /q sandbox_game\build
    if exist sandbox_game\bin rmdir /s /q sandbox_game\bin
)

echo [INFO] Building Engine SDK (editor + runtime + plugins)...



call build_engine.bat --config !CONFIG!
if %errorlevel% neq 0 (
    echo [ERROR] Engine SDK build failed.
    exit /b %errorlevel%
)

:: Compile sandbox_game user scripts dynamically using the SDK CMake config
if exist sandbox_game\scripts\CMakeLists.txt (
    echo [INFO] Building sandbox_game user scripts...
    ver > nul
    if not exist sandbox_game\build mkdir sandbox_game\build
    cmake -S sandbox_game\scripts -B sandbox_game\build -G "Visual Studio 17 2022" -A x64 -T v143 -DCMAKE_BUILD_TYPE=!CONFIG!
    cmake --build sandbox_game\build --config !CONFIG!
    if !errorlevel! neq 0 (
        echo [ERROR] User scripts build failed.
        exit /b !errorlevel!
    )
)

if not exist sandbox_game\shaders mkdir sandbox_game\shaders
xcopy /E /Y /I sdk\shaders sandbox_game\shaders\
if not exist sandbox_game\build\shaders mkdir sandbox_game\build\shaders
xcopy /E /Y /I sdk\shaders sandbox_game\build\shaders\

REM Automatically deploy engine plugins and plugin assets to sandbox_game/plugins/
if not exist sandbox_game\plugins mkdir sandbox_game\plugins
xcopy /E /Y /I sdk\plugins sandbox_game\plugins\
if not exist sandbox_game\build\plugins mkdir sandbox_game\build\plugins
xcopy /E /Y /I sdk\plugins sandbox_game\build\plugins\

echo [SUCCESS] Engine built successfully.
echo.
echo   To open the sandbox project in the editor:
echo     cd sandbox_game
echo     ..\sdk\editor.exe
echo.

:: Run check — open editor with sandbox_game as the project
if "%1"=="--run" set RUN_EDITOR=1
if "%2"=="--run" set RUN_EDITOR=1

if !RUN_EDITOR! equ 1 (
    echo [INFO] Launching editor with sandbox_game project...
    if exist sdk\editor.exe (
        start "" "sdk\editor.exe" "sandbox_game"
    ) else (
        echo [ERROR] editor.exe not found in sdk\. Build may have failed.
    )
)

endlocal

