#pragma once

#include "Notification.hpp"

#include <glm/glm.hpp>

#include <vector>
#include <string>

namespace Kerberos
{
	class NotificationManager
	{
	public:
		NotificationManager() = default;

		void AddNotification(const std::string& message, Notification::Type type, float duration = 10.0f, const glm::vec4& bgColor = { 0.0f, 0.0f, 0.0f, 1.0f }, const glm::vec4& textColor = { 1.0f, 1.0f, 1.0f, 1.0f });
		void RenderNotifications();

	private:
		std::vector<Notification> m_Notifications;
		uint32_t m_NextId = 0;
	};
}
