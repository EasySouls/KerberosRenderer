#pragma once

#include "Event.hpp"

namespace Kerberos
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

		std::string ToString() const override
		{
			return std::string(GetName()) + ": " + std::to_string(m_XOffset) + ", " + std::to_string(m_YOffset);
		}

		EVENT_CLASS_TYPE(MouseScrolled)
		EVENT_CLASS_CATEGORY(EventCategoryMouse | EventCategoryInput)

	private:
		double m_XOffset;
		double m_YOffset;
	};
}