# Windows cross-compilation toolchain file for CMake

# System information
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86)

# Specify the cross compiler
set(CMAKE_C_COMPILER i686-w64-mingw32-gcc-posix)
set(CMAKE_CXX_COMPILER i686-w64-mingw32-g++-posix)
set(CMAKE_RC_COMPILER i686-w64-mingw32-windres)

# Where to look for the target environment
set(CMAKE_FIND_ROOT_PATH /usr/i686-w64-mingw32)

# Adjust the default behavior of the FIND_XXX() commands
# search programs in the host environment
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)

# search headers and libraries in the target environment
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# Set Windows-specific options
add_definitions(-D_WIN32)

# Enable static linking by default for Windows
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")

# Set output name with .exe extension
set(CMAKE_EXECUTABLE_SUFFIX ".exe")