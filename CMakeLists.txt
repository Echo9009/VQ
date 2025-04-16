# CMake build system for VQ
# Supports both Windows and Linux platforms

cmake_minimum_required(VERSION 3.7)
project(VQ)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 11)

# Define source files
set(SOURCE_FILES
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
)

# Platform specific settings
if(WIN32)
    # Windows specific settings
    add_definitions(-D_WIN32)
    set(CMAKE_CXX_FLAGS "-Wall -Wextra -Wno-unused-variable -Wno-unused-parameter -Wno-missing-field-initializers -O2")
    set(PLATFORM_LIBS ws2_32)
    
    # Add pcap wrapper for Windows
    list(APPEND SOURCE_FILES pcap_wrapper.cpp)
    
    # Option to use wepoll instead of embedded libev
    option(USE_WEPOLL "Use wepoll instead of embedded libev" OFF)
    if(USE_WEPOLL)
        add_definitions(-DNO_LIBEV_EMBED)
        list(APPEND PLATFORM_LIBS ev)
    endif()
    
    # Multi-platform support
    option(ENABLE_MP "Enable multi-platform support" ON)
    if(ENABLE_MP)
        # Keeping UDP2RAW_MP define for compatibility
        add_definitions(-DUDP2RAW_MP)
    endif()
else()
    # Linux/Unix specific settings
    set(CMAKE_CXX_FLAGS "-Wall -Wextra -Wno-unused-variable -Wno-unused-parameter -Wno-missing-field-initializers -O2")
    set(PLATFORM_LIBS rt pthread)
    
    # Debug build with sanitizers
    option(ENABLE_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)
    if(ENABLE_SANITIZERS)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -fsanitize=address,undefined")
    endif()
    
    # Option for static linking
    option(STATIC_LINK "Link libraries statically" OFF)
    if(STATIC_LINK)
        set(CMAKE_EXE_LINKER_FLAGS "-static")
    endif()
endif()

# Include directories
include_directories(SYSTEM "libev")
include_directories(".")

# Build executable
add_executable(VQ ${SOURCE_FILES})
target_link_libraries(VQ ${PLATFORM_LIBS})

# Installation rules
install(TARGETS VQ DESTINATION bin)

# Print build configuration summary
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Platform: ${CMAKE_SYSTEM_NAME}")
if(WIN32)
    message(STATUS "Multi-platform support: ${ENABLE_MP}")
    message(STATUS "Using wepoll: ${USE_WEPOLL}")
else()
    message(STATUS "Static linking: ${STATIC_LINK}")
    message(STATUS "Sanitizers enabled: ${ENABLE_SANITIZERS}")
endif()
