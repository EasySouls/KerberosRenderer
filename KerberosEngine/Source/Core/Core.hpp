#pragma once

#include "Asserts.hpp"

#include <memory>

namespace Kerberos
{
#ifdef KBR_DEBUG
    #define KBR_ENABLE_ASSERTS
    #if defined(KBR_PLATFORM_WINDOWS)
        #define KBR_DEBUGBREAK() __debugbreak()
    #elif defined(KBR_PLATFORM_LINUX)
        #include <signal.h>
        #define KBR_DEBUGBREAK() raise(SIGTRAP)
    #else
        #error "Platform doesn't support debugbreak yet!"
    #endif
#else
    #define KBR_DEBUGBREAK()
#endif

	template<typename T>
	using Owner = std::unique_ptr<T>;

	template<typename T, typename ... Args>
	constexpr Owner<T> CreateOwner(Args&& ... args)
	{
		return std::make_unique<T>(std::forward<Args>(args)...);
	}

	template<typename T>
	using Ref = std::shared_ptr<T>;

	template<typename T, typename ... Args>
	constexpr Ref<T> CreateRef(Args&& ... args)
	{
		return std::make_shared<T>(std::forward<Args>(args)...);
	}
}