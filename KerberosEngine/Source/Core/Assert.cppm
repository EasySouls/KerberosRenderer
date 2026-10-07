module;

#include <string_view>
#include <source_location>
#include <format>
#include <utility>
#include <concepts>
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

template<typename T>
concept AssertCondition = std::convertible_to<T, bool> && !std::is_pointer_v<std::decay_t<T>>;

template<AssertCondition Cond>
inline void KBRAssert(Cond&& condition, const std::source_location& loc = std::source_location::current())
{
#ifdef KBR_DEBUG
    if (!static_cast<bool>(condition)) {
        Log::CoreCritical("ASSERTION FAILED in {} ({}:{})", loc.function_name(), loc.file_name(), loc.line());
        DebugBreak();
    }
#else
    (void)condition;
    (void)loc;
#endif
}

inline void KBRAssert(const std::source_location& loc = std::source_location::current())
{
#ifdef KBR_DEBUG
    Log::CoreCritical("ASSERTION FAILED in {} ({}:{})", loc.function_name(), loc.file_name(), loc.line());
    DebugBreak();
#else
    (void)loc;
#endif
}

template<AssertCondition Cond>
inline void KBRAssert(Cond&& condition,
                      std::string_view message,
                      const std::source_location& loc = std::source_location::current())
{
#ifdef KBR_DEBUG
    if (!static_cast<bool>(condition)) {
        Log::CoreCritical(
            "ASSERTION FAILED in {} ({}:{}): {}", loc.function_name(), loc.file_name(), loc.line(), message);
        DebugBreak();
    }
#else
    (void)condition;
    (void)message;
    (void)loc;
#endif
}

inline void KBRAssert(std::string_view message,
                      const std::source_location& loc = std::source_location::current())
{
#ifdef KBR_DEBUG
    Log::CoreCritical("ASSERTION FAILED in {} ({}:{}): {}", loc.function_name(), loc.file_name(), loc.line(), message);
    DebugBreak();
#else
    (void)message;
    (void)loc;
#endif
}

template <AssertCondition Cond, typename... Args>
void KBRAssert(Cond&& condition, 
               std::type_identity_t<AssertFormat<std::type_identity_t<Args>...>> format, 
               Args&&... args)
{
#ifdef KBR_DEBUG
    if (!static_cast<bool>(condition)) {
        const std::string msg = std::format(format.fmt, std::forward<Args>(args)...);
        Log::CoreCritical("ASSERTION FAILED in {} ({}:{}): {}",
                          format.loc.function_name(),
                          format.loc.file_name(),
                          format.loc.line(),
                          msg);
        DebugBreak();
    }
#else
    (void)condition;
    (void)format;
    (static_cast<void>(args), ...);
#endif
}

template <typename... Args>
void KBRAssert(std::type_identity_t<AssertFormat<std::type_identity_t<Args>...>> format,
               Args&&... args)
{
#ifdef KBR_DEBUG
    const std::string msg = std::format(format.fmt, std::forward<Args>(args)...);
    Log::CoreCritical("ASSERTION FAILED in {} ({}:{}): {}",
                      format.loc.function_name(),
                      format.loc.file_name(),
                      format.loc.line(),
                      msg);
    DebugBreak();
#else
    (void)format;
    (static_cast<void>(args), ...);
#endif
}

}
