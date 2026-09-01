#pragma once

#include "Core/Core.hpp"

#include <string>
#include <string_view>
#include <chrono>
#include <fstream>
#include <memory>
#include <mutex>

namespace Kerberos {

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
    Instrumentor();

	void BeginSession(const std::string& name, const std::string& filepath = "results.json");
    void EndSession();

	void WriteProfile(const ProfileResult& result);
	void WriteHeader();
	void WriteFooter();

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
    explicit InstrumentationTimer(const char* name);
	~InstrumentationTimer();

	void Stop();

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