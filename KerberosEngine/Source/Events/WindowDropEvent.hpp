#pragma once

#include <filesystem>

#include "Event.hpp"

namespace Kerberos
{
	class WindowDropEvent : public Event
	{
	public:
		explicit WindowDropEvent(std::vector<std::filesystem::path> filepaths)
			: m_Filepaths(std::move(filepaths))
		{
		}

		const std::vector<std::filesystem::path>& GetFilepaths() const { return m_Filepaths; }

		std::string ToString() const override
		{
			std::string result = std::string(GetName()) + ": ";
			for (const auto& path : m_Filepaths)
			{
				result += path.string() + " ";
			}
			return result;
		}

		EVENT_CLASS_TYPE(WindowDrop)
		EVENT_CLASS_CATEGORY(EventCategoryApplication)

	private:
		std::vector<std::filesystem::path> m_Filepaths;
	};
}