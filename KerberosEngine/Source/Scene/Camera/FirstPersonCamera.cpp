#include "kbrpch.hpp"
#include "FirstPersonCamera.hpp"

#include <algorithm>

#include "events/MouseButtonPressedEvent.hpp"
#include "events/MouseButtonReleasedEvent.hpp"
#include "Input/InputSystem.hpp"

namespace Kerberos
{
	FirstPersonCamera::FirstPersonCamera(const float fov, const float aspectRatio, const float nearClip, const float farClip)
		: m_Fov(fov), m_AspectRatio(aspectRatio), m_NearClip(nearClip), m_FarClip(farClip)
	{
		UpdateProjection();
		UpdateView();
	}

	void FirstPersonCamera::OnUpdate(const float deltaTime)
	{
		const bool forward = Input::IsKeyPressed(Key::W);
		const bool backward = Input::IsKeyPressed(Key::S);
		const bool left = Input::IsKeyPressed(Key::A);
		const bool right = Input::IsKeyPressed(Key::D);

		if (forward || backward || left || right)
		{
			const glm::vec3 forwardDir = GetForward();
			const glm::vec3 rightDir = GetRight();

			glm::vec3 movement(0.0f);
			if (forward)
				movement += forwardDir;
			if (backward)
				movement -= forwardDir;
			if (left)
				movement -= rightDir;
			if (right)
				movement += rightDir;

			if (glm::length(movement) > 0.0f)
			{
				movement = glm::normalize(movement);
				m_Position += movement * m_MoveSpeed * deltaTime;
				m_ViewDirty = true;
			}
		}

		if (m_ViewDirty)
		{
			UpdateView();
		}
		if (m_ProjectionDirty)
		{
			UpdateProjection();
		}
	}

	void FirstPersonCamera::OnEvent(const std::shared_ptr<Event>& event)
	{
		if (const auto mouseButtonPressedEvent = std::dynamic_pointer_cast<MouseButtonPressedEvent>(event))
		{
			if (mouseButtonPressedEvent->GetButton() == Mouse::Button1)
			{
				//Input::SetCursorMode(CursorMode::Locked);
				m_CanLookAround = true;
			}
		}
		else if (const auto mouseButtonReleasedEvent = std::dynamic_pointer_cast<MouseButtonReleasedEvent>(event))
		{
			if (mouseButtonReleasedEvent->GetButton() == Mouse::Button1)
			{
				//Input::SetCursorMode(CursorMode::Normal);
				m_CanLookAround = false;
			}
		}
		else if (const auto mouseMovedEvent = std::dynamic_pointer_cast<MouseMovedEvent>(event))
		{
			const int32_t x = static_cast<int32_t>(mouseMovedEvent->GetX());
			const int32_t y = static_cast<int32_t>(mouseMovedEvent->GetY());

			const int32_t dx = static_cast<int32_t>(m_MousePosition.x) - x;
			const int32_t dy = static_cast<int32_t>(m_MousePosition.y) - y;

			m_MousePosition.x = static_cast<float>(x);
			m_MousePosition.y = static_cast<float>(y);

			if (!m_CanLookAround)
				return;

			constexpr float rotationSpeed = 0.3f;
			m_Yaw -= static_cast<float>(dx) * rotationSpeed;
			m_Pitch += static_cast<float>(dy) * rotationSpeed;
			m_Pitch = std::min(m_Pitch, 89.0f);
			m_Pitch = std::max(m_Pitch, -89.0f);
			m_ViewDirty = true;
		}
	}

	void FirstPersonCamera::SetPosition(const glm::vec3& position)
	{
		m_Position = position;
		m_ViewDirty = true;
	}

	void FirstPersonCamera::SetRotation(const glm::vec3& rotation) 
	{
		m_Pitch = rotation.x;
		m_Yaw = rotation.y;

		m_ViewDirty = true;
	}

	void FirstPersonCamera::Rotate(const float pitch, const float yaw) 
	{
		m_Pitch += pitch;
		m_Yaw += yaw;

		m_ViewDirty = true;
	}

	void FirstPersonCamera::Rotate(const glm::vec3& axis, const float angle) 
	{
		m_Pitch += axis.x * angle;
		m_Yaw += axis.y * angle;

		m_ViewDirty = true;
	}

	float FirstPersonCamera::GetDistance() const 
	{
		return 0.0f;
	}

	void FirstPersonCamera::SetDistance(const float distance)
	{
	}

	void FirstPersonCamera::Focus(const glm::vec3& focusPoint)
	{
	}

	void FirstPersonCamera::SetViewportSize(const float width, const float height)
	{
		m_AspectRatio = width / height;
		m_ProjectionDirty = true;
		UpdateProjection();
	}

	void FirstPersonCamera::SetFlipY(const bool flip) 
	{
		m_FlipY = flip;

		m_ProjectionDirty = true;
	}

	const glm::mat4& FirstPersonCamera::GetViewMatrix() const
	{
		return m_ViewMatrix;
	}

	const glm::mat4& FirstPersonCamera::GetProjectionMatrix() const
	{
		return m_ProjectionMatrix;
	}

	glm::mat4 FirstPersonCamera::GetViewProjectionMatrix() const
	{
		return m_ProjectionMatrix * m_ViewMatrix;
	}

	glm::vec3 FirstPersonCamera::GetUp() const
	{
		return {0.0f, 1.0f, 0.0f};
	}

	glm::vec3 FirstPersonCamera::GetRight() const
	{
		return glm::normalize(glm::cross(GetForward(), GetUp()));
	}

	glm::vec3 FirstPersonCamera::GetForward() const
	{
		glm::vec3 forward;
		forward.x = cos(glm::radians(m_Yaw)) * cos(glm::radians(m_Pitch));
		forward.y = sin(glm::radians(m_Pitch));
		forward.z = sin(glm::radians(m_Yaw)) * cos(glm::radians(m_Pitch));
		return glm::normalize(forward);
	}

	const glm::vec3& FirstPersonCamera::GetPosition() const
	{
		return m_Position;
	}

	glm::quat FirstPersonCamera::GetOrientation() const
	{
		return glm::quat(glm::vec3(glm::radians(-m_Pitch), glm::radians(-m_Yaw), 0.0f));
	}

	float FirstPersonCamera::GetPitch() const
	{
		return m_Pitch;
	}

	float FirstPersonCamera::GetYaw() const
	{
		return m_Yaw;
	}

	const glm::vec3& FirstPersonCamera::GetFocalPoint() const
	{
		constexpr static glm::vec3 zeroPoint{ 0.0f, 0.0f, 0.0f };
		return zeroPoint;
	}

	void FirstPersonCamera::UpdateView()
	{
		const glm::vec3 direction = GetForward();
		m_ViewMatrix = glm::lookAt(m_Position, m_Position + direction, GetUp());
		m_ViewDirty = false;
	}

	void FirstPersonCamera::UpdateProjection()
	{
		m_ProjectionMatrix = glm::perspective(glm::radians(m_Fov), m_AspectRatio, m_NearClip, m_FarClip);
		if (m_FlipY) {
			m_ProjectionMatrix[1][1] *= -1; // Invert Y for Vulkan
		}
		m_ProjectionDirty = false;
	}

}
