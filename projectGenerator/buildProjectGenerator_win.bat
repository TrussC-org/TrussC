@echo off
REM =============================================================================
REM TrussC Project Generator Build Script (Windows)
REM =============================================================================
REM Run this script to build projectGenerator
REM =============================================================================

echo ==========================================
echo   TrussC Project Generator Build Script
echo ==========================================
echo.

REM Move to script directory
cd /d "%~dp0"
set SCRIPT_DIR=%cd%

REM Source directory
set SOURCE_DIR=%SCRIPT_DIR%\..\examples\tools\projectGenerator

REM Create build folder
if not exist "%SOURCE_DIR%\build" (
    echo Creating build directory...
    mkdir "%SOURCE_DIR%\build"
)

cd /d "%SOURCE_DIR%\build"

REM CMake configuration
echo Running CMake...
cmake ..
if %ERRORLEVEL% neq 0 (
    echo.
    echo ERROR: CMake configuration failed!
    echo Please make sure CMake is installed and in your PATH.
    echo.
    pause
    exit /b 1
)

REM Build
echo.
echo Building...
cmake --build . --config Release
if %ERRORLEVEL% neq 0 (
    echo.
    echo ERROR: Build failed!
    echo.
    pause
    exit /b 1
)

REM Copy binary to distribution folder
echo.
echo Copying to distribution folder...
copy /Y "%SOURCE_DIR%\bin\projectGenerator.exe" "%SCRIPT_DIR%\"

echo.
echo ==========================================
echo   Build completed successfully!
echo ==========================================
echo.
echo projectGenerator.exe is located at:
echo   %SCRIPT_DIR%\projectGenerator.exe
echo.

REM Ask to open
set /p answer="Open projectGenerator now? (y/n): "
if /i "%answer%"=="y" (
    start "" "%SCRIPT_DIR%\projectGenerator.exe"
)

pause
