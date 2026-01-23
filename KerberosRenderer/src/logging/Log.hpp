#pragma once

#include "spdlog/spdlog.h"
#include "spdlog/fmt/ostr.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <memory>
#include <filesystem>

namespace kbr
{
	enum class LogLevel : uint8_t
	{
		Trace = 0,
		Debug,
		Info,
		Warn,
		Error,
		Critical
	};

	class Log
	{
	public:
		static void Init();

		static std::unique_ptr<spdlog::logger>& GetCoreLogger() { return s_CoreLogger; }


		/**
		 * Sets the log level for the core logger.
		 * @param level The minimum log level.
		 */
		static void SetCoreLogLevel(const LogLevel level)
		{
			switch (level)
			{
				case LogLevel::Trace: s_CoreLogger->set_level(spdlog::level::trace); break;
				case LogLevel::Debug: s_CoreLogger->set_level(spdlog::level::debug); break;
				case LogLevel::Info: s_CoreLogger->set_level(spdlog::level::info); break;
				case LogLevel::Warn: s_CoreLogger->set_level(spdlog::level::warn); break;
				case LogLevel::Error: s_CoreLogger->set_level(spdlog::level::err); break;
				case LogLevel::Critical: s_CoreLogger->set_level(spdlog::level::critical); break;
			}
		}


		/**
		 * Sets the log level for both core and client loggers.
		 * @param level The minimum log level.
		 */
		static void SetLogLevel(const LogLevel level)
		{
			SetCoreLogLevel(level);
		}

	private:
		static std::unique_ptr<spdlog::logger> s_CoreLogger;
	};
}

/// Core log macros
#define KBR_CORE_TRACE(...)    ::kbr::Log::GetCoreLogger()->trace(__VA_ARGS__)
#define KBR_CORE_INFO(...)     ::kbr::Log::GetCoreLogger()->info(__VA_ARGS__)
#define KBR_CORE_WARN(...)     ::kbr::Log::GetCoreLogger()->warn(__VA_ARGS__)
#define KBR_CORE_ERROR(...)    ::kbr::Log::GetCoreLogger()->error(__VA_ARGS__)
#define KBR_CORE_CRITICAL(...) ::kbr::Log::GetCoreLogger()->critical(__VA_ARGS__)


template<>
struct fmt::formatter<glm::vec2> : fmt::formatter<std::string>
{
	static auto format(glm::vec2 vec, const format_context& ctx) -> decltype(ctx.out())
	{
		return fmt::format_to(ctx.out(), "({}, {})", vec.x, vec.y);
	}
};

template<>
struct fmt::formatter<glm::vec3> : fmt::formatter<std::string>
{
	static auto format(glm::vec3 vec, const format_context& ctx) -> decltype(ctx.out())
	{
		return fmt::format_to(ctx.out(), "({}, {}, {})", vec.x, vec.y, vec.z);
	}
};

template<>
struct fmt::formatter<glm::vec4> : fmt::formatter<std::string>
{
	static auto format(glm::vec4 vec, const format_context& ctx) -> decltype(ctx.out())
	{
		return fmt::format_to(ctx.out(), "({}, {}, {}, {})", vec.x, vec.y, vec.z, vec.w);
	}
};

template<>
struct fmt::formatter<std::filesystem::path> : fmt::formatter<std::string>
{
	static auto format(const std::filesystem::path& path, const format_context& ctx) -> decltype(ctx.out())
	{
		return fmt::format_to(ctx.out(), "{}", path.string());
	}
};