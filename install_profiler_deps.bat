@echo off
REM Install required dependencies for the profiler on Windows

echo Installing dependencies for UDP2RAW Performance Profiler...

REM Check if vcpkg is installed
where vcpkg >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo vcpkg not found! Installing vcpkg...
    
    REM Create a temporary directory
    mkdir vcpkg_tmp
    cd vcpkg_tmp
    
    REM Clone vcpkg
    git clone https://github.com/Microsoft/vcpkg.git
    cd vcpkg
    
    REM Bootstrap vcpkg
    call bootstrap-vcpkg.bat
    
    REM Add vcpkg to PATH
    echo Adding vcpkg to PATH...
    setx PATH "%PATH%;%CD%"
    
    cd ..\..
    echo vcpkg installed successfully!
) else (
    echo vcpkg is already installed.
)

REM Install required packages
echo Installing required packages...
vcpkg install curl:x64-windows nlohmann-json:x64-windows

echo.
echo Dependencies installed successfully!
echo.
echo Please rebuild the project using:
echo   cmake -G "MinGW Makefiles" -DCMAKE_TOOLCHAIN_FILE=[path_to_vcpkg]/scripts/buildsystems/vcpkg.cmake ..
echo   cmake --build .
echo.
echo Replace [path_to_vcpkg] with the actual path to your vcpkg installation.
echo. 