#note: experimental
#      currently only used for generating `compile_commands.json` for clangd.
#      to build this project, it's suggested to use `makefile` instead

cmake_minimum_required(VERSION 3.10)
project(udp2raw)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Enable optimizations
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")

# Enable multi-threading
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")

# Define platform-specific variables and settings
if(WIN32)
    # Windows-specific settings
    add_definitions(-DUDP2RAW_MP -D_WIN32)
    set(PLATFORM_LIBS ws2_32)
    # Disable POSIX warnings on Windows
    if(MSVC)
        add_definitions(-D_CRT_SECURE_NO_WARNINGS)
    else()
        # For MinGW
        add_definitions(-Wno-narrowing)
    endif()
else()
    # Linux-specific settings
    add_definitions(-DUDP2RAW_LINUX)
    set(PLATFORM_LIBS rt pthread)
endif()

# Common source files
set(COMMON_SOURCES
    main.cpp 
    lib/md5.cpp
    lib/pbkdf2-sha1.cpp
    lib/pbkdf2-sha256.cpp
    encrypt.cpp
    log.cpp
    network.cpp
    common.cpp
    connection.cpp
    misc.cpp
    fd_manager.cpp
    client.cpp
    server.cpp
    lib/aes_faster_c/aes.cpp
    lib/aes_faster_c/wrapper.cpp
    my_ev.cpp
    profiler.cpp
)

# Add platform-specific sources
if(WIN32)
    list(APPEND COMMON_SOURCES pcap_wrapper.cpp)
endif()

# Set include directories
include_directories(SYSTEM "libev")
include_directories(".")

# Find required packages
include(FetchContent)

# Add libcurl
find_package(CURL QUIET)
if(NOT CURL_FOUND)
    message(STATUS "libcurl not found, fetching it...")
    FetchContent_Declare(
        curl
        URL https://github.com/curl/curl/releases/download/curl-7_78_0/curl-7.78.0.tar.gz
        URL_HASH SHA256=d82f6a44b65379f7d97b43d0fc006c8231cce6e5dbf70e7f9d2a287d6b385494
    )
    FetchContent_MakeAvailable(curl)
endif()

# Add nlohmann/json
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.10.5/json.tar.xz
    URL_HASH SHA256=5daca6ca216495edf89d167f808d1b6deb16dff8b45aca1848d990c5b56a6d09
)
FetchContent_MakeAvailable(json)

# Common compiler flags
set(COMMON_FLAGS "-Wall -Wextra -Wno-unused-variable -Wno-unused-parameter -Wno-missing-field-initializers")

# Set compiler flags based on platform
if(WIN32)
    # Windows compiler flags
    set(CMAKE_CXX_FLAGS "${COMMON_FLAGS} -O2 -Wno-narrowing -fpermissive")
    if(MINGW)
        # Additional MinGW-specific flags
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static")
    endif()
else()
    # Linux compiler flags
    set(CMAKE_CXX_FLAGS "${COMMON_FLAGS} -O2 -g")
    # Optional: Add sanitizer flags for debug builds on Linux
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address,undefined")
    endif()
endif()

# Define the main executable
add_executable(udp2raw ${COMMON_SOURCES})

# Link platform-specific libraries
target_link_libraries(udp2raw ${PLATFORM_LIBS} CURL::libcurl nlohmann_json::nlohmann_json)

# Installation rules
install(TARGETS udp2raw DESTINATION bin)
install(FILES example.conf DESTINATION etc/udp2raw)
