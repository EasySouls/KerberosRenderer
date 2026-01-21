#pragma once

#include "Event.hpp"

namespace kbr
{
	class MouseMovedEvent : public Event
	{
	public:
		MouseMovedEvent(const float x, const float y)
			: m_MouseX(x), m_MouseY(y) 
		{
		}

		float GetX() const { return m_MouseX; }
		float GetY() const { return m_MouseY; }

		const char* GetName() const override { return "MouseMovedEvent"; }
		int GetCategoryFlags() const override { return EventCategoryMouse | EventCategoryInput; }

		std::string ToString() const override
		{
			return std::string(GetName()) + ": " + std::to_string(m_MouseX) + ", " + std::to_string(m_MouseY);
		}

	private:
		float m_MouseX;
		float m_MouseY;
	};
}