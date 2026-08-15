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
#include <functional>
#include <utility>

namespace Kerberos
{
	enum class Handedness
	{
		Left,
		Right
	};

	class Camera
	{
	public:
		virtual ~Camera() = default;

		virtual void OnUpdate(float deltaTime) = 0;
		virtual void OnEvent(Event &event) = 0;

		virtual void SetPosition(const glm::vec3 &position) = 0;
		virtual void SetRotation(const glm::vec3 &rotation) = 0;
		virtual void Rotate(float pitch, float yaw) = 0;
		virtual void Rotate(const glm::vec3 &axis, float angle) = 0;

		virtual float GetDistance() const = 0;
		virtual void SetDistance(float distance) = 0;

		virtual void Focus(const glm::vec3 &focusPoint) = 0;

		virtual void SetViewportSize(float width, float height) = 0;

		virtual void SetFlipY(bool flip) = 0;

		virtual float GetNearClip() const = 0;
		virtual float GetFarClip() const = 0;
		virtual void SetNearClip(float nearClip) = 0;
		virtual void SetFarClip(float farClip) = 0;

		virtual float GetMoveSpeed() const = 0;
		virtual void SetMoveSpeed(float speed) = 0;

		void SetIsInputBlocked(const bool isInputBlocked) { m_IsInputBlocked = isInputBlocked; }

		virtual const glm::mat4 &GetViewMatrix(Handedness handedness = Handedness::Right) const = 0;
		virtual const glm::mat4 &GetProjectionMatrix(Handedness handedness = Handedness::Right) const = 0;
		virtual glm::mat4 GetViewProjectionMatrix(Handedness handedness = Handedness::Right) const = 0;

		virtual std::pair<std::vector<glm::mat4>, glm::vec4> GetLightSpaceMatrices(const glm::vec3 &lightDir, const std::function<glm::vec4(float)> &getCascadeSplits) const = 0;

		virtual glm::vec3 GetUp() const = 0;
		virtual glm::vec3 GetRight() const = 0;
		virtual glm::vec3 GetForward() const = 0;
		virtual const glm::vec3 &GetPosition() const = 0;
		virtual glm::quat GetOrientation() const = 0;
		virtual float GetPitch() const = 0;
		virtual float GetYaw() const = 0;
		virtual const glm::vec3 &GetFocalPoint() const = 0;

	protected:
		bool IsInputBlocked() const { return m_IsInputBlocked; }

	private:
		bool m_IsInputBlocked = false;
	};
}
