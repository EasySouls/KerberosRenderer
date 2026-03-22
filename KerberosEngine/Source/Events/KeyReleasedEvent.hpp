#pragma once

#include "Event.hpp"

namespace Kerberos
{
	class KeyReleasedEvent : public Event
	{
	public:
		explicit KeyReleasedEvent(const int keyCode)
			: m_KeyCode(keyCode) 
		{
		}

		int GetKeyCode() const { return m_KeyCode; }

		std::string ToString() const override
		{
			return std::string(GetName()) + ": " + std::to_string(m_KeyCode);
		}

		EVENT_CLASS_TYPE(KeyReleased)
		EVENT_CLASS_CATEGORY(EventCategoryKeyboard | EventCategoryInput)

	private:
		int m_KeyCode;
	};
}