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

REM Check for Python
where python >nul 2>&1
if errorlevel 1 (
    echo ERROR: Python not found in PATH
    echo Please install Python 3.10+ from https://python.org/downloads/
    exit /b 1
)
echo   Python: OK

echo.

echo [1/6] Installing MIPS cross-compiler...
if not exist "tools\mips" mkdir tools\mips

REM Check if already installed
if exist "tools\mips\bin\mipsel-none-elf-gcc.exe" (
    echo MIPS toolchain already installed, skipping...
) else (
    echo Downloading MIPS toolchain ^(this may take a while, ~150MB^)...
    curl -L -o tools\mips\mips-toolchain.zip "https://static.grumpycoder.net/pixel/mips/g++-mipsel-none-elf-13.1.0.zip"
    if errorlevel 1 (
        echo ERROR: Failed to download MIPS toolchain
        exit /b 1
    )

    echo Extracting MIPS toolchain...
    powershell -Command "Expand-Archive -Force -Path 'tools\mips\mips-toolchain.zip' -DestinationPath 'tools\mips'"
    del tools\mips\mips-toolchain.zip

    if not exist "tools\mips\bin\mipsel-none-elf-gcc.exe" (
        echo ERROR: mipsel-none-elf-gcc.exe not found after extraction
        exit /b 1
    )
)

REM Add to PATH for this session
set "PATH=%CD%\tools\mips\bin;%PATH%"
echo Done.
echo.

echo [2/6] Installing Ninja build system...
if not exist "tools\ninja" mkdir tools\ninja

REM Check if ninja already exists
if exist "tools\ninja\ninja.exe" (
    echo Ninja already installed, skipping...
) else (
    echo Downloading Ninja v1.11.1...
    curl -L -o tools\ninja\ninja-win.zip "https://github.com/ninja-build/ninja/releases/download/v1.11.1/ninja-win.zip"
    if errorlevel 1 (
        echo ERROR: Failed to download Ninja
        exit /b 1
    )

    echo Extracting Ninja...
    powershell -Command "Expand-Archive -Force -Path 'tools\ninja\ninja-win.zip' -DestinationPath 'tools\ninja'"
    del tools\ninja\ninja-win.zip

    if not exist "tools\ninja\ninja.exe" (
        echo ERROR: ninja.exe not found after extraction
        exit /b 1
    )
)

REM Add to PATH for this session
set "PATH=%CD%\tools\ninja;%PATH%"
echo Done.
echo.

echo [3/6] Initializing SDK submodule...
if not exist "sdk\.git" (
    echo Cloning ps1-bare-metal SDK...
    git clone https://github.com/spicyjpeg/ps1-bare-metal.git sdk
    if errorlevel 1 (
        echo ERROR: Failed to clone SDK repository
        exit /b 1
    )
) else (
    echo SDK already cloned, updating...
    cd sdk
    git pull
    cd ..
)
echo Done.
echo.

echo [4/6] Installing psxavenc...
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

echo [5/6] Installing mkpsxiso...
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

echo [6/7] Setting up Python virtual environment...
if not exist "env\Scripts\python.exe" (
    echo Creating virtual environment...
    python -m venv env
    if errorlevel 1 (
        echo ERROR: Failed to create Python virtual environment
        exit /b 1
    )
)

if not exist "env\Scripts\python.exe" (
    echo ERROR: Python virtual environment was not created properly
    exit /b 1
)

echo Installing Python dependencies...
env\Scripts\python.exe -m pip install pillow --quiet
if errorlevel 1 (
    echo ERROR: Failed to install Python dependencies
    exit /b 1
)
echo Done.
echo.

echo [7/7] Verifying installation...
echo   SDK submodule: OK
if exist "tools\mips\bin\mipsel-none-elf-gcc.exe" (echo   MIPS toolchain: OK) else (echo   MIPS toolchain: MISSING)
if exist "tools\ninja\ninja.exe" (echo   Ninja: OK) else (echo   Ninja: MISSING)
if exist "tools\psxavenc\psxavenc.exe" (echo   psxavenc: OK) else (echo   psxavenc: MISSING)
if exist "tools\mkpsxiso\mkpsxiso.exe" (echo   mkpsxiso: OK) else (echo   mkpsxiso: MISSING)
if exist "env\Scripts\python.exe" (echo   Python venv: OK) else (echo   Python venv: MISSING)
echo Done.
echo.

echo ========================================
echo Installation complete!
echo.
echo To add the MIPS toolchain to your PATH permanently, run:
echo   add_to_path.bat
echo.
echo Or you can start building immediately by running:
echo   build.bat
echo   ^(build.bat will use the local toolchain automatically^)
echo ========================================
