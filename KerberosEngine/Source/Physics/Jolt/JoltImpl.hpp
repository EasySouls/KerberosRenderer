#pragma once

#include "Core/Core.hpp"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <tuple>

namespace Kerberos::Physics
{
	static void TraceImpl(const char* inFmt, ...)
	{
		/// TODO: Use Kerberos logging, and when implemented, a separate physics logger.

		// Format the message
		va_list list;
		va_start(list, inFmt);
		char buffer[1024];
		std::ignore = std::vsnprintf(buffer, sizeof(buffer), inFmt, list);
		va_end(list);

		KBR_CORE_TRACE("Jolt: {}", buffer);
	}

	static bool AssertFailedImpl(const char* inExpression, const char* inMessage, const char* inFile, std::uint32_t inLine)
	{
		KBR_CORE_ERROR("{}:{}: ({}) {}", inFile, inLine, inExpression, inMessage != nullptr ? inMessage : "");

		return true;
	};
}
