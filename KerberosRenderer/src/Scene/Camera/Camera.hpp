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
		virtual ~Camera() = default;

		virtual void OnUpdate(float deltaTime) = 0;
		virtual void OnEvent(const std::shared_ptr<Event>& event) = 0;

		virtual void SetPosition(const glm::vec3& position) = 0;
		virtual void SetRotation(const glm::vec3& rotation) = 0;
		virtual void Rotate(float pitch, float yaw) = 0;
		virtual void Rotate(const glm::vec3& axis, float angle) = 0;

		virtual float GetDistance() const = 0;
		virtual void SetDistance(float distance) = 0;

		virtual void Focus(const glm::vec3& focusPoint) = 0;

		virtual void SetViewportSize(float width, float height) = 0;

		virtual void SetFlipY(bool flip) = 0;

		virtual const glm::mat4& GetViewMatrix() const = 0;
		virtual const glm::mat4& GetProjectionMatrix() const = 0;
		virtual glm::mat4 GetViewProjectionMatrix() const = 0;

		virtual glm::vec3 GetUp() const = 0;
		virtual glm::vec3 GetRight() const = 0;
		virtual glm::vec3 GetForward() const = 0;
		virtual const glm::vec3& GetPosition() const = 0;
		virtual glm::quat GetOrientation() const = 0;
		virtual float GetPitch() const = 0;
		virtual float GetYaw() const = 0;
		virtual const glm::vec3& GetFocalPoint() const = 0;
	};
}
