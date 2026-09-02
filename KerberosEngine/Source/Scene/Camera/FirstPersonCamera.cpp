#include "FirstPersonCamera.hpp"

#include <algorithm>

#include "Frustum.hpp"
#include "Events/MouseButtonPressedEvent.hpp"
#include "Events/MouseButtonReleasedEvent.hpp"
#include "Input/InputSystem.hpp"
#include "Renderer/Renderer.hpp"

import Kerberos;

namespace Kerberos
{
	FirstPersonCamera::FirstPersonCamera(const float fov, const float aspectRatio, const float nearClip, const float farClip)
		: m_Fov(fov), m_AspectRatio(aspectRatio), m_NearClip(nearClip), m_FarClip(farClip)
	{
		UpdateProjection();
		UpdateView();
	}

	void FirstPersonCamera::OnUpdate(const float deltaTime)
	{
		if (IsInputBlocked())
			return;

		const bool forward = Input::IsKeyPressed(Key::W);
		const bool backward = Input::IsKeyPressed(Key::S);
		const bool left = Input::IsKeyPressed(Key::A);
		const bool right = Input::IsKeyPressed(Key::D);

		if (forward || backward || left || right)
		{
			const glm::vec3 forwardDir = GetForward();
			const glm::vec3 rightDir = GetRight();

			glm::vec3 movement(0.0f);
			if (forward)
				movement += forwardDir;
			if (backward)
				movement -= forwardDir;
			if (left)
				movement -= rightDir;
			if (right)
				movement += rightDir;

			if (glm::length(movement) > 0.0f)
			{
				movement = glm::normalize(movement);
				m_Position += movement * m_MoveSpeed * deltaTime;
				m_ViewDirty = true;
			}
		}

		if (m_ViewDirty)
		{
			UpdateView();
		}
		if (m_ProjectionDirty)
		{
			UpdateProjection();
		}
	}

	void FirstPersonCamera::OnEvent(Event& event)
	{
		EventDispatcher dispatcher(event);
		dispatcher.Dispatch<MouseButtonPressedEvent>(KBR_BIND_FN(FirstPersonCamera::OnMouseButtonPressed));
		dispatcher.Dispatch<MouseButtonReleasedEvent>(KBR_BIND_FN(FirstPersonCamera::OnMouseButtonReleased));
		dispatcher.Dispatch<MouseMovedEvent>(KBR_BIND_FN(FirstPersonCamera::OnMouseMoved));
	}

	void FirstPersonCamera::SetPosition(const glm::vec3& position)
	{
		m_Position = position;
		m_ViewDirty = true;
	}

	void FirstPersonCamera::SetRotation(const glm::vec3& rotation) 
	{
		m_Pitch = rotation.x;
		m_Yaw = rotation.y;

		m_ViewDirty = true;
	}

	void FirstPersonCamera::Rotate(const float pitch, const float yaw) 
	{
		m_Pitch += pitch;
		m_Yaw += yaw;

		m_ViewDirty = true;
	}

	void FirstPersonCamera::Rotate(const glm::vec3& axis, const float angle) 
	{
		m_Pitch += axis.x * angle;
		m_Yaw += axis.y * angle;

		m_ViewDirty = true;
	}

	float FirstPersonCamera::GetDistance() const 
	{
		return 0.0f;
	}

	void FirstPersonCamera::SetDistance([[maybe_unused]] const float distance)
	{
	}

	void FirstPersonCamera::Focus([[maybe_unused]] const glm::vec3& focusPoint)
	{
	}

	void FirstPersonCamera::SetViewportSize(const float width, const float height)
	{
		m_AspectRatio = width / height;
		m_ProjectionDirty = true;
		UpdateProjection();
	}

	void FirstPersonCamera::SetFlipY(const bool flip) 
	{
		m_FlipY = flip;

		m_ProjectionDirty = true;
	}

	float FirstPersonCamera::GetNearClip() const 
	{
		return m_NearClip;
	}

	float FirstPersonCamera::GetFarClip() const 
	{
		return m_FarClip;
	}

	void FirstPersonCamera::SetNearClip(const float nearClip) 
	{
		m_NearClip = nearClip;
		m_ProjectionDirty = true;
	}

	void FirstPersonCamera::SetFarClip(const float farClip) 
	{
		m_FarClip = farClip;
		m_ProjectionDirty = true;
	}

	float FirstPersonCamera::GetMoveSpeed() const 
	{
		return m_MoveSpeed;
	}

	void FirstPersonCamera::SetMoveSpeed(const float speed) 
	{
		m_MoveSpeed = speed;
	}

	const glm::mat4& FirstPersonCamera::GetViewMatrix(const Handedness handedness) const
	{
		if (handedness == Handedness::Right) 
		{
			return m_ViewMatrix;
		}

		return m_ViewMatrixLH;
	}

	const glm::mat4& FirstPersonCamera::GetProjectionMatrix(const Handedness handedness) const 
	{
		if (handedness == Handedness::Right) 
		{
			return m_ProjectionMatrix;
		}

		return m_ProjectionMatrixLH;
	}

	glm::mat4 FirstPersonCamera::GetViewProjectionMatrix(const Handedness handedness) const 
	{
		if (handedness == Handedness::Right) 
		{
			return m_ProjectionMatrix * m_ViewMatrix;
		}

		return m_ProjectionMatrixLH * m_ViewMatrixLH;
	}

	std::pair<std::vector<glm::mat4>, glm::vec4> FirstPersonCamera::GetLightSpaceMatrices(const glm::vec3& lightDir,
																					 const std::function<glm::vec4(float)>& getCascadeSplits) const
	{
		const glm::vec4 cascadeSplits = getCascadeSplits(m_FarClip);

		std::vector<glm::mat4> matrices;
		for (uint32_t i = 0; i < 3 + 1; ++i)
		{
			if (i == 0)
			{
				matrices.push_back(GetLightSpaceMatrix(m_NearClip, cascadeSplits[i], lightDir));
			}
			else if (i < 3)
			{
				matrices.push_back(GetLightSpaceMatrix(cascadeSplits[i - 1], cascadeSplits[i], lightDir));
			}
			else
			{
				matrices.push_back(GetLightSpaceMatrix(cascadeSplits[i - 1], m_FarClip, lightDir));
			}
		}

		return { matrices, cascadeSplits };
	}

	glm::vec3 FirstPersonCamera::GetUp() const
	{
		return {0.0f, 1.0f, 0.0f};
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
		return { glm::vec3(glm::radians(-m_Pitch), glm::radians(-m_Yaw), 0.0f) };
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
		constexpr static glm::vec3 zeroPoint{ 0.0f, 0.0f, 0.0f };
		return zeroPoint;
	}

	void FirstPersonCamera::UpdateView()
	{
		const glm::vec3 direction = GetForward();
		m_ViewMatrix = glm::lookAt(m_Position, m_Position + direction, GetUp());

		m_ViewMatrixLH = glm::lookAtLH(m_Position, m_Position + direction, GetUp());

		m_ViewDirty = false;
	}

	void FirstPersonCamera::UpdateProjection()
	{
		m_ProjectionMatrix = glm::perspective(glm::radians(m_Fov), m_AspectRatio, m_NearClip, m_FarClip);
		if (m_FlipY) {
			m_ProjectionMatrix[1][1] *= -1; // Invert Y for Vulkan
		}

		m_ProjectionMatrixLH = glm::perspectiveLH(glm::radians(m_Fov), m_AspectRatio, m_NearClip, m_FarClip);
		if (m_FlipY) {
			m_ProjectionMatrixLH[1][1] *= -1; // Invert Y for Vulkan
		}

		m_ProjectionDirty = false;
	}

	bool FirstPersonCamera::OnMouseButtonPressed(const MouseButtonPressedEvent& event) 
	{
		if (IsInputBlocked())
			return false;

		if (event.GetButton() == Mouse::Button1)
		{
			//Input::SetCursorMode(CursorMode::Locked);
			m_CanLookAround = true;
			return true;
		}

		return false;
	}

	bool FirstPersonCamera::OnMouseButtonReleased(const MouseButtonReleasedEvent& event) 
	{
		if (IsInputBlocked())
			return false;

		if (event.GetButton() == Mouse::Button1)
		{
			//Input::SetCursorMode(CursorMode::Normal);
			m_CanLookAround = false;
			return true;
		}

		return false;
	}

	bool FirstPersonCamera::OnMouseMoved(const MouseMovedEvent& event)
	{
		if (IsInputBlocked())
			return false;

		const int32_t x = static_cast<int32_t>(event.GetX());
		const int32_t y = static_cast<int32_t>(event.GetY());

		const int32_t dx = static_cast<int32_t>(m_MousePosition.x) - x;
		const int32_t dy = static_cast<int32_t>(m_MousePosition.y) - y;

		m_MousePosition.x = static_cast<float>(x);
		m_MousePosition.y = static_cast<float>(y);

		if (!m_CanLookAround)
			return false;

		constexpr float rotationSpeed = 0.3f;
		m_Yaw -= static_cast<float>(dx) * rotationSpeed;
		m_Pitch += static_cast<float>(dy) * rotationSpeed;
		m_Pitch = std::min(m_Pitch, 89.0f);
		m_Pitch = std::max(m_Pitch, -89.0f);
		m_ViewDirty = true;

		return true;
	}

	glm::mat4 FirstPersonCamera::GetLightSpaceMatrix(const float nearPlane, const float farPlane, const glm::vec3& lightDir) const
	{
		const glm::mat4 proj = glm::perspective(
			glm::radians(m_Fov), m_AspectRatio, nearPlane, farPlane);

		const auto corners = GetFrustumCornersWorldSpace(proj, GetViewMatrix());

		glm::vec3 center = glm::vec3(0, 0, 0);
		for (const auto& v : corners)
		{
			center += glm::vec3(v);
		}
		center /= static_cast<float>(corners.size());

		glm::vec3 lightUp = glm::vec3(0.0f, 1.0f, 0.0f);
		if (glm::abs(glm::dot(lightDir, lightUp)) > 0.99f)
		{
			lightUp = glm::vec3(1.0f, 0.0f, 0.0f);
		}

		const auto lightView = glm::lookAt(center + lightDir, center, lightUp);

		float minX = std::numeric_limits<float>::max();
		float maxX = std::numeric_limits<float>::lowest();
		float minY = std::numeric_limits<float>::max();
		float maxY = std::numeric_limits<float>::lowest();
		float minZ = std::numeric_limits<float>::max();
		float maxZ = std::numeric_limits<float>::lowest();
		for (const auto& v : corners)
		{
			const auto trf = lightView * v;
			minX = std::min(minX, trf.x);
			maxX = std::max(maxX, trf.x);
			minY = std::min(minY, trf.y);
			maxY = std::max(maxY, trf.y);
			minZ = std::min(minZ, trf.z);
			maxZ = std::max(maxZ, trf.z);
		}

		// Tune this parameter according to the scene
		constexpr float zMult = 10.0f;
		if (minZ < 0)
		{
			minZ *= zMult;
		}
		else
		{
			minZ /= zMult;
		}
		if (maxZ < 0)
		{
			maxZ /= zMult;
		}
		else
		{
			maxZ *= zMult;
		}

		// Add a slight offset to prevent negative near plane bounding issues if scene bounds are close to near clip
		minZ = std::min(minZ, -100.0f);
		maxZ = std::max(maxZ, 100.0f);

		// Apply texel snapping
		const float orthoWidth = maxX - minX;
		const uint32_t shadowMapResolution = Renderer::GetShadowMapResolution();
		const float worldUnitsPerTexel = orthoWidth / static_cast<float>(shadowMapResolution);

		minX = std::floor(minX / worldUnitsPerTexel) * worldUnitsPerTexel;
		maxX = std::floor(maxX / worldUnitsPerTexel) * worldUnitsPerTexel;
		minY = std::floor(minY / worldUnitsPerTexel) * worldUnitsPerTexel;
		maxY = std::floor(maxY / worldUnitsPerTexel) * worldUnitsPerTexel;

		const glm::mat4 lightProjection = glm::ortho(minX, maxX, minY, maxY, minZ, maxZ);
		return lightProjection * lightView;
	}
}