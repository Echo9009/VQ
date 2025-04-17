#!/bin/bash

echo "Installing dependencies for UDP2RAW Performance Profiler on Linux..."

# Detect package manager
if command -v apt-get &> /dev/null; then
    # Debian/Ubuntu
    echo "Detected Debian/Ubuntu system"
    sudo apt-get update
    sudo apt-get install -y libcurl4-openssl-dev nlohmann-json3-dev
elif command -v dnf &> /dev/null; then
    # Fedora/RHEL/CentOS 8+
    echo "Detected Fedora/RHEL/CentOS system"
    sudo dnf install -y libcurl-devel nlohmann-json-devel
elif command -v yum &> /dev/null; then
    # CentOS/RHEL
    echo "Detected CentOS/RHEL system"
    sudo yum install -y libcurl-devel
    
    # nlohmann-json may not be available in standard repos
    echo "Installing nlohmann-json from source..."
    mkdir -p json_tmp
    cd json_tmp
    curl -L -o json.tar.gz https://github.com/nlohmann/json/releases/download/v3.10.5/json.tar.xz
    tar -xf json.tar.gz
    cd json-3.10.5
    mkdir build && cd build
    cmake ..
    make -j$(nproc)
    sudo make install
    cd ../../../
    rm -rf json_tmp
elif command -v pacman &> /dev/null; then
    # Arch Linux
    echo "Detected Arch Linux system"
    sudo pacman -Sy curl nlohmann-json
else
    echo "Could not detect package manager. Please install libcurl and nlohmann-json manually."
    exit 1
fi

echo "Dependencies installed successfully!"
echo
echo "Please rebuild the project using:"
echo "  cmake .."
echo "  cmake --build ."
echo 