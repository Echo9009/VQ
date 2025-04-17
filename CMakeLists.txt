#note: experimental
#      currently only used for generating `compile_commands.json` for clangd.
#      to build this project, it's suggested to use `makefile` instead

cmake_minimum_required(VERSION 3.10)
project(udp2raw)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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
    worker.cpp
    lock_free_queue.cpp
    server_core.cpp
    lib/aes_faster_c/aes.cpp
    lib/aes_faster_c/wrapper.cpp
    my_ev.cpp
)

# Add platform-specific sources
if(WIN32)
    list(APPEND COMMON_SOURCES pcap_wrapper.cpp)
endif()

# Set include directories
include_directories(SYSTEM "libev")
include_directories(".")

# Common compiler flags
set(COMMON_FLAGS "-Wall -Wextra -Wno-unused-variable -Wno-unused-parameter -Wno-missing-field-initializers")

# Set compiler flags based on platform
if(WIN32)
    # Windows compiler flags
    set(CMAKE_CXX_FLAGS "${COMMON_FLAGS} -O2 -Wno-narrowing -fpermissive -std=c++14")
    if(MINGW)
        # Additional MinGW-specific flags
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static")
    endif()
else()
    # Linux compiler flags
    set(CMAKE_CXX_FLAGS "${COMMON_FLAGS} -O2 -g -std=c++14 -pthread")
    # Optional: Add sanitizer flags for debug builds on Linux
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address,undefined")
    endif()
endif()

# Define the main executable
add_executable(udp2raw ${COMMON_SOURCES})

# Link platform-specific libraries
target_link_libraries(udp2raw ${PLATFORM_LIBS})

# Installation rules
install(TARGETS udp2raw DESTINATION bin)
install(FILES example.conf DESTINATION etc/udp2raw)
