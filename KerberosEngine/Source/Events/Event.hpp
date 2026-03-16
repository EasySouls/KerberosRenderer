#pragma once

#include <cstdint>
#include <string>
#include <functional>
#include <ostream>

namespace Kerberos
{
	enum class EventType
	{
		None = 0,
		WindowClose, WindowResize, WindowFocus, WindowLostFocus, WindowMoved, WindowDrop,
		AppTick, AppUpdate, AppRender,
		KeyPressed, KeyReleased, KeyTyped,
		MouseButtonPressed, MouseButtonReleased, MouseMoved, MouseScrolled
	};

	enum EventCategory : uint8_t
	{
		EventCategoryApplication = 1 << 0,
		EventCategoryInput = 1 << 1,
		EventCategoryKeyboard = 1 << 2,
		EventCategoryMouse = 1 << 3,
	};

#define EVENT_CLASS_TYPE(type) static EventType GetStaticType() { return EventType::##type; }\
								virtual EventType GetEventType() const override { return GetStaticType(); }\
								virtual const char* GetName() const override { return #type; }

#define EVENT_CLASS_CATEGORY(category) virtual int GetCategoryFlags() const override { return category; }

	class Event
	{
	public:
		virtual ~Event() = default;

		virtual const char* GetName() const = 0;
		virtual EventType GetEventType() const = 0;
		virtual int GetCategoryFlags() const = 0;

		virtual std::string ToString() const
		{
			return GetName();
		}

		bool IsInCategory(const int category) const
		{
			return GetCategoryFlags() & category;
		}

		bool Handled = false;
	};

	template<typename T>
	constexpr bool IsEventType = std::is_base_of_v<Event, T>;

	class EventDispatcher
	{
	public:
		template<typename T> 
			requires IsEventType<T>
		using EventFn = std::function<bool(T&)>;

		explicit EventDispatcher(Event& event)
			: m_EventRef(event)
		{
		}

		template<typename T> 
			requires IsEventType<T>
		bool Dispatch(EventFn<T> fn)
		{
			if (m_EventRef.GetEventType() == T::GetStaticType())
			{
				m_EventRef.Handled = fn(*static_cast<T*>(&m_EventRef));
				return true;
			}
			return false;
		}

	private:
		Event& m_EventRef;
	};

	inline std::ostream& operator<<(std::ostream& os, const Event& e)
	{
		return os << e.ToString();
	}
}
