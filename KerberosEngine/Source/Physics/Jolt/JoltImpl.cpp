#include "JoltImpl.hpp"

#include <cstdarg>
#include <cstdio>
#include <tuple>

import Kerberos;

namespace Kerberos::Physics {

void TraceImpl(const char* inFmt, ...)
{
    // Format the message
    va_list list;
    va_start(list, inFmt);
    char buffer[1024];
    std::ignore = std::vsnprintf(buffer, sizeof(buffer), inFmt, list);
    va_end(list);

    Log::CoreTrace("Jolt: {}", buffer);
}

bool AssertFailedImpl(const char* inExpression, const char* inMessage, const char* inFile, std::uint32_t inLine)
{
    Log::CoreError("{}:{}: ({}) {}", inFile, inLine, inExpression, inMessage != nullptr ? inMessage : "");

    return true;
};

}