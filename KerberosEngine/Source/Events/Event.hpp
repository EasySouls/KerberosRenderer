#pragma once

#include <cstdint>
#include <string>
#include <ostream>

namespace Kerberos
{
	enum EventCategory : uint8_t
	{
		EventCategoryApplication = 1 << 0,
		EventCategoryInput = 1 << 1,
		EventCategoryKeyboard = 1 << 2,
		EventCategoryMouse = 1 << 3,
	};

	class Event
	{
	public:
		virtual ~Event() = default;

		virtual const char* GetName() const = 0;
		virtual int GetCategoryFlags() const = 0;

		virtual std::string ToString() const = 0;

		bool IsInCategory(const int category) const
		{
			return GetCategoryFlags() & category;
		}
	};

	inline std::ostream& operator<<(std::ostream& os, const Event& e)
	{
		return os << e.ToString();
	}
}
