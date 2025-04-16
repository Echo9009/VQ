#pragma once

// Fixes for libev on Windows platform

#if defined(_WIN32) || defined(__MINGW32__)
// Redefine FD_SETSIZE before winsock2.h is included
#ifdef FD_SETSIZE
#undef FD_SETSIZE
#endif
#define FD_SETSIZE 4096

#include <winsock2.h>
#include <windows.h>

// Fix for SOCKET type handling
#undef INVALID_SOCKET
#define INVALID_SOCKET  (SOCKET)(~0)

// Define socket invalid value
#define SOCKET_ERROR (-1)

// Override libev's socket handling - using casts that work for MinGW
#define EV_FD_TO_WIN32_HANDLE(fd) ((HANDLE)(UINT_PTR)(fd))
#define EV_WIN32_HANDLE_TO_FD(handle) ((int)(UINT_PTR)(handle))
#define EV_WIN32_CLOSE_FD(fd) closesocket((SOCKET)(fd))

// Use select for Windows
#define EV_USE_SELECT 1
#define EV_SELECT_IS_WINSOCKET 1
#endif 