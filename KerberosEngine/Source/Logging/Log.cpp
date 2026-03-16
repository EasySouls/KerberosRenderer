#include "kbrpch.hpp"
#include "Log.hpp"

#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/sinks/basic_file_sink.h"

namespace Kerberos
{
	std::unique_ptr<spdlog::logger> Log::s_CoreLogger;
	std::unique_ptr<spdlog::logger> Log::s_EditorLogger;

	void Log::Init()
	{
		// [Timestamp] [name of logger]: [message]
		spdlog::set_pattern("%^[%T] %n: %v%$\n");

		auto coreConsoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
		coreConsoleSink->set_level(spdlog::level::trace);
		auto coreFileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("KerberosRenderer.log", true);
		coreFileSink->set_level(spdlog::level::trace);

		s_CoreLogger = std::make_unique<spdlog::logger>(spdlog::logger("CORE", { coreConsoleSink, coreFileSink }));
		s_CoreLogger->set_level(spdlog::level::info);

		auto editorConsoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
		editorConsoleSink->set_level(spdlog::level::trace);
		auto editorFileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("KerberosRenderer.log", true);
		editorFileSink->set_level(spdlog::level::trace);

		s_EditorLogger = std::make_unique<spdlog::logger>(spdlog::logger("EDITOR", { editorConsoleSink, editorFileSink }));
		s_EditorLogger->set_level(spdlog::level::info);
	}
}