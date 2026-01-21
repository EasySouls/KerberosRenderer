#pragma once 

#include "Event.hpp"

namespace kbr
{
	class MouseButtonPressedEvent : public Event
	{
	public:
		explicit MouseButtonPressedEvent(const int button)
			: m_Button(button) {}

		int GetButton() const { return m_Button; }

		const char* GetName() const override { return "MouseButtonPressedEvent"; }
		int GetCategoryFlags() const override { return EventCategoryMouse | EventCategoryInput; }

		std::string ToString() const override
		{
			return std::string(GetName()) + ": " + std::to_string(m_Button);
		}

	private:
		int m_Button;
	};
}