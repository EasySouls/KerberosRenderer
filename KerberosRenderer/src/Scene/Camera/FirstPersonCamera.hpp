#pragma once

#include "Camera.hpp"

namespace kbr 
{
	class FirstPersonCamera : public Camera
	{
	public:
		FirstPersonCamera(float fov, float aspectRatio, float nearClip, float farClip);
		~FirstPersonCamera() override = default;

		void OnUpdate(float deltaTime) override;
		void OnEvent(const std::shared_ptr<Event>& event) override;

		void SetPosition(const glm::vec3& position) override;
		void SetRotation(const glm::vec3& rotation) override;
		void Rotate(float pitch, float yaw) override;
		void Rotate(const glm::vec3& axis, float angle) override;

		float GetDistance() const override;
		void SetDistance(float distance) override;

		void Focus(const glm::vec3& focusPoint) override;

		void SetViewportSize(float width, float height) override;

		void SetFlipY(bool flip) override;

		const glm::mat4& GetViewMatrix() const override;
		const glm::mat4& GetProjectionMatrix() const override;
		glm::mat4 GetViewProjectionMatrix() const override;

		glm::vec3 GetUp() const override;
		glm::vec3 GetRight() const override;
		glm::vec3 GetForward() const override;

		const glm::vec3& GetPosition() const override;
		glm::quat GetOrientation() const override;
		float GetPitch() const override;
		float GetYaw() const override;
		const glm::vec3& GetFocalPoint() const override;

	private:
		void UpdateView();
		void UpdateProjection();

	private:
		glm::vec3 m_Position{ 0.0f, 0.0f, 0.0f };
		float m_MoveSpeed = 20.0f;
		bool m_CanLookAround = false;

		bool m_FlipY = false;

		glm::vec2 m_MousePosition{ 0.0f, 0.0f };

		float m_Pitch = 0.0f;
		float m_Yaw = -90.0f; // Facing towards negative Z by default
		float m_Fov;
		float m_AspectRatio;
		float m_NearClip;
		float m_FarClip;
		glm::mat4 m_ViewMatrix{ 1.0f };
		glm::mat4 m_ProjectionMatrix{ 1.0f };
		bool m_ViewDirty = true;
		bool m_ProjectionDirty = true;
	};

}