#pragma once

#include "Logging/Log.hpp"

#include <memory>

namespace Kerberos
{
#ifdef KBR_DEBUG
    #define KBR_PROFILE
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

#define KBR_BIND_FN(fn) [this](auto&&... args) -> decltype(auto) { return fn(std::forward<decltype(args)>(args)...); }

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

    template<typename T>
    using WeakRef = std::weak_ptr<T>;
}