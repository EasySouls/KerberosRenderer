#pragma once

#include "events/Event.hpp"
#include "events/KeyPressedEvent.hpp"
#include "events/MouseMovedEvent.hpp"
#include "events/MouseScrolledEvent.hpp"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <memory>

namespace kbr 
{
	class Camera
	{
	public:
		Camera() = default;
		Camera(float fov, float aspectRatio, float nearClip, float farClip);

		void OnUpdate(float deltaTime);
		void OnEvent(const std::shared_ptr<Event>& event);

		float GetDistance() const { return m_Distance; }
		void SetDistance(const float distance) { m_Distance = distance; }

		void Focus(const glm::vec3& focusPoint);

		void SetViewportSize(float width, float height);

		const glm::mat4& GetViewMatrix() const { return m_View; }
		const glm::mat4& GetProjectionMatrix() const { return m_Projection; }
		glm::mat4 GetViewProjectionMatrix() const { return m_Projection * m_View; }

		glm::vec3 GetUp() const;
		glm::vec3 GetRight() const;
		glm::vec3 GetForward() const;
		const glm::vec3& GetPosition() const { return m_Position; }
		glm::quat GetOrientation() const;

		float GetPitch() const { return m_Pitch; }
		float GetYaw() const { return m_Yaw; }

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
	};
}
