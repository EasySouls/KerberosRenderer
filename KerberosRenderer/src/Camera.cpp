#include "Camera.hpp"

namespace kbr 
{
	Camera::Camera(const float fovY, const float aspectRatio, const float nearPlane, const float farPlane)
		: m_Position(0.0f), m_Rotation(0.0f)
	{
		m_Matrices.perspective = glm::perspective(glm::radians(fovY), aspectRatio, nearPlane, farPlane);
		m_Matrices.perspective[1][1] *= -1; // Invert Y for Vulkan
		UpdateViewMatrix();
	}

	void Camera::SetPosition(const glm::vec3& position) 
	{
		m_Position = position;
		UpdateViewMatrix();
	}

	void Camera::SetRotation(const glm::vec3& rotation) 
	{
		m_Rotation = rotation;
		UpdateViewMatrix();
	}

	void Camera::Move(const glm::vec3& delta) 
	{
		m_Position += delta;
		UpdateViewMatrix();
	}

	void Camera::Rotate(const glm::vec3& delta) 
	{
		m_Rotation += delta;
		UpdateViewMatrix();
	}

	CameraMatrices Camera::GetMatrices() const 
	{
		return m_Matrices;
	}

	glm::vec3 Camera::GetPosition() const 
	{
		return m_Position;
	}

	void Camera::UpdateViewMatrix() 
	{
		const glm::mat4 rotation = 
			glm::rotate(glm::mat4(1.0f), glm::radians(m_Rotation.x), glm::vec3(1.0f, 0.0f, 0.0f)) *
			glm::rotate(glm::mat4(1.0f), glm::radians(m_Rotation.y), glm::vec3(0.0f, 1.0f, 0.0f)) *
			glm::rotate(glm::mat4(1.0f), glm::radians(m_Rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

		const glm::mat4 translation = glm::translate(glm::mat4(1.0f), -m_Position);
		m_Matrices.view = rotation * translation;
	}
}