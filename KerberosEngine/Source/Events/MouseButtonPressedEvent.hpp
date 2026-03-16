#pragma once 

#include "Event.hpp"

namespace Kerberos
{
	class MouseButtonPressedEvent : public Event
	{
	public:
		explicit MouseButtonPressedEvent(const int button)
			: m_Button(button) 
		{
		}

		int GetButton() const { return m_Button; }

		std::string ToString() const override
		{
			return std::string(GetName()) + ": " + std::to_string(m_Button);
		}

		EVENT_CLASS_TYPE(MouseButtonPressed)
		EVENT_CLASS_CATEGORY(EventCategoryMouse | EventCategoryInput)

	private:
		int m_Button;
	};
}