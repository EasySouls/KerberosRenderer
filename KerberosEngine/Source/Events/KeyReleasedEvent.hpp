#pragma once

#include "Event.hpp"

namespace Kerberos
{
	class KeyReleasedEvent : public Event
	{
	public:
		explicit KeyReleasedEvent(const char keyChar)
			: m_KeyChar(keyChar) 
		{
		}

		char GetKeyChar() const { return m_KeyChar; }

		std::string ToString() const override
		{
			return std::string(GetName()) + ": " + m_KeyChar;
		}

		EVENT_CLASS_TYPE(KeyReleased)
		EVENT_CLASS_CATEGORY(EventCategoryKeyboard | EventCategoryInput)

	private:
		char m_KeyChar;
	};
}