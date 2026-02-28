#pragma once

#include "Event.hpp"

namespace Kerberos
{
	class MouseButtonReleasedEvent : public Event
	{
	public:
		explicit MouseButtonReleasedEvent(const int button)
			: m_Button(button)
		{
		}

		int GetButton() const { return m_Button; }

		const char* GetName() const override { return "MouseButtonReleasedEvent"; }
		int GetCategoryFlags() const override { return EventCategoryMouse | EventCategoryInput; }

		std::string ToString() const override
		{
			return std::string(GetName()) + ": " + std::to_string(m_Button);
		}

	private:
		int m_Button;
	};
}