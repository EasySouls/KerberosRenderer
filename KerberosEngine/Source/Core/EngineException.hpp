#pragma once

#include <exception>
#include <string>
#include <format>
#include <source_location>
#include <stacktrace>

namespace Kerberos {

class EngineException : public std::exception
{
public:
    explicit EngineException(std::string message,
                             const std::source_location& location = std::source_location::current(), 
                             std::stacktrace stacktrace = std::stacktrace::current())
                                 : m_RawMessage(std::move(message)), m_Location(location), m_Stacktrace(std::move(stacktrace))
    {
        m_FormattedMessage = std::format("Exception thrown at {}:{} in function {}: {}\n\nStacktrace:\n{}",
                                         m_Location.file_name(),
                                         m_Location.line(),
                                         m_Location.function_name(),
                                         m_RawMessage,
                                         std::to_string(m_Stacktrace));
    }

    [[nodiscard]] const char* what() const noexcept override
    {
        return m_FormattedMessage.c_str();
    }

    [[nodiscard]] const std::source_location& location() const noexcept
    {
        return m_Location;
    }
    [[nodiscard]] const std::stacktrace& stacktrace() const noexcept
    {
        return m_Stacktrace;
    }
    [[nodiscard]] const std::string& message() const noexcept
    {
        return m_RawMessage;
    }

private: 
    std::string m_RawMessage {};
    std::source_location m_Location;
    std::stacktrace m_Stacktrace;
    std::string m_FormattedMessage;
};

class ResourceException : public EngineException
{
public:
    using EngineException::EngineException;
};

}