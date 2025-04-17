#!/bin/bash
# Build script for udp2raw on Linux

echo "Building udp2raw for Linux..."

# Remove existing build directory to avoid cache issues
rm -rf build

# Create build directory
mkdir -p build
cd build

# Check if CMake is available
if ! command -v cmake &> /dev/null; then
    echo "CMake not found! Please install CMake."
    exit 1
fi

# Generate project files
echo "Generating project files with CMake..."
cmake ..

# Build the project
echo "Building the project..."
cmake --build . --config Release

if [ $? -ne 0 ]; then
    echo "Build failed!"
    exit 1
fi

echo "Build completed successfully!"
echo "The executable is located at: udp2raw"
echo "Done!" 