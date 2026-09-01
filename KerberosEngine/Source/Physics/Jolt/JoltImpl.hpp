#pragma once

#include "Core/Core.hpp"

#include <cstdint>

namespace Kerberos::Physics {

void TraceImpl(const char* inFmt, ...);

bool AssertFailedImpl(const char* inExpression, const char* inMessage, const char* inFile, std::uint32_t inLine);

}
