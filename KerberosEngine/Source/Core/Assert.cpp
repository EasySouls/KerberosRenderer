module;

#if defined(_MSC_VER)
#include <intrin.h>
#endif

module Kerberos;

namespace Kerberos {

void DebugBreak()
{
#if defined(_MSC_VER)
    __debugbreak();
#elif defined(__clang__) || defined(__GNUC__)
    __builtin_trap();
#endif
}

}