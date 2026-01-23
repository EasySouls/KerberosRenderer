#include "Log.hpp"

#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/sinks/basic_file_sink.h"

namespace kbr
{
	std::unique_ptr<spdlog::logger> Log::s_CoreLogger;

	void Log::Init()
	{
		// [Timestamp] [name of logger]: [message]
		spdlog::set_pattern("%^[%T] %n: %v%$");

		auto coreConsoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
		coreConsoleSink->set_level(spdlog::level::trace);
		auto coreFileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("KerberosRenderer.log", true);
		coreFileSink->set_level(spdlog::level::trace);

		s_CoreLogger = std::make_unique<spdlog::logger>(spdlog::logger("CORE", { coreConsoleSink, coreFileSink }));
		s_CoreLogger->set_level(spdlog::level::info);
	}
}