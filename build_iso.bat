@echo off
REM Build PSX disc image (.bin/.cue) from compiled executable
REM Requires mkpsxiso - download from https://github.com/Lameguy64/mkpsxiso/releases

setlocal

set PROJECT_ROOT=%~dp0
set BUILD_DIR=%PROJECT_ROOT%build
set OUTPUT_DIR=%PROJECT_ROOT%web\rom
set WORK_DIR=%BUILD_DIR%\iso_work

REM Check if executable exists
if not exist "%BUILD_DIR%\lander.psexe" (
    echo ERROR: lander.psexe not found!
    echo Run 'cmake --build build' first
    exit /b 1
)

REM Look for mkpsxiso
set MKPSXISO=
if exist "%PROJECT_ROOT%tools\mkpsxiso\mkpsxiso.exe" set MKPSXISO=%PROJECT_ROOT%tools\mkpsxiso\mkpsxiso.exe
if exist "%PROJECT_ROOT%tools\mkpsxiso.exe" set MKPSXISO=%PROJECT_ROOT%tools\mkpsxiso.exe
where mkpsxiso >nul 2>&1 && set MKPSXISO=mkpsxiso

if "%MKPSXISO%"=="" (
    echo ERROR: mkpsxiso not found!
    echo.
    echo Run install.bat first to download mkpsxiso
    echo Or download manually from: https://github.com/Lameguy64/mkpsxiso/releases
    exit /b 1
)

echo Using mkpsxiso: %MKPSXISO%

REM Create working directory
if not exist "%WORK_DIR%" mkdir "%WORK_DIR%"

REM Copy executable
copy /Y "%BUILD_DIR%\lander.psexe" "%WORK_DIR%\lander.psexe" >nul

REM Create SYSTEM.CNF
echo BOOT=cdrom:\LANDER.EXE;1> "%WORK_DIR%\system.cnf"
echo TCB=4>> "%WORK_DIR%\system.cnf"
echo EVENT=10>> "%WORK_DIR%\system.cnf"
echo STACK=801FFFF0>> "%WORK_DIR%\system.cnf"

REM Create ISO XML configuration
(
echo ^<?xml version="1.0" encoding="UTF-8"?^>
echo ^<iso_project image_name="lander.bin" cue_sheet="lander.cue"^>
echo     ^<track type="data"^>
echo         ^<identifiers
echo             system="PLAYSTATION"
echo             application="PLAYSTATION"
echo             volume="LANDER"
echo             volume_set="LANDER"
echo             publisher="BARE_METAL"
echo             data_preparer="MKPSXISO"
echo         /^>
echo         ^<license file="none"/^>
echo         ^<directory_tree^>
echo             ^<file name="SYSTEM.CNF" source="system.cnf"/^>
echo             ^<file name="LANDER.EXE" source="lander.psexe"/^>
echo         ^</directory_tree^>
echo     ^</track^>
echo ^</iso_project^>
) > "%WORK_DIR%\iso.xml"

REM Build disc image
echo Building disc image...
pushd "%WORK_DIR%"
"%MKPSXISO%" iso.xml -o .
if errorlevel 1 (
    echo ERROR: mkpsxiso failed!
    popd
    exit /b 1
)
popd

REM Copy output to web/rom
if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%"
copy /Y "%WORK_DIR%\lander.bin" "%OUTPUT_DIR%\lander.bin" >nul
copy /Y "%WORK_DIR%\lander.cue" "%OUTPUT_DIR%\lander.cue" >nul

echo.
echo SUCCESS! Created:
echo   %OUTPUT_DIR%\lander.bin
echo   %OUTPUT_DIR%\lander.cue
echo.
echo To test, refresh the web page.

endlocal
