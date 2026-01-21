#pragma once 

#include "Event.hpp"

namespace kbr
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

		const char* GetName() const override { return "KeyPressedEvent"; }
		int GetCategoryFlags() const override { return EventCategoryKeyboard | EventCategoryInput; }

		std::string ToString() const override
		{
			return "KeyPressedEvent: " + std::to_string(m_KeyCode) + " (repeats: " + std::to_string(m_RepeatCount) + ")";
		}

	private:
		int m_KeyCode;
		int m_RepeatCount;
	};
}