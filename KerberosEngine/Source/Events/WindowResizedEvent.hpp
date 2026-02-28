#pragma once

#include "Event.hpp"

namespace Kerberos 
{
	class WindowResizedEvent : public Event
	{
	public:
		WindowResizedEvent(const uint32_t width, const uint32_t height)
			: m_Width(width), m_Height(height) 
		{
		}

		uint32_t GetWidth() const { return m_Width; }
		uint32_t GetHeight() const { return m_Height; }

		const char* GetName() const override { return "WindowResizedEvent"; }
		int GetCategoryFlags() const override { return EventCategoryApplication; }

		std::string ToString() const override
		{
			return std::string(GetName()) + ": " + std::to_string(m_Width) + "x" + std::to_string(m_Height);
		}

	private:
		uint32_t m_Width;
		uint32_t m_Height;
	};
}