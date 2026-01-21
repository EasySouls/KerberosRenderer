#pragma once

#include "Event.hpp"

namespace kbr
{
	class KeyReleasedEvent : public Event
	{
	public:
		explicit KeyReleasedEvent(const char keyChar)
			: m_KeyChar(keyChar) 
		{
		}

		char GetKeyChar() const { return m_KeyChar; }

		const char* GetName() const override { return "KeyReleasedEvent"; }
		int GetCategoryFlags() const override { return EventCategoryKeyboard | EventCategoryInput; }

		std::string ToString() const override
		{
			return std::string(GetName()) + ": " + m_KeyChar;
		}

	private:
		char m_KeyChar;
	};
}