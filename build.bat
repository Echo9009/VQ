@echo off
REM Build script for VQ on Windows

SETLOCAL EnableDelayedExpansion

REM Default options
SET DEBUG=0
SET STATIC=1
SET CLEAN=0

REM Parse command line arguments
:parse_args
IF "%~1"=="" GOTO :done_args
IF /I "%~1"=="--help" (
    ECHO Usage: %0 [options]
    ECHO Options:
    ECHO   --help     Show this help message
    ECHO   --debug    Build with debug symbols
    ECHO   --clean    Clean build files before building
    ECHO   --dynamic  Use dynamic linking instead of static
    EXIT /B 0
)
IF /I "%~1"=="--debug" SET DEBUG=1
IF /I "%~1"=="--clean" SET CLEAN=1
IF /I "%~1"=="--dynamic" SET STATIC=0
SHIFT
GOTO :parse_args
:done_args

REM Check if CMake is installed
where cmake >nul 2>nul
IF %ERRORLEVEL% NEQ 0 (
    ECHO Error: CMake is not installed or not in PATH.
    ECHO Please install CMake from https://cmake.org/download/
    EXIT /B 1
)

REM Create build directory if it doesn't exist
IF NOT EXIST build mkdir build

REM Clean build files if requested
IF %CLEAN% EQU 1 (
    ECHO Cleaning build files...
    IF EXIST build\* (
        RMDIR /S /Q build
        MKDIR build
    )
)

REM Configure CMake options
SET CMAKE_OPTIONS=-G "MinGW Makefiles"

IF %DEBUG% EQU 1 (
    SET CMAKE_OPTIONS=!CMAKE_OPTIONS! -DCMAKE_BUILD_TYPE=Debug
) ELSE (
    SET CMAKE_OPTIONS=!CMAKE_OPTIONS! -DCMAKE_BUILD_TYPE=Release
)

IF %STATIC% EQU 1 (
    SET CMAKE_OPTIONS=!CMAKE_OPTIONS! -DSTATIC_LINK=ON
) ELSE (
    SET CMAKE_OPTIONS=!CMAKE_OPTIONS! -DSTATIC_LINK=OFF
)

REM Always enable multi-platform support for Windows
SET CMAKE_OPTIONS=!CMAKE_OPTIONS! -DENABLE_MP=ON

REM Build the project
ECHO Building VQ for Windows...
cd build

ECHO Running CMake with options: !CMAKE_OPTIONS!
cmake !CMAKE_OPTIONS! ..

IF %ERRORLEVEL% NEQ 0 (
    ECHO CMake configuration failed.
    EXIT /B 1
)

ECHO Compiling...
mingw32-make

IF %ERRORLEVEL% NEQ 0 (
    ECHO Build failed.
    EXIT /B 1
)

cd ..
ECHO Build completed successfully!
ECHO The executable is located at: build\VQ.exe