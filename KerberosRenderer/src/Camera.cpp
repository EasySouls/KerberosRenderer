#include "Camera.hpp"

#include "events/WindowResizedEvent.hpp"
#include "input/InputSystem.hpp"

namespace kbr 
{
	Camera::Camera(const float fov, const float aspectRatio, const float nearClip, const float farClip)
		: m_Fov(fov), m_AspectRatio(aspectRatio), m_NearClip(nearClip), m_FarClip(farClip), m_Projection(glm::perspective(glm::radians(fov), aspectRatio, nearClip, farClip))
	{
		UpdateView();
	}

	void Camera::OnUpdate(float) 
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

	void Camera::Focus(const glm::vec3& focusPoint) 
	{
		m_FocalPoint = focusPoint;
		m_Distance = 10.0f;
		UpdateView();
	}

	void Camera::SetViewportSize(const float width, const float height) 
	{
		m_ViewportWidth = width;
		m_ViewportHeight = height;
		UpdateProjection();
	}

	glm::vec3 Camera::GetUp() const 
	{
		return glm::rotate(GetOrientation(), glm::vec3(0.0f, 1.0f, 0.0f));
	}

	glm::vec3 Camera::GetRight() const 
	{
		return glm::rotate(GetOrientation(), glm::vec3(1.0f, 0.0f, 0.0f));
	}

	glm::vec3 Camera::GetForward() const 
	{
		return glm::rotate(GetOrientation(), glm::vec3(0.0f, 0.0f, -1.0f));
	}

	glm::quat Camera::GetOrientation() const 
	{
		return { glm::vec3(-m_Pitch, -m_Yaw, 0.0f) };
	}

	void Camera::UpdateProjection() 
	{
		m_AspectRatio = m_ViewportWidth / m_ViewportHeight;
		m_Projection = glm::perspective(glm::radians(m_Fov), m_AspectRatio, m_NearClip, m_FarClip);
		m_Projection[1][1] *= -1; // Invert Y coordinate for Vulkan
	}

	void Camera::UpdateView() 
	{
		// Lock the camera's rotation
		// m_Pitch = 0.0f;
		// m_Yaw = 0.0f;

		m_Position = CalculatePosition();

		const glm::quat orientation = GetOrientation();
		m_View = glm::translate(glm::mat4(1.0f), m_Position) * glm::toMat4(orientation);
		m_View = glm::inverse(m_View);
	}

	void Camera::MousePan(const glm::vec2& delta)
	{
		auto [xFactor, yFactor] = GetPanSpeed();
		m_FocalPoint += -GetRight() * delta.x * xFactor * m_Distance;
		m_FocalPoint += GetUp() * delta.y * yFactor * m_Distance;
	}

	void Camera::MouseRotate(const glm::vec2& delta)
	{
		const float yawSign = GetUp().y < 0.0f ? -1.0f : 1.0f;
		const float rotationSpeed = GetRotationSpeed();

		m_Yaw += delta.x * rotationSpeed * yawSign;
		m_Pitch += delta.y * rotationSpeed;
	}

	void Camera::MouseZoom(const float delta)
	{
		m_Distance -= delta * GetZoomSpeed();
		if (m_Distance < 1.0f)
		{
			m_FocalPoint += GetForward();
			m_Distance = 1.0f;
		}
	}

	glm::vec3 Camera::CalculatePosition() const 
	{
		return m_FocalPoint - GetForward() * m_Distance;
	}

	std::pair<float, float> Camera::GetPanSpeed() const
	{
		const float x = std::min(m_ViewportWidth / 1000.f, 2.0f);
		float xFactor = 0.0366f * (x * x) - 0.1778f * x + 0.3021f;

		const float y = std::min(m_ViewportHeight / 1000.f, 2.0f);
		float yFactor = 0.0366f * (y * y) - 0.1778f * y + 0.3021f;

		return { xFactor, yFactor };
	}

	float Camera::GetRotationSpeed() const
	{
		return 0.8f;
	}

	float Camera::GetZoomSpeed() const
	{
		float distance = m_Distance * 0.2f;
		distance = std::max(distance, 0.0f);

		float speed = distance * distance;
		speed = std::min(speed, 100.0f);

		return speed;
	}

	void Camera::OnEvent(const std::shared_ptr<Event>& event)
	{
		if (const auto mouseScrolled = std::dynamic_pointer_cast<MouseScrolledEvent>(event))
		{
			OnMouseScrolled(*mouseScrolled);
		}
	}

	void Camera::OnMouseScrolled(const MouseScrolledEvent& mouseScrolled) 
	{
		constexpr float sensitivity = 0.1f;
		const float delta = static_cast<float>(mouseScrolled.GetYOffset()) * sensitivity;
		MouseZoom(delta);

		UpdateView();
	}
}
