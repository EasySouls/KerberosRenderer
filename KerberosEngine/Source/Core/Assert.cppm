module;

#include <string_view>
#include <source_location>
#include <format>
#include <utility>
#include <type_traits>

export module Kerberos:Assert;
import :Log;

export namespace Kerberos {

void DebugBreak();

/// Helper capturing format string and call-site source location simultaneously
template <typename... Args> 
struct AssertFormat
{
    std::format_string<Args...> fmt;
    std::source_location loc;

    explicit(false) consteval AssertFormat(const char* str, const std::source_location& location = std::source_location::current())
        : fmt(str), loc(location)
    {
    }

    explicit(false) consteval AssertFormat(std::string_view str, const std::source_location& location = std::source_location::current())
        : fmt(str), loc(location)
    {
    }
};

inline void KBRAssert(const bool condition, const std::source_location& loc = std::source_location::current())
{
#ifdef KBR_DEBUG
    if (!condition) {
        Log::CoreCritical("ASSERTION FAILED in {} ({}:{})", loc.function_name(), loc.file_name(), loc.line());
        DebugBreak();
    }
#endif
}

inline void KBRAssert(const bool condition,
                      std::string_view message,
                      const std::source_location& loc = std::source_location::current())
{
#ifdef KBR_DEBUG
    if (!condition) {
        Log::CoreCritical(
            "ASSERTION FAILED in {} ({}:{}): {}", loc.function_name(), loc.file_name(), loc.line(), message);
        DebugBreak();
    }
#endif
}

template <typename... Args>
void KBRAssert(const bool condition, 
               std::type_identity_t<AssertFormat<std::type_identity_t<Args>...>> format, 
               Args&&... args)
{
#ifdef KBR_DEBUG
    if (!condition) {
        const std::string msg = std::format(format.fmt, std::forward<Args>(args)...);
        Log::CoreCritical("ASSERTION FAILED in {} ({}:{}): {}",
                          format.loc.function_name(),
                          format.loc.file_name(),
                          format.loc.line(),
                          msg);
        DebugBreak();
    }
#endif
}

}
