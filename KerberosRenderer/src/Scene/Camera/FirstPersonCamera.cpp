#include "FirstPersonCamera.hpp"

namespace kbr
{
	FirstPersonCamera::FirstPersonCamera(const float fov, const float aspectRatio, const float nearClip, const float farClip)
		: m_Fov(fov), m_AspectRatio(aspectRatio), m_NearClip(nearClip), m_FarClip(farClip)
	{
		UpdateProjection();
		UpdateView();
	}

	void FirstPersonCamera::OnUpdate(const float deltaTime)
	{
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
		// Handle events if necessary
	}

	void FirstPersonCamera::SetPosition(const glm::vec3& position)
	{
		m_Position = position;
		m_ViewDirty = true;
	}

	float FirstPersonCamera::GetDistance() const 
	{
		return m_Distance;
	}

	void FirstPersonCamera::SetDistance(const float distance)
	{
		m_Distance = distance;
		m_ViewDirty = true;
	}

	void FirstPersonCamera::Focus(const glm::vec3& focusPoint)
	{
		m_FocalPoint = focusPoint;
		m_ViewDirty = true;
	}

	void FirstPersonCamera::SetViewportSize(const float width, const float height)
	{
		m_AspectRatio = width / height;
		m_ProjectionDirty = true;
		UpdateProjection();
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
		return glm::vec3(0.0f, 1.0f, 0.0f);
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
		return m_FocalPoint;
	}

	void FirstPersonCamera::UpdateView()
	{
		glm::vec3 direction = GetForward();
		m_ViewMatrix = glm::lookAt(m_Position, m_Position + direction, GetUp());
		m_ViewDirty = false;
	}

	void FirstPersonCamera::UpdateProjection()
	{
		m_ProjectionMatrix = glm::perspective(glm::radians(m_Fov), m_AspectRatio, m_NearClip, m_FarClip);
		m_ProjectionMatrix[1][1] *= -1; // Invert Y for Vulkan
		m_ProjectionDirty = false;
	}

}