#pragma once 

#include "Event.hpp"

namespace Kerberos
{
	class KeyPressedEvent : public Event
	{
	public:
		KeyPressedEvent(const int keyCode, const int repeatCount)
			: m_KeyCode(keyCode), m_RepeatCount(repeatCount)
		{
		}

		int GetKeyCode() const { return m_KeyCode; }
		int GetRepeatCount() const { return m_RepeatCount; }

		std::string ToString() const override
		{
			return "KeyPressedEvent: " + std::to_string(m_KeyCode) + " (repeats: " + std::to_string(m_RepeatCount) + ")";
		}

		EVENT_CLASS_TYPE(KeyPressed)
		EVENT_CLASS_CATEGORY(EventCategoryKeyboard | EventCategoryInput)

	private:
		int m_KeyCode;
		int m_RepeatCount;
	};
}