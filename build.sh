#!/bin/bash
# Build script for udp2raw - supports both Linux and Windows (via cross-compilation)

# Function to display usage information
show_usage() {
    echo "Usage: $0 [options]"
    echo "Options:"
    echo "  --help          Show this help message"
    echo "  --windows       Build for Windows"
    echo "  --linux         Build for Linux"
    echo "  --static        Use static linking"
    echo "  --debug         Build with debug symbols"
    echo "  --clean         Clean build files"
    echo "  --install       Install after building (Linux only)"
}

# Default values
BUILD_WINDOWS=0
BUILD_LINUX=0
STATIC_LINK=0
DEBUG_BUILD=0
CLEAN_BUILD=0
INSTALL_AFTER=0

# Parse command line arguments
if [ $# -eq 0 ]; then
    # No arguments, build for current platform
    if [ "$(uname)" == "Linux" ]; then
        BUILD_LINUX=1
    else
        # Assume Windows if not Linux
        BUILD_WINDOWS=1
    fi
else
    for arg in "$@"; do
        case $arg in
            --help)
                show_usage
                exit 0
                ;;
            --windows)
                BUILD_WINDOWS=1
                ;;
            --linux)
                BUILD_LINUX=1
                ;;
            --static)
                STATIC_LINK=1
                ;;
            --debug)
                DEBUG_BUILD=1
                ;;
            --clean)
                CLEAN_BUILD=1
                ;;
            --install)
                INSTALL_AFTER=1
                ;;
            *)
                echo "Unknown option: $arg"
                show_usage
                exit 1
                ;;
        esac
    done
fi

# Create build directory if it doesn't exist
mkdir -p build

# Clean build files if requested
if [ $CLEAN_BUILD -eq 1 ]; then
    echo "Cleaning build files..."
    rm -rf build/*
fi

# Build for Linux
if [ $BUILD_LINUX -eq 1 ]; then
    echo "Building for Linux..."
    cd build
    
    CMAKE_OPTIONS=""
    
    if [ $STATIC_LINK -eq 1 ]; then
        CMAKE_OPTIONS="$CMAKE_OPTIONS -DSTATIC_LINK=ON"
    fi
    
    if [ $DEBUG_BUILD -eq 1 ]; then
        CMAKE_OPTIONS="$CMAKE_OPTIONS -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON"
    else
        CMAKE_OPTIONS="$CMAKE_OPTIONS -DCMAKE_BUILD_TYPE=Release"
    fi
    
    cmake $CMAKE_OPTIONS ..
    make -j$(nproc)
    
    if [ $INSTALL_AFTER -eq 1 ]; then
        echo "Installing..."
        sudo make install
    fi
    
    cd ..
fi

# Build for Windows (cross-compilation)
if [ $BUILD_WINDOWS -eq 1 ]; then
    echo "Building for Windows..."
    
    # Check if MinGW is installed
    if command -v i686-w64-mingw32-g++-posix &> /dev/null; then
        cd build
        
        CMAKE_OPTIONS="-DCMAKE_TOOLCHAIN_FILE=../windows-toolchain.cmake"
        
        if [ $DEBUG_BUILD -eq 1 ]; then
            CMAKE_OPTIONS="$CMAKE_OPTIONS -DCMAKE_BUILD_TYPE=Debug"
        else
            CMAKE_OPTIONS="$CMAKE_OPTIONS -DCMAKE_BUILD_TYPE=Release"
        fi
        
        # Always enable multi-platform support for Windows
        CMAKE_OPTIONS="$CMAKE_OPTIONS -DENABLE_MP=ON"
        
        cmake $CMAKE_OPTIONS ..
        make -j$(nproc)
        cd ..
    else
        echo "Error: MinGW cross-compiler not found. Please install it to build for Windows."
        echo "On Debian/Ubuntu: sudo apt-get install mingw-w64"
        exit 1
    fi
fi

echo "Build completed successfully!"