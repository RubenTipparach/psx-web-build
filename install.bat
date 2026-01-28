@echo off
echo ========================================
echo PSX Web Build - Installation Script
echo ========================================
echo.

echo Checking prerequisites...

REM Check for CMake
where cmake >nul 2>&1
if errorlevel 1 (
    echo ERROR: CMake not found in PATH
    echo Please install CMake from https://cmake.org/download/
    exit /b 1
)
echo   CMake: OK

REM Check for GCC cross-compiler
where mipsel-none-elf-gcc >nul 2>&1
if errorlevel 1 (
    echo ERROR: mipsel-none-elf-gcc not found in PATH
    echo Please install the MIPS cross-compiler toolchain.
    echo See: https://github.com/grumpycoders/pcsx-redux/tree/main/tools/gcc-mipsel
    exit /b 1
)
echo   GCC Cross-compiler: OK

REM Check for Python
where python >nul 2>&1
if errorlevel 1 (
    echo ERROR: Python not found in PATH
    echo Please install Python 3.10+ from https://python.org/downloads/
    exit /b 1
)
echo   Python: OK

echo.

echo [1/5] Initializing submodules...
git submodule update --init --recursive
if errorlevel 1 (
    echo ERROR: Failed to initialize submodules
    exit /b 1
)
echo Done.
echo.

echo [2/5] Installing psxavenc...
if not exist "tools\psxavenc" mkdir tools\psxavenc

REM Check if psxavenc already exists
if exist "tools\psxavenc\psxavenc.exe" (
    echo psxavenc already installed, skipping...
) else (
    echo Downloading psxavenc v0.3.1...
    curl -L -o tools\psxavenc\psxavenc-windows.zip "https://github.com/WonderfulToolchain/psxavenc/releases/download/v0.3.1/psxavenc-windows.zip"
    if errorlevel 1 (
        echo ERROR: Failed to download psxavenc
        exit /b 1
    )

    echo Extracting psxavenc...
    powershell -Command "Expand-Archive -Force -Path 'tools\psxavenc\psxavenc-windows.zip' -DestinationPath 'tools\psxavenc'"
    del tools\psxavenc\psxavenc-windows.zip

    REM The exe is inside bin/ subdirectory, move it up
    if exist "tools\psxavenc\bin\psxavenc.exe" (
        move tools\psxavenc\bin\psxavenc.exe tools\psxavenc\psxavenc.exe >nul
        rmdir tools\psxavenc\bin
    )

    if not exist "tools\psxavenc\psxavenc.exe" (
        echo ERROR: psxavenc.exe not found after extraction
        exit /b 1
    )
)
echo Done.
echo.

echo [3/5] Installing mkpsxiso...
if not exist "tools\mkpsxiso" mkdir tools\mkpsxiso

REM Check if mkpsxiso already exists
if exist "tools\mkpsxiso\mkpsxiso.exe" (
    echo mkpsxiso already installed, skipping...
) else (
    echo Downloading mkpsxiso v2.04...
    curl -L -o tools\mkpsxiso\mkpsxiso-2.04-win64.zip "https://github.com/Lameguy64/mkpsxiso/releases/download/v2.04/mkpsxiso-2.04-win64.zip"
    if errorlevel 1 (
        echo ERROR: Failed to download mkpsxiso
        exit /b 1
    )

    echo Extracting mkpsxiso...
    powershell -Command "Expand-Archive -Force -Path 'tools\mkpsxiso\mkpsxiso-2.04-win64.zip' -DestinationPath 'tools\mkpsxiso'"
    del tools\mkpsxiso\mkpsxiso-2.04-win64.zip

    REM Find mkpsxiso.exe in nested directories and move it up
    if exist "tools\mkpsxiso\mkpsxiso-2.04-win64\bin\mkpsxiso.exe" (
        move tools\mkpsxiso\mkpsxiso-2.04-win64\bin\mkpsxiso.exe tools\mkpsxiso\mkpsxiso.exe >nul
        rmdir /s /q tools\mkpsxiso\mkpsxiso-2.04-win64 2>nul
    ) else if exist "tools\mkpsxiso\bin\mkpsxiso.exe" (
        move tools\mkpsxiso\bin\mkpsxiso.exe tools\mkpsxiso\mkpsxiso.exe >nul
        rmdir /s /q tools\mkpsxiso\bin 2>nul
    )

    if not exist "tools\mkpsxiso\mkpsxiso.exe" (
        echo ERROR: mkpsxiso.exe not found after extraction
        exit /b 1
    )
)
echo Done.
echo.

echo [4/5] Setting up Python virtual environment...
if not exist "env" (
    echo Creating virtual environment...
    python -m venv env
)
call env\Scripts\activate.bat
pip install pillow --quiet
echo Done.
echo.

echo [5/5] Verifying installation...
echo   SDK submodule: OK
if exist "tools\psxavenc\psxavenc.exe" (echo   psxavenc: OK) else (echo   psxavenc: MISSING)
if exist "tools\mkpsxiso\mkpsxiso.exe" (echo   mkpsxiso: OK) else (echo   mkpsxiso: MISSING)
if exist "env\Scripts\python.exe" (echo   Python venv: OK) else (echo   Python venv: MISSING)
echo Done.
echo.

echo ========================================
echo Installation complete!
echo.
echo To build the project, run: build.bat
echo ========================================
