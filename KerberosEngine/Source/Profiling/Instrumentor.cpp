#include "Instrumentor.hpp"

#include <algorithm>
#include <thread>

import Kerberos;

namespace Kerberos {

Instrumentor::Instrumentor() : m_CurrentSession(nullptr), m_ProfileCount(0) {}

void Instrumentor::BeginSession(const std::string& name, const std::string& filepath)
{
    std::lock_guard lock(m_Mutex);

    m_OutputStream.open(filepath);
    if (!m_OutputStream.is_open()) 
    {
        Log::CoreError("Failed to open profiling output file '{0}'", filepath);
        return;
    }

    WriteHeader();

    m_CurrentSession = CreateOwner<InstrumentationSession>(name);
}

void Instrumentor::EndSession()
{
    std::lock_guard lock(m_Mutex);
    if (!m_CurrentSession)
        return;

    WriteFooter();

    m_OutputStream.close();

    if (m_OutputStream.fail())
    {
        Log::CoreError("Failed to end profiling session.");
    }

    m_CurrentSession.reset();
    m_ProfileCount = 0;
}

void Instrumentor::WriteProfile(const ProfileResult& result) 
{
    std::lock_guard lock(m_Mutex);

    if (m_OutputStream.fail())
    {
        Log::CoreError("Failed to write profiling data.");
        return;
    }

    if (m_ProfileCount++ > 0)
        m_OutputStream << ",";

    std::string name{ result.Name };
    std::ranges::replace(name, '"', '\'');

    m_OutputStream << "{";
    m_OutputStream << R"("cat":"function",)";
    m_OutputStream << "\"dur\":" << (result.End - result.Start) << ',';
    m_OutputStream << R"("name":")" << name << "\",";
    m_OutputStream << R"("ph":"X",)";
    m_OutputStream << "\"pid\":0,";
    m_OutputStream << "\"tid\":" << result.ThreadID << ",";
    m_OutputStream << "\"ts\":" << result.Start;
    m_OutputStream << "}";
}

void Instrumentor::WriteHeader()
{
    m_OutputStream << R"({"otherData": {},"traceEvents":[)";
    m_OutputStream.flush();
}

void Instrumentor::WriteFooter()
{
    m_OutputStream << "]}";
    m_OutputStream.flush();
}

InstrumentationTimer::InstrumentationTimer(const char* name) 
    : m_Name(name), m_Stopped(false)
{
    m_StartTimepoint = std::chrono::high_resolution_clock::now();
}

InstrumentationTimer ::~InstrumentationTimer()
{
    if (!m_Stopped)
        Stop();
}

void InstrumentationTimer::Stop()
{
    const auto endTimepoint = std::chrono::high_resolution_clock::now();

    const long long start =
        std::chrono::time_point_cast<std::chrono::microseconds>(m_StartTimepoint).time_since_epoch().count();
    const long long end =
        std::chrono::time_point_cast<std::chrono::microseconds>(endTimepoint).time_since_epoch().count();

    const uint32_t threadID = static_cast<uint32_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
    Instrumentor::Get().WriteProfile({ .Name = m_Name, .Start = start, .End = end, .ThreadID = threadID });

    m_Stopped = true;
}

}