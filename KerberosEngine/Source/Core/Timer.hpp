#pragma once

#include <chrono>
#include <utility>

namespace Kerberos
{
	struct TimerData
	{
		const char* Name;
		float DurationMs;
	};

	template<typename Fn>
	class Timer
	{
	public:
		explicit Timer(const char* name, Fn&& callback)
			: m_Name(name), m_Callback(std::move(callback)), m_Stopped(false)
		{
			m_StartTimepoint = std::chrono::high_resolution_clock::now();
		}

		~Timer()
		{
			if (!m_Stopped)
				Stop();
		}

		void Stop()
		{
			const auto endTimepoint = std::chrono::high_resolution_clock::now();

			const auto start = std::chrono::time_point_cast<std::chrono::microseconds>(m_StartTimepoint).time_since_epoch().count();
			const auto end = std::chrono::time_point_cast<std::chrono::microseconds>(endTimepoint).time_since_epoch().count();

			m_Stopped = true;

			const auto duration = end - start;
			auto durationMs = duration * 0.001f;

			m_Callback({ m_Name, durationMs });
		}

	private:
		const char* m_Name;
		Fn m_Callback;

		std::chrono::time_point<std::chrono::high_resolution_clock> m_StartTimepoint;
		bool m_Stopped;
	};
}