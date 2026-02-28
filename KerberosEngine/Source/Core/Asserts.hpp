#pragma once

#include "Logging/Log.hpp"

namespace Kerberos {

#ifdef KBR_ENABLE_ASSERTS
#define KBR_ASSERT(x, ...) \
        do { \
            if (!(x)) { \
                KBR_ERROR("Assertion Failed: {0}", __VA_ARGS__); \
                KBR_DEBUGBREAK(); \
            } \
        } while (0)

#define KBR_CORE_ASSERT(x, ...) \
        do { \
            if (!(x)) { \
                KBR_CORE_ERROR("Assertion Failed: {0}", __VA_ARGS__); \
                KBR_DEBUGBREAK(); \
            } \
        } while (0)
#else
#define KBR_ASSERT(x, ...) do {} while (0)
#define KBR_CORE_ASSERT(x, ...) do {} while (0)
#endif

}