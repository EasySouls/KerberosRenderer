#pragma once

#include <glm/glm.hpp>

#include <string>
#include <cstdint>

#include "imgui/imgui.h"


namespace Kerberos
{
	struct Notification
	{
		enum class Type : uint8_t
		{
			Info,
			Warning,
			Error
		};

		Type type;
		std::string message;
		glm::vec4 color;
		glm::vec4 textColor;
		double startTime = 0.0;
		double duration = 5.0; /// Default duration in seconds
		float currentAlpha = 1.0f;
		bool isHovered = false;
		uint32_t id = 0; /// Unique identifier for the notification

		Notification(const Type type, std::string message, const float duration, const uint32_t uniqueId, const glm::vec4& bgColor = { 0.0f, 0.0f, 0.0f, 1.0f }, const glm::vec4& textColor = { 1.0f, 1.0f, 1.0f, 1.0f })
			: type(type), message(std::move(message)), color(bgColor), textColor(textColor), startTime(ImGui::GetTime()), duration(duration), id(uniqueId) {
		}
	};
}
