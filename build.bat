@echo off
REM Build script for udp2raw on Windows

ECHO Building udp2raw for Windows...

IF NOT EXIST "build" mkdir build
cd build

REM Check if CMake is available
WHERE cmake >nul 2>nul
IF %ERRORLEVEL% NEQ 0 (
    ECHO CMake not found! Please install CMake and add it to your PATH.
    EXIT /B 1
)

REM Generate project files
ECHO Generating project files with CMake...
cmake -G "MinGW Makefiles" ..

REM Build the project
ECHO Building the project...
cmake --build . --config Release

IF %ERRORLEVEL% NEQ 0 (
    ECHO Build failed!
    EXIT /B 1
)

ECHO Build completed successfully!
ECHO The executable is located at: build\udp2raw.exe

cd ..
ECHO Done! 