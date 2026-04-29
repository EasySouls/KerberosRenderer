#include "kbrpch.hpp"
#include "Log.hpp"

#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/sinks/basic_file_sink.h"

namespace Kerberos
{
	std::unique_ptr<spdlog::logger> Log::s_CoreLogger = nullptr;
	std::unique_ptr<spdlog::logger> Log::s_EditorLogger = nullptr;

	std::shared_ptr<EditorSinkMultithreaded> Log::s_EditorSink = nullptr;

	void Log::Init()
	{
		// [Timestamp] [name of logger]: [message]
		spdlog::set_pattern("%^[%T] %n: %v%$\n");

		s_EditorSink = std::make_shared<EditorSinkMultithreaded>();
		s_EditorSink->set_pattern("[%T] %n: %v");

		auto coreConsoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
		coreConsoleSink->set_level(spdlog::level::trace);
		auto coreFileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("KerberosRenderer.log", true);
		coreFileSink->set_level(spdlog::level::trace);

		s_CoreLogger = std::make_unique<spdlog::logger>(spdlog::logger("CORE", { coreConsoleSink, coreFileSink, s_EditorSink }));
		s_CoreLogger->set_level(spdlog::level::info);

		auto editorConsoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
		editorConsoleSink->set_level(spdlog::level::trace);
		auto editorFileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("KerberosRenderer.log", true);
		editorFileSink->set_level(spdlog::level::trace);

		s_EditorLogger = std::make_unique<spdlog::logger>(spdlog::logger("EDITOR", { editorConsoleSink, editorFileSink, s_EditorSink }));
		s_EditorLogger->set_level(spdlog::level::info);

		KBR_CORE_INFO("Logger initialized");
	}

	void Log::Shutdown()
	{
		KBR_CORE_ASSERT(s_CoreLogger && s_EditorLogger, "Loggers have been destructed before calling Log::Shutdown");

		s_CoreLogger.reset();
		s_EditorLogger.reset();
	}
}