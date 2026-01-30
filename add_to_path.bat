@echo off
REM Add MIPS toolchain to Windows PATH

echo ========================================
echo Add MIPS Toolchain to PATH
echo ========================================
echo.

set "MIPS_PATH=%CD%\tools\mips\bin"

if not exist "%MIPS_PATH%\mipsel-none-elf-gcc.exe" (
    echo ERROR: MIPS toolchain not found at %MIPS_PATH%
    echo Please run install.bat first.
    exit /b 1
)

echo Adding to PATH: %MIPS_PATH%
echo.

REM Use PowerShell to add to user PATH permanently
powershell -Command "& { $path = [Environment]::GetEnvironmentVariable('Path', 'User'); if ($path -notlike '*%MIPS_PATH%*') { [Environment]::SetEnvironmentVariable('Path', $path + ';%MIPS_PATH%', 'User'); Write-Host 'Successfully added to PATH!'; Write-Host 'Please close and reopen your terminal for changes to take effect.'; } else { Write-Host 'Path already contains MIPS toolchain.'; } }"

if errorlevel 1 (
    echo ERROR: Failed to add to PATH
    echo You may need to run this script as Administrator
    exit /b 1
)

echo.
echo ========================================
echo Done!
echo.
echo Close and reopen your terminal, then run:
echo   build.bat
echo ========================================
