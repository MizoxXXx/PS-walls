@echo off
setlocal enabledelayedexpansion

echo [!] Killing existing process...
cd /d "%~dp0"
taskkill /F /IM ps~walls.exe 2>nul

echo [+] Building project...
:: Try to build
cmake --build . --config Release
if %ERRORLEVEL% NEQ 0 (
    echo [!] Build failed. Attempting clean rebuild...
    echo [!] Cleaning CMake cache to fix potential structural path issues...
    if exist CMakeCache.txt del /F /Q CMakeCache.txt
    if exist CMakeFiles rd /S /Q CMakeFiles
    
    echo [+] Re-configuring...
    cmake .. -G "MinGW Makefiles"
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
        echo [+] Executable is located at: Release\ps~walls.exe
    ) else if exist ps~walls.exe (
        echo [+] Executable is located at: ps~walls.exe
    )
) else (
    echo [-] Build failed.
)
pause
