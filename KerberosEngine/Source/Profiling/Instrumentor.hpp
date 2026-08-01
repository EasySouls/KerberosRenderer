#pragma once

#include <string>
#include <string_view>
#include <chrono>
#include <algorithm>
#include <fstream>
#include <thread>
#include <memory>
#include <mutex>

namespace Kerberos
{
	struct ProfileResult
	{
		std::string_view Name;
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
			std::lock_guard lock(m_Mutex);

			m_OutputStream.open(filepath);
			if (!m_OutputStream.is_open())
			{
				KBR_CORE_ERROR("Failed to open profiling output file '{0}'", filepath);
				return;
			}

			WriteHeader();

			m_CurrentSession = CreateOwner<InstrumentationSession>(name);
		}

		void EndSession()
		{
			std::lock_guard lock(m_Mutex);
			if (!m_CurrentSession)
				return;

			WriteFooter();

			m_OutputStream.close();
			m_CurrentSession.reset();
			m_ProfileCount = 0;
		}

		void WriteProfile(const ProfileResult& result)
		{
			if (m_ProfileCount++ > 0)
				m_OutputStream << ",";

			std::string name{ result.Name };
			std::ranges::replace(name, '"', '\'');

			std::lock_guard lock(m_Mutex);

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
		Owner<InstrumentationSession> m_CurrentSession;
		std::ofstream m_OutputStream;
		int m_ProfileCount;
		std::mutex m_Mutex{};
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

#if defined(__GNUC__) || (defined(__clang__))
#define KBR_FUNC_SIG __PRETTY_FUNCTION__
#elif defined(_MSC_VER)
#define KBR_FUNC_SIG __FUNCSIG__
#else
#define KBR_FUNC_SIG __func__
#endif

#define KBR_PROFILE_COMBINE_INNER(A, B) A##B
#define KBR_PROFILE_COMBINE(A, B) KBR_PROFILE_COMBINE_INNER(A, B)

#ifdef KBR_PROFILE
#define KBR_PROFILE_BEGIN_SESSION(name, filepath) ::Kerberos::Instrumentor::Get().BeginSession(name, filepath)
#define KBR_PROFILE_END_SESSION() ::Kerberos::Instrumentor::Get().EndSession()
#define KBR_PROFILE_SCOPE(name) ::Kerberos::InstrumentationTimer KBR_PROFILE_COMBINE(timer, __LINE__)(name)
#define KBR_PROFILE_FUNCTION() KBR_PROFILE_SCOPE(KBR_FUNC_SIG)
#else
#define KBR_PROFILE_BEGIN_SESSION(name, filepath)
#define KBR_PROFILE_END_SESSION()
#define KBR_PROFILE_SCOPE(name)
#define KBR_PROFILE_FUNCTION()
#endif