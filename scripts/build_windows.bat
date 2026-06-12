@echo off
REM SiliconV — Windows Build Script
REM
REM Requirements:
REM   1. Visual Studio 2022 with "Desktop development with C++"
REM   2. Qt 6.5+ (MSVC 2022 64-bit) from https://www.qt.io/download
REM   3. CMake 3.16+ (included with VS2022 or separate install)
REM
REM Usage:
REM   build_windows.bat                  → Debug build
REM   build_windows.bat Release          → Release build
REM   build_windows.bat Release --dry    → Dry run (configure only)

setlocal enabledelayedexpansion

set BUILD_TYPE=%1
if "%BUILD_TYPE%"=="" set BUILD_TYPE=Debug

echo ========================================
echo  SiliconV Windows Build
echo  Config: %BUILD_TYPE%
echo ========================================
echo.

REM Find Qt6
if not defined Qt6_DIR (
    REM Common Qt6 install locations
    if exist "C:\Qt\6.7.0\msvc2022_64\lib\cmake\Qt6" (
        set Qt6_DIR=C:\Qt\6.7.0\msvc2022_64\lib\cmake\Qt6
    )
    if exist "C:\Qt\6.6.0\msvc2022_64\lib\cmake\Qt6" (
        set Qt6_DIR=C:\Qt\6.6.0\msvc2022_64\lib\cmake\Qt6
    )
    if exist "C:\Qt\6.5.0\msvc2022_64\lib\cmake\Qt6" (
        set Qt6_DIR=C:\Qt\6.5.0\msvc2022_64\lib\cmake\Qt6
    )
)

if not defined Qt6_DIR (
    echo ERROR: Qt6 not found. Please set Qt6_DIR environment variable.
    echo Example: set Qt6_DIR=C:\Qt\6.7.0\msvc2022_64\lib\cmake\Qt6
    exit /b 1
)

echo Qt6_DIR = %Qt6_DIR%
echo.

REM Configure
echo [1/2] Configuring...
cmake -B build -G "Visual Studio 17 2022" -A x64 ^
    -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ^
    -DSV_BUILD_QT=ON ^
    -DQt6_DIR=%Qt6_DIR%

if errorlevel 1 (
    echo ERROR: CMake configuration failed.
    exit /b 1
)

REM Build
echo.
echo [2/2] Building...
cmake --build build --config %BUILD_TYPE% --parallel

if errorlevel 1 (
    echo ERROR: Build failed.
    exit /b 1
)

echo.
echo ========================================
echo  Build Complete!
echo  Output: build\%BUILD_TYPE%\SiliconV.exe
echo.
echo  For deployment:
echo    windeployqt build\%BUILD_TYPE%\SiliconV.exe
echo ========================================
