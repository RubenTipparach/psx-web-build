@echo off
REM Lander Bare-Metal - Build & Serve Script
REM Builds the PSX game using ps1-bare-metal and serves via EmulatorJS

setlocal enabledelayedexpansion

cd /d %~dp0

set SDK_PATH=sdk
set WEB_DIR=web

echo ========================================
echo Lander Bare-Metal - Build ^& Serve
echo ========================================
echo.

REM ========================================
REM Step 0: Set up PATH with local tools
REM ========================================
if exist "tools\mips\bin\mipsel-none-elf-gcc.exe" (
    set "PATH=%CD%\tools\mips\bin;%PATH%"
) else (
    echo ERROR: MIPS toolchain not found. Please run install.bat first.
    exit /b 1
)

if exist "tools\ninja\ninja.exe" (
    set "PATH=%CD%\tools\ninja;%PATH%"
) else (
    echo ERROR: Ninja not found. Please run install.bat first.
    exit /b 1
)

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
if not exist "build\build.ninja" (
    echo [1/4] Configuring CMake...
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
echo [2/4] Building lander.psexe...
cmake --build build
if errorlevel 1 (
    echo ERROR: Build failed!
    exit /b 1
)
echo.

REM ========================================
REM Step 4: Build Disc Image
REM ========================================
echo [3/4] Building disc image...
python tools\build_iso.py
if errorlevel 1 (
    echo ERROR: Disc image creation failed!
    exit /b 1
)

echo [4/4] Done!
echo.

REM ========================================
REM Start Server
REM ========================================
echo ========================================
echo Build Complete!
echo ========================================
echo.
echo Output: build\lander.psexe
echo.
echo Starting server at http://localhost:3000
echo Press Ctrl+C to stop.
echo.

cd %WEB_DIR%
py -m http.server 3000

endlocal
