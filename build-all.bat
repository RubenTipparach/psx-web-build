@echo off
REM Lander Bare-Metal - Build All Web Targets
REM Builds the PSX game and sets up all web emulator options

setlocal enabledelayedexpansion

cd /d %~dp0

set SDK_PATH=sdk
set BUILD_DIR=build

echo ========================================
echo Lander Bare-Metal - Build All Targets
echo ========================================
echo.

REM ========================================
REM Step 1: Check Python venv exists
REM ========================================
if not exist "%SDK_PATH%\env" (
    echo [SETUP] Creating Python virtual environment...
    cd %SDK_PATH%
    py -m venv env
    env\Scripts\pip.exe install -r tools\requirements.txt
    cd %~dp0
    echo.
)

REM ========================================
REM Step 2: Configure CMake if needed
REM ========================================
if not exist "%BUILD_DIR%\build.ninja" (
    echo [1/5] Configuring CMake...
    cmake --preset debug
    if errorlevel 1 (
        echo ERROR: CMake configuration failed!
        exit /b 1
    )
    echo.
)

REM ========================================
REM Step 3: Build the executable
REM ========================================
echo [2/5] Building lander.psexe...
cmake --build %BUILD_DIR%
if errorlevel 1 (
    echo ERROR: Build failed!
    exit /b 1
)
echo.

REM ========================================
REM Step 4: Setup all web directories
REM ========================================
echo [3/5] Setting up EmulatorJS...
if not exist web mkdir web
if not exist web\rom mkdir web\rom
copy /Y %BUILD_DIR%\lander.psexe web\rom\lander.exe >nul

echo.
echo ========================================
echo Build Complete!
echo ========================================
echo.
echo Output: build\lander.psexe
echo.
echo Starting EmulatorJS on http://localhost:3000
echo Press Ctrl+C to stop.
echo.
cd web
py -m http.server 3000

endlocal
