#pragma once

#include "Event.hpp"

namespace Kerberos
{
	class KeyTypedEvent : public Event
	{
	public:
		explicit KeyTypedEvent(const char keyChar)
			: m_KeyChar(keyChar) 
		{
		}

		char GetKeyChar() const { return m_KeyChar; }

		std::string ToString() const override
		{
			return std::string(GetName()) + ": " + m_KeyChar;
		}

		EVENT_CLASS_TYPE(KeyTyped)
		EVENT_CLASS_CATEGORY(EventCategoryKeyboard | EventCategoryInput)

	private:
		char m_KeyChar;
	};
}
