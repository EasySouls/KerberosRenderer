module;

#include "Core/UUID.hpp"
#include "Logging/EditorSink.hpp"

#include <spdlog/spdlog.h>
#include <spdlog/fmt/ostr.h>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <filesystem>
#include <format>
#include <memory>
#include <source_location>
#include <string>
#include <string_view>
#include <utility>

export module Kerberos:Log;

export namespace Kerberos::Log {

enum class Level : uint8_t
{
    Trace = 0,
    Debug,
    Info,
    Warn,
    Error,
    Critical
};

std::unique_ptr<spdlog::logger>& GetCoreLogger();
std::unique_ptr<spdlog::logger>& GetEditorLogger();
std::shared_ptr<EditorSinkMultithreaded>& GetEditorSink();

void Init();
void Shutdown();

void LogMessage(bool isCore, Level level, const std::source_location& loc, std::string_view message);

void SetCoreLogLevel(Level level);
void SetEditorLogLevel(Level level);
void SetLogLevel(Level level);

void Flush();

/// Helper capturing format string and call-site source location simultaneously
template <typename... Args>
struct LogFormat
{
    std::format_string<Args...> fmt;
    std::source_location loc;

    explicit(false) consteval LogFormat(const char* str,
                                        const std::source_location& location = std::source_location::current())
        : fmt(str), loc(location)
    {
    }

    explicit(false) consteval LogFormat(std::string_view str,
                                        const std::source_location& location = std::source_location::current())
        : fmt(str), loc(location)
    {
    }
};

template <typename... Args>
void CoreTrace(std::type_identity_t<LogFormat<Args...>> format, Args&&... args)
{
    LogMessage(true, Level::Trace, format.loc, std::format(format.fmt, std::forward<Args>(args)...));
}

inline void CoreTrace(const std::string_view message,
                      const std::source_location& loc = std::source_location::current())
{
    LogMessage(true, Level::Trace, loc, message);
}

template <typename... Args>
void CoreDebug(std::type_identity_t<LogFormat<Args...>> format, Args&&... args)
{
    LogMessage(true, Level::Debug, format.loc, std::format(format.fmt, std::forward<Args>(args)...));
}

inline void CoreDebug(const std::string_view message,
                      const std::source_location& loc = std::source_location::current())
{
    LogMessage(true, Level::Debug, loc, message);
}

template <typename... Args>
void CoreInfo(std::type_identity_t<LogFormat<Args...>> format, Args&&... args)
{
    LogMessage(true, Level::Info, format.loc, std::format(format.fmt, std::forward<Args>(args)...));
}

inline void CoreInfo(const std::string_view message,
                     const std::source_location& loc = std::source_location::current())
{
    LogMessage(true, Level::Info, loc, message);
}

template <typename... Args>
void CoreWarn(std::type_identity_t<LogFormat<Args...>> format, Args&&... args)
{
    LogMessage(true, Level::Warn, format.loc, std::format(format.fmt, std::forward<Args>(args)...));
}

inline void CoreWarn(const std::string_view message,
                     const std::source_location& loc = std::source_location::current())
{
    LogMessage(true, Level::Warn, loc, message);
}

template <typename... Args>
void CoreError(std::type_identity_t<LogFormat<Args...>> format, Args&&... args)
{
    LogMessage(true, Level::Error, format.loc, std::format(format.fmt, std::forward<Args>(args)...));
}

inline void CoreError(const std::string_view message,
                      const std::source_location& loc = std::source_location::current())
{
    LogMessage(true, Level::Error, loc, message);
}

template <typename... Args>
void CoreCritical(std::type_identity_t<LogFormat<Args...>> format, Args&&... args)
{
    LogMessage(true, Level::Critical, format.loc, std::format(format.fmt, std::forward<Args>(args)...));
}

inline void CoreCritical(std::string_view message,
                         const std::source_location& loc = std::source_location::current())
{
    LogMessage(true, Level::Critical, loc, message);
}

template <typename... Args>
void EditorTrace(std::type_identity_t<LogFormat<Args...>> format, Args&&... args)
{
    LogMessage(false, Level::Trace, format.loc, std::format(format.fmt, std::forward<Args>(args)...));
}

inline void EditorTrace(const std::string_view message,
                        const std::source_location& loc = std::source_location::current())
{
    LogMessage(false, Level::Trace, loc, message);
}

template <typename... Args>
void EditorDebug(std::type_identity_t<LogFormat<Args...>> format, Args&&... args)
{
    LogMessage(false, Level::Debug, format.loc, std::format(format.fmt, std::forward<Args>(args)...));
}

inline void EditorDebug(const std::string_view message,
                        const std::source_location& loc = std::source_location::current())
{
    LogMessage(false, Level::Debug, loc, message);
}

template <typename... Args>
void EditorInfo(std::type_identity_t<LogFormat<Args...>> format, Args&&... args)
{
    LogMessage(false, Level::Info, format.loc, std::format(format.fmt, std::forward<Args>(args)...));
}

inline void EditorInfo(const std::string_view message,
                       const std::source_location& loc = std::source_location::current())
{
    LogMessage(false, Level::Info, loc, message);
}

template <typename... Args>
void EditorWarn(std::type_identity_t<LogFormat<Args...>> format, Args&&... args)
{
    LogMessage(false, Level::Warn, format.loc, std::format(format.fmt, std::forward<Args>(args)...));
}

inline void EditorWarn(const std::string_view message,
                       const std::source_location& loc = std::source_location::current())
{
    LogMessage(false, Level::Warn, loc, message);
}

template <typename... Args>
void EditorError(std::type_identity_t<LogFormat<Args...>> format, Args&&... args)
{
    LogMessage(false, Level::Error, format.loc, std::format(format.fmt, std::forward<Args>(args)...));
}

inline void EditorError(const std::string_view message,
                        const std::source_location& loc = std::source_location::current())
{
    LogMessage(false, Level::Error, loc, message);
}

template <typename... Args>
void EditorCritical(std::type_identity_t<LogFormat<Args...>> format, Args&&... args)
{
    LogMessage(false, Level::Critical, format.loc, std::format(format.fmt, std::forward<Args>(args)...));
}

inline void EditorCritical(const std::string_view message,
                           const std::source_location& loc = std::source_location::current())
{
    LogMessage(false, Level::Critical, loc, message);
}

}

// --- Custom types for fmt ---

template <> 
struct fmt::formatter<glm::vec2> : fmt::formatter<std::string>
{
    static auto format(glm::vec2 vec, const format_context& ctx) -> decltype(ctx.out())
    {
        return fmt::format_to(ctx.out(), "({}, {})", vec.x, vec.y);
    }
};

template <> 
struct fmt::formatter<glm::vec3> : fmt::formatter<std::string>
{
    static auto format(glm::vec3 vec, const format_context& ctx) -> decltype(ctx.out())
    {
        return fmt::format_to(ctx.out(), "({}, {}, {})", vec.x, vec.y, vec.z);
    }
};

template <> 
struct fmt::formatter<glm::vec4> : fmt::formatter<std::string>
{
    static auto format(glm::vec4 vec, const format_context& ctx) -> decltype(ctx.out())
    {
        return fmt::format_to(ctx.out(), "({}, {}, {}, {})", vec.x, vec.y, vec.z, vec.w);
    }
};

template <> 
struct fmt::formatter<std::filesystem::path> : fmt::formatter<std::string>
{
    static auto format(const std::filesystem::path& path, const format_context& ctx) -> decltype(ctx.out())
    {
        return fmt::format_to(ctx.out(), "{}", path.string());
    }
};

template <> 
struct fmt::formatter<Kerberos::UUID> : fmt::formatter<std::string>
{
    static auto format(const Kerberos::UUID uuid, const format_context& ctx) -> decltype(ctx.out())
    {
        return fmt::format_to(ctx.out(), "{}", static_cast<uint64_t>(uuid));
    }
};

// --- Custom types for std::format ---

template <> 
struct std::formatter<glm::vec2> : std::formatter<std::string>
{
    auto format(const glm::vec2& vec, std::format_context& ctx) const
    {
        return std::formatter<std::string>::format(std::format("({}, {})", vec.x, vec.y), ctx);
    }
};

template <> 
struct std::formatter<glm::vec3> : std::formatter<std::string>
{
    auto format(const glm::vec3& vec, std::format_context& ctx) const
    {
        return std::formatter<std::string>::format(std::format("({}, {}, {})", vec.x, vec.y, vec.z), ctx);
    }
};

template <> 
struct std::formatter<glm::vec4> : std::formatter<std::string>
{
    auto format(const glm::vec4& vec, std::format_context& ctx) const
    {
        return std::formatter<std::string>::format(std::format("({}, {}, {}, {})", vec.x, vec.y, vec.z, vec.w), ctx);
    }
};

template <> 
struct std::formatter<std::filesystem::path> : std::formatter<std::string>
{
    auto format(const std::filesystem::path& path, std::format_context& ctx) const
    {
        return std::formatter<std::string>::format(std::format("{}", path.string()), ctx);
    }
};

template <> 
struct std::formatter<Kerberos::UUID> : std::formatter<std::string>
{
    auto format(const Kerberos::UUID& uuid, std::format_context& ctx) const
    {
        return std::formatter<std::string>::format(std::format("{}", static_cast<uint64_t>(uuid)), ctx);
    }
};