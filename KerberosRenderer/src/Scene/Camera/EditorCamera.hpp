#pragma once

#include "Camera.hpp"

#include "events/Event.hpp"
#include "events/MouseScrolledEvent.hpp"

#include <memory>

namespace kbr
{
	class EditorCamera : public Camera
	{
	public:
		EditorCamera() = default;
		EditorCamera(float fov, float aspectRatio, float nearClip, float farClip);

		void OnUpdate(float deltaTime) override;
		void OnEvent(const std::shared_ptr<Event>& event) override;

		void SetPosition(const glm::vec3& position) override;

		float GetDistance() const override { return m_Distance; }
		void SetDistance(const float distance) override { m_Distance = distance; }

		void Focus(const glm::vec3& focusPoint) override;

		void SetViewportSize(float width, float height) override;

		void SetFlipY(bool flip) override;

		const glm::mat4& GetViewMatrix() const override { return m_View; }
		const glm::mat4& GetProjectionMatrix() const override { return m_Projection; }
		glm::mat4 GetViewProjectionMatrix() const override { return m_Projection * m_View; }

		glm::vec3 GetUp() const override;
		glm::vec3 GetRight() const override;
		glm::vec3 GetForward() const override;
		const glm::vec3& GetPosition() const override { return m_Position; }
		glm::quat GetOrientation() const override;

		float GetPitch() const override { return m_Pitch; }
		float GetYaw() const override { return m_Yaw; }
		const glm::vec3& GetFocalPoint() const override { return m_FocalPoint; }

	private:
		void UpdateProjection();
		void UpdateView();

		void MousePan(const glm::vec2& delta);
		void MouseRotate(const glm::vec2& delta);
		void MouseZoom(float delta);

		glm::vec3 CalculatePosition() const;

		std::pair<float, float> GetPanSpeed() const;
		float GetRotationSpeed() const;
		float GetZoomSpeed() const;

		void OnMouseScrolled(const MouseScrolledEvent& mouseScrolled);

	private:
		float m_Fov = 45.0f;
		float m_AspectRatio = 1.778f;
		float m_NearClip = 0.1f;
		float m_FarClip = 1000.0f;

		glm::mat4 m_View;
		glm::mat4 m_Projection;
		glm::vec3 m_Position = { 0.0f, 0.0f, 0.0f };
		glm::vec3 m_FocalPoint = { 0.0f, 0.0f, 0.0f };

		glm::vec2 m_InitialMousePosition = { 0.0f, 0.0f };

		float m_Distance = 5.0f;
		float m_Pitch = 0.0f;
		float m_Yaw = 0.0f;

		float m_ViewportWidth = 1280.0f;
		float m_ViewportHeight = 720.0f;

		bool m_FlipY = true;
	};
}
