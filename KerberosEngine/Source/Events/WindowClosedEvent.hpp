#pragma once

#include "Event.hpp"

namespace Kerberos
{
	class WindowClosedEvent : public Event
	{
	public:
		WindowClosedEvent() = default;

		EVENT_CLASS_TYPE(WindowClose)
		EVENT_CLASS_CATEGORY(EventCategoryApplication)
	};
}