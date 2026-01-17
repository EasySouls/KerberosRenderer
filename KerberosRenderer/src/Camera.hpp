#pragma once

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace kbr 
{
	struct CameraMatrices
	{
		glm::mat4 perspective;
		glm::mat4 view;
	};

	class Camera
	{
	public:
		Camera(float fovY, float aspectRatio, float nearPlane, float farPlane);

		void SetPosition(const glm::vec3& position);
		void SetRotation(const glm::vec3& rotation);
		void Move(const glm::vec3& delta);
		void Rotate(const glm::vec3& delta);

		CameraMatrices GetMatrices() const;
		glm::vec3 GetPosition() const;

	private:
		void UpdateViewMatrix();

	private:
		glm::vec3 m_Position;
		glm::vec3 m_Rotation; // Euler angles in degrees
		CameraMatrices m_Matrices;
	};
}