module;

#include "Logging/EditorSink.hpp"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

#include <filesystem>
#include <format>
#include <memory>
#include <source_location>
#include <string>
#include <string_view>
#include <utility>

module Kerberos;

namespace Kerberos::Log {

namespace {

std::unique_ptr<spdlog::logger> s_CoreLogger = nullptr;
std::unique_ptr<spdlog::logger> s_EditorLogger = nullptr;
std::shared_ptr<EditorSinkMultithreaded> s_EditorSink = nullptr;

spdlog::level::level_enum ToSpdlogLevel(const Level level)
{
    switch (level)
    {
    case Level::Trace:
        return spdlog::level::trace;
    case Level::Debug:
        return spdlog::level::debug;
    case Level::Info:
        return spdlog::level::info;
    case Level::Warn:
        return spdlog::level::warn;
    case Level::Error:
        return spdlog::level::err;
    case Level::Critical:
        return spdlog::level::critical;
    }
    return spdlog::level::info;
}

}

void Init()
{
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

	LogMessage(true, Level::Info, std::source_location::current(), "Logger initialized");
}

void Shutdown()
{
	s_CoreLogger.reset();
	s_EditorLogger.reset();
}

void LogMessage(const bool isCore, const Level level, const std::source_location& loc, std::string_view message)
{
    const auto& logger = isCore ? s_CoreLogger : s_EditorLogger;
    if (!logger)
        return;

    std::string_view filename = loc.file_name();
    const auto lastSlash = filename.find_last_of("/\\");
    if (lastSlash != std::string_view::npos) {
        filename.remove_prefix(lastSlash + 1);
    }

    const std::string fullMsg = std::format("[{}:{}] {}", filename, loc.line(), message);
    logger->log(ToSpdlogLevel(level), fullMsg);
}

void SetCoreLogLevel(const Level level)
{
    s_CoreLogger->set_level(ToSpdlogLevel(level));
}

void SetEditorLogLevel(const Level level)
{
    s_EditorLogger->set_level(ToSpdlogLevel(level));
}

void SetLogLevel(const Level level)
{
    SetCoreLogLevel(level);
    SetEditorLogLevel(level);
}

void Flush()
{
    s_CoreLogger->flush();
    s_EditorLogger->flush();
}

std::unique_ptr<spdlog::logger>& GetCoreLogger()
{
    return s_CoreLogger;
}
std::unique_ptr<spdlog::logger>& GetEditorLogger()
{
    return s_EditorLogger;
}
std::shared_ptr<EditorSinkMultithreaded>& GetEditorSink()
{
    return s_EditorSink;
}

}