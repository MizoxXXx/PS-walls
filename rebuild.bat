@echo off
setlocal enabledelayedexpansion

echo [!] Killing existing process...
cd /d "%~dp0"
taskkill /F /IM ps~walls.exe 2>nul

:: Always work inside a build/ subdirectory
set BUILD_DIR=%~dp0build

echo [+] Entering build directory...
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
cd /d "%BUILD_DIR%"

echo [+] Configuring...
cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
if %ERRORLEVEL% NEQ 0 (
    echo [-] Configuration failed.
    echo [TIP] Make sure CMake and MinGW are installed and added to PATH.
    pause
    exit /b 1
)

echo [+] Building...
cmake --build . --config Release
if %ERRORLEVEL% NEQ 0 (
    echo [!] Build failed. Attempting clean rebuild...
    cd /d "%~dp0"
    rd /S /Q "%BUILD_DIR%"
    mkdir "%BUILD_DIR%"
    cd /d "%BUILD_DIR%"

    echo [+] Re-configuring...
    cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
    if !ERRORLEVEL! NEQ 0 (
        echo [-] Configuration failed.
        echo [TIP] If 'Access is denied', please close all terminals and delete the 'build' folder manually.
        pause
        exit /b 1
    )

    echo [+] Re-building...
    cmake --build . --config Release
)

if %ERRORLEVEL% EQU 0 (
    echo [+] Build successful!
    if exist Release\ps~walls.exe (
        echo [+] Executable located at: %BUILD_DIR%\Release\ps~walls.exe
    ) else if exist ps~walls.exe (
        echo [+] Executable located at: %BUILD_DIR%\ps~walls.exe
    )
) else (
    echo [-] Build failed.
)
pause
