#include "ev_fixes.h"

#define EV_STANDALONE 1
#define EV_COMMON \
    void *data;   \
    unsigned long long u64;
#define EV_COMPAT3 0

#if defined(_WIN32) || defined(__MINGW32__)
// Windows-specific definitions are now in ev_fixes.h
#endif

//#define EV_VERIFY 2
