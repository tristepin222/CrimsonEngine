@echo off
setlocal

REM ============================================================
REM  build_game_package.bat
REM  Packages a standalone game distributable.
REM  Called by the editor's Build Settings panel.
REM
REM  Usage:
REM    build_game_package.bat <project_path> <output_path>
REM
REM  Expects to be run from the same directory as editor.exe,
REM  which is sdk/bin/ (so game_runtime.exe, engine.dll, etc. are next to it).
REM ============================================================

set PROJECT_PATH=%~1
set OUTPUT_PATH=%~2
set SCRIPT_DIR=%~dp0

if "%PROJECT_PATH%"=="" (
    echo [ERROR] No project path provided.
    exit /b 1
)
if "%OUTPUT_PATH%"=="" (
    echo [ERROR] No output path provided.
    exit /b 1
)

echo [Build] Packaging game to: %OUTPUT_PATH%
echo [Build] Project path: %PROJECT_PATH%

REM Create output directory structure
if exist "%OUTPUT_PATH%" rmdir /s /q "%OUTPUT_PATH%"
mkdir "%OUTPUT_PATH%"

REM Copy headless runtime template as game.exe
if exist "%SCRIPT_DIR%bin\game_runtime.exe" (
    copy /Y "%SCRIPT_DIR%bin\game_runtime.exe" "%OUTPUT_PATH%\game.exe"
) else if exist "%SCRIPT_DIR%game_runtime.exe" (
    copy /Y "%SCRIPT_DIR%game_runtime.exe" "%OUTPUT_PATH%\game.exe"
) else (
    echo [ERROR] game_runtime.exe template not found in SDK. Run build_engine.bat first.
    exit /b 1
)


REM Copy engine.dll
copy /Y "%SCRIPT_DIR%engine.dll" "%OUTPUT_PATH%\"

REM Copy engine plugins and project plugins
if exist "%SCRIPT_DIR%plugins" (
    xcopy /E /Y /I "%SCRIPT_DIR%plugins" "%OUTPUT_PATH%\plugins\"
)
if exist "%PROJECT_PATH%\plugins" (
    xcopy /E /Y /I "%PROJECT_PATH%\plugins" "%OUTPUT_PATH%\plugins\"
)

REM Copy compiled built-in shaders
if exist "%SCRIPT_DIR%shaders" (
    xcopy /E /Y /I "%SCRIPT_DIR%shaders" "%OUTPUT_PATH%\shaders\"
) else if exist "%SCRIPT_DIR%..\shaders" (
    xcopy /E /Y /I "%SCRIPT_DIR%..\shaders" "%OUTPUT_PATH%\shaders\"
) else if exist "%SCRIPT_DIR%build\shaders" (
    xcopy /E /Y /I "%SCRIPT_DIR%build\shaders" "%OUTPUT_PATH%\shaders\"
)


REM Copy project assets, scenes, project.settings
if exist "%PROJECT_PATH%\assets" (
    xcopy /E /Y /I "%PROJECT_PATH%\assets" "%OUTPUT_PATH%\assets\"
)
if exist "%PROJECT_PATH%\scenes" (
    xcopy /E /Y /I "%PROJECT_PATH%\scenes" "%OUTPUT_PATH%\scenes\"
)
if exist "%PROJECT_PATH%\project.settings" (
    copy /Y "%PROJECT_PATH%\project.settings" "%OUTPUT_PATH%\"
)

REM Compile user scripts on build in an isolated temporary build directory
if exist "%PROJECT_PATH%\scripts\CMakeLists.txt" (
    echo [Build] Compiling project user scripts...
    if not exist "%PROJECT_PATH%\.script_build" mkdir "%PROJECT_PATH%\.script_build"
    cmake -S "%PROJECT_PATH%\scripts" -B "%PROJECT_PATH%\.script_build" -G "Visual Studio 17 2022" -A x64 -T v143 -DCMAKE_BUILD_TYPE=Release
    cmake --build "%PROJECT_PATH%\.script_build" --config Release
    if %ERRORLEVEL% NEQ 0 (
        echo [ERROR] Project user scripts compilation failed.
        if exist "%PROJECT_PATH%\.script_build" rmdir /s /q "%PROJECT_PATH%\.script_build"
        exit /b %ERRORLEVEL%
    )
    if exist "%PROJECT_PATH%\.script_build" rmdir /s /q "%PROJECT_PATH%\.script_build"
)


REM Copy compiled user script DLLs
if exist "%PROJECT_PATH%\bin" (
    mkdir "%OUTPUT_PATH%\bin"
    copy /Y "%PROJECT_PATH%\bin\*.dll" "%OUTPUT_PATH%\bin\"
)

REM Clean up any development/build artifacts from the output directory
if exist "%OUTPUT_PATH%\*.sln" del /q "%OUTPUT_PATH%\*.sln"
if exist "%OUTPUT_PATH%\*.vcxproj*" del /q "%OUTPUT_PATH%\*.vcxproj*"
if exist "%OUTPUT_PATH%\*.cmake" del /q "%OUTPUT_PATH%\*.cmake"
if exist "%OUTPUT_PATH%\CMakeCache.txt" del /q "%OUTPUT_PATH%\CMakeCache.txt"

echo.
echo [Build] Game packaged successfully at: %OUTPUT_PATH%
echo [Build] Run: %OUTPUT_PATH%\game.exe
echo.

endlocal
exit /b 0

