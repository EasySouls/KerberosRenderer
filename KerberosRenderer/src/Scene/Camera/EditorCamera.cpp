#include "pch.hpp"
#include "EditorCamera.hpp"

#include "events/WindowResizedEvent.hpp"
#include "input/InputSystem.hpp"

namespace kbr
{
	EditorCamera::EditorCamera(const float fov, const float aspectRatio, const float nearClip, const float farClip)
		: m_Fov(fov), m_AspectRatio(aspectRatio), m_NearClip(nearClip), m_FarClip(farClip), m_Projection(glm::perspective(glm::radians(fov), aspectRatio, nearClip, farClip))
	{
		UpdateView();
	}

	void EditorCamera::OnUpdate(const float)
	{
		if (Input::IsKeyPressed(Key::LeftAlt))
		{
			const glm::vec2 mouse = { Input::GetMouseX(), Input::GetMouseY() };
			const glm::vec2& delta = (mouse - m_InitialMousePosition) * 0.003f;
			m_InitialMousePosition = mouse;

			if (Input::IsMouseButtonPressed(Mouse::ButtonMiddle))
			{
				MousePan(delta);
			}
			else if (Input::IsMouseButtonPressed(Mouse::ButtonRight))
			{
				MouseRotate(delta);
			}
			else if (Input::IsMouseButtonPressed(Mouse::ButtonLeft))
			{
				MouseZoom(delta.y);
			}

			UpdateView();
		}
	}

	void EditorCamera::Focus(const glm::vec3& focusPoint)
	{
		m_FocalPoint = focusPoint;
		m_Distance = 10.0f;
		UpdateView();
	}

	void EditorCamera::SetViewportSize(const float width, const float height)
	{
		m_ViewportWidth = width;
		m_ViewportHeight = height;
		UpdateProjection();
	}

	void EditorCamera::SetFlipY(const bool flip)
	{
		m_FlipY = flip; 
		
		UpdateProjection();
	}

	glm::vec3 EditorCamera::GetUp() const
	{
		return glm::rotate(GetOrientation(), glm::vec3(0.0f, 1.0f, 0.0f));
	}

	glm::vec3 EditorCamera::GetRight() const
	{
		return glm::rotate(GetOrientation(), glm::vec3(1.0f, 0.0f, 0.0f));
	}

	glm::vec3 EditorCamera::GetForward() const
	{
		return glm::rotate(GetOrientation(), glm::vec3(0.0f, 0.0f, -1.0f));
	}

	glm::quat EditorCamera::GetOrientation() const
	{
		return { glm::vec3(-m_Pitch, -m_Yaw, 0.0f) };
	}

	void EditorCamera::UpdateProjection()
	{
		m_AspectRatio = m_ViewportWidth / m_ViewportHeight;
		m_Projection = glm::perspective(glm::radians(m_Fov), m_AspectRatio, m_NearClip, m_FarClip);
		if (m_FlipY) {
			m_Projection[1][1] *= -1; // Invert Y coordinate for Vulkan
		}
	}

	void EditorCamera::UpdateView()
	{
		// Lock the camera's rotation
		// m_Pitch = 0.0f;
		// m_Yaw = 0.0f;

		m_Position = CalculatePosition();
		glm::vec3 translation = m_Position;
		if (m_FlipY) {
			translation.y *= -1; // Invert Y coordinate for Vulkan
		}

		const glm::quat orientation = GetOrientation();
		m_View = glm::translate(glm::mat4(1.0f), translation) * glm::toMat4(orientation);
		m_View = glm::inverse(m_View);
	}

	void EditorCamera::MousePan(const glm::vec2& delta)
	{
		auto [xFactor, yFactor] = GetPanSpeed();
		m_FocalPoint += -GetRight() * delta.x * xFactor * m_Distance;
		m_FocalPoint += GetUp() * delta.y * yFactor * m_Distance;
	}

	void EditorCamera::MouseRotate(const glm::vec2& delta)
	{
		const float yawSign = GetUp().y < 0.0f ? -1.0f : 1.0f;
		const float rotationSpeed = GetRotationSpeed();

		m_Yaw += delta.x * rotationSpeed * yawSign;
		m_Pitch += delta.y * rotationSpeed;
	}

	void EditorCamera::MouseZoom(const float delta)
	{
		m_Distance -= delta * GetZoomSpeed();
		if (m_Distance < 1.0f)
		{
			m_FocalPoint += GetForward();
			m_Distance = 1.0f;
		}
	}

	glm::vec3 EditorCamera::CalculatePosition() const
	{
		return m_FocalPoint - GetForward() * m_Distance;
	}

	std::pair<float, float> EditorCamera::GetPanSpeed() const
	{
		const float x = std::min(m_ViewportWidth / 1000.f, 2.0f);
		float xFactor = 0.0366f * (x * x) - 0.1778f * x + 0.3021f;

		const float y = std::min(m_ViewportHeight / 1000.f, 2.0f);
		float yFactor = 0.0366f * (y * y) - 0.1778f * y + 0.3021f;

		return { xFactor, yFactor };
	}

	float EditorCamera::GetRotationSpeed() const
	{
		return 0.8f;
	}

	float EditorCamera::GetZoomSpeed() const
	{
		float distance = m_Distance * 0.2f;
		distance = std::max(distance, 0.0f);

		float speed = distance * distance;
		speed = std::min(speed, 100.0f);

		return speed;
	}

	void EditorCamera::OnEvent(const std::shared_ptr<Event>& event)
	{
		if (const auto mouseScrolled = std::dynamic_pointer_cast<MouseScrolledEvent>(event))
		{
			OnMouseScrolled(*mouseScrolled);
		}
	}

	void EditorCamera::SetPosition(const glm::vec3& position)
	{
		// We cannot just set the position directly, as the camera's position is derived from the focal point and distance
		const glm::vec3 offset = position - m_Position;
		m_FocalPoint += offset;

		m_Distance = glm::length(m_FocalPoint - position);

		UpdateView();
	}

	void EditorCamera::SetRotation(const glm::vec3& rotation) 
	{
		m_Pitch = rotation.x;
		m_Yaw = rotation.y;
	}

	void EditorCamera::Rotate(const float pitch, const float yaw) 
	{
		m_Pitch += pitch;
		m_Yaw += yaw;
	}

	void EditorCamera::Rotate(const glm::vec3& axis, const float angle) 
	{
		m_Pitch += axis.x * angle;
		m_Yaw += axis.y * angle;
	}

	void EditorCamera::OnMouseScrolled(const MouseScrolledEvent& mouseScrolled)
	{
		constexpr float sensitivity = 0.1f;
		const float delta = static_cast<float>(mouseScrolled.GetYOffset()) * sensitivity;
		MouseZoom(delta);

		UpdateView();
	}
}
