
#ifdef KBR_USE_MINIAUDIO

#ifdef _MSC_VER
    #pragma warning(push, 0)
#endif

#if defined(__clang__) || defined(__GNUC__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wall"
    #pragma GCC diagnostic ignored "-Wextra"
    #pragma GCC diagnostic ignored "-Wpedantic"
#endif

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio/miniaudio.h"

#ifdef _MSC_VER
    #pragma warning(pop)
#endif

#if defined(__clang__) || defined(__GNUC__)
    #pragma GCC diagnostic pop
#endif

#endif