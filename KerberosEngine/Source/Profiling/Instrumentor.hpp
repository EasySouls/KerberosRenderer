#pragma once

#include <string>
#include <chrono>
#include <algorithm>
#include <fstream>
#include <thread>

namespace Kerberos
{
	struct ProfileResult
	{
		std::string Name;
		long long Start, End;
		uint32_t ThreadID;
	};

	struct InstrumentationSession
	{
		std::string Name;
	};

	class Instrumentor
	{
	public:
		Instrumentor()
			: m_CurrentSession(nullptr), m_ProfileCount(0)
		{
		}

		void BeginSession(const std::string& name, const std::string& filepath = "results.json")
		{
			m_OutputStream.open(filepath);
			WriteHeader();
			m_CurrentSession = new InstrumentationSession({ name });
		}

		void EndSession()
		{
			WriteFooter();
			m_OutputStream.close();
			delete m_CurrentSession;
			m_CurrentSession = nullptr;
			m_ProfileCount = 0;
		}

		void WriteProfile(const ProfileResult& result)
		{
			if (m_ProfileCount++ > 0)
				m_OutputStream << ",";

			std::string name = result.Name;
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

			m_OutputStream.flush();
		}

		void WriteHeader()
		{
			m_OutputStream << R"({"otherData": {},"traceEvents":[)";
			m_OutputStream.flush();
		}

		void WriteFooter()
		{
			m_OutputStream << "]}";
			m_OutputStream.flush();
		}

		static Instrumentor& Get()
		{
			static Instrumentor instance;
			return instance;
		}

	private:
		InstrumentationSession* m_CurrentSession;
		std::ofstream m_OutputStream;
		int m_ProfileCount;
	};

	class InstrumentationTimer
	{
	public:
		explicit InstrumentationTimer(const char* name)
			: m_Name(name), m_Stopped(false)
		{
			m_StartTimepoint = std::chrono::high_resolution_clock::now();
		}

		~InstrumentationTimer()
		{
			if (!m_Stopped)
				Stop();
		}

		void Stop()
		{
			const auto endTimepoint = std::chrono::high_resolution_clock::now();

			const long long start = std::chrono::time_point_cast<std::chrono::microseconds>(m_StartTimepoint).time_since_epoch().count();
			const long long end = std::chrono::time_point_cast<std::chrono::microseconds>(endTimepoint).time_since_epoch().count();

			const uint32_t threadID = static_cast<uint32_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
			Instrumentor::Get().WriteProfile({ .Name = m_Name, .Start = start, .End = end, .ThreadID = threadID });

			m_Stopped = true;
		}

	private:
		const char* m_Name;
		std::chrono::time_point<std::chrono::high_resolution_clock> m_StartTimepoint;
		bool m_Stopped;
	};
}

#ifdef KBR_PROFILE
#define KBR_PROFILE_BEGIN_SESSION(name, filepath) ::Kerberos::Instrumentor::Get().BeginSession(name, filepath)
#define KBR_PROFILE_END_SESSION() ::Kerberos::Instrumentor::Get().EndSession()
#define KBR_PROFILE_SCOPE(name) ::Kerberos::InstrumentationTimer timer##__LINE__(name)
#define KBR_PROFILE_FUNCTION() KBR_PROFILE_SCOPE(__FUNCSIG__)
#else
#define KBR_PROFILE_BEGIN_SESSION(name, filepath)
#define KBR_PROFILE_END_SESSION()
#define KBR_PROFILE_SCOPE(name)
#define KBR_PROFILE_FUNCTION()
#endif