#pragma once

#include "Event.hpp"

namespace kbr
{
	class MouseScrolledEvent : public Event
	{
	public:
		MouseScrolledEvent(const double xOffset, const double yOffset)
			: m_XOffset(xOffset), m_YOffset(yOffset)
		{
		}

		double GetXOffset() const { return m_XOffset; }
		double GetYOffset() const { return m_YOffset; }

		const char* GetName() const override { return "MouseScrolledEvent"; }
		int GetCategoryFlags() const override { return EventCategoryMouse | EventCategoryInput; }

		std::string ToString() const override
		{
			return std::string(GetName()) + ": " + std::to_string(m_XOffset) + ", " + std::to_string(m_YOffset);
		}

	private:
		double m_XOffset;
		double m_YOffset;
	};
}