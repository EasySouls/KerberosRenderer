#include "kbrpch.hpp"
#include "EditorCamera.hpp"

#include "Frustum.hpp"
#include "Events/WindowResizedEvent.hpp"
#include "Input/InputSystem.hpp"

namespace Kerberos
{
	EditorCamera::EditorCamera(const float fov, const float aspectRatio, const float nearClip, const float farClip)
		: m_Fov(fov), m_AspectRatio(aspectRatio), m_NearClip(nearClip), m_FarClip(farClip), m_Projection(glm::perspective(glm::radians(fov), aspectRatio, nearClip, farClip))
	{
		UpdateView();
	}

	void EditorCamera::OnUpdate(const float)
	{
		if (IsInputBlocked())
			return;

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

	void EditorCamera::Focus(const glm::vec3& focusPoint)
	{
		m_FocalPoint = focusPoint;
		m_Distance = 10.0f;
		UpdateView();
	}

	void EditorCamera::SetViewportSize(const float width, const float height)
	{
		m_ViewportWidth = width;
		m_ViewportHeight = height;
		UpdateProjection();
	}

	void EditorCamera::SetFlipY(const bool flip)
	{
		m_FlipY = flip; 
		
		UpdateProjection();
	}

	float EditorCamera::GetNearClip() const
	{
		return m_NearClip;
	}

	float EditorCamera::GetFarClip() const
	{
		return m_FarClip;
	}

	void EditorCamera::SetNearClip(const float nearClip)
	{
		m_NearClip = nearClip;

		UpdateProjection();
	}

	void EditorCamera::SetFarClip(const float farClip)
	{
		m_FarClip = farClip;
		
		UpdateProjection();
	}

	float EditorCamera::GetMoveSpeed() const
	{
		return 0.0f;
	}

	void EditorCamera::SetMoveSpeed(const float /*speed*/)
	{
	}

	const glm::mat4& EditorCamera::GetViewMatrix(const Handedness handedness) const 
	{
		if (handedness == Handedness::Right)
		{
			return m_View;
		}

		return m_ViewLH;
	}

	const glm::mat4& EditorCamera::GetProjectionMatrix(const Handedness handedness) const
	{
		if (handedness == Handedness::Right)
		{
			return m_Projection;
		}

		return m_ProjectionLH;
	}

	glm::mat4 EditorCamera::GetViewProjectionMatrix(const Handedness handedness) const 
	{
		if (handedness == Handedness::Right)
		{
			return m_Projection * m_View;
		}

		return m_ProjectionLH * m_ViewLH;
	}

	std::pair<std::vector<glm::mat4>, glm::vec4> EditorCamera::GetLightSpaceMatrices(const glm::vec3& lightDir,
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

	glm::vec3 EditorCamera::GetUp() const
	{
		return glm::rotate(GetOrientation(), glm::vec3(0.0f, 1.0f, 0.0f));
	}

	glm::vec3 EditorCamera::GetRight() const
	{
		return glm::rotate(GetOrientation(), glm::vec3(1.0f, 0.0f, 0.0f));
	}

	glm::vec3 EditorCamera::GetForward() const
	{
		return glm::rotate(GetOrientation(), glm::vec3(0.0f, 0.0f, -1.0f));
	}

	glm::quat EditorCamera::GetOrientation() const
	{
		return { glm::vec3(-m_Pitch, -m_Yaw, 0.0f) };
	}

	void EditorCamera::UpdateProjection()
	{
		m_AspectRatio = m_ViewportWidth / m_ViewportHeight;
		m_Projection = glm::perspective(glm::radians(m_Fov), m_AspectRatio, m_NearClip, m_FarClip);
		if (m_FlipY) {
			m_Projection[1][1] *= -1; // Invert Y coordinate for Vulkan
		}

		glm::mat4 projectionLH = glm::perspectiveLH_ZO(glm::radians(m_Fov), m_AspectRatio, m_NearClip, m_FarClip);
		if (m_FlipY)
			projectionLH[1][1] *= -1.0f;
		m_ProjectionLH = projectionLH;
	}

	void EditorCamera::UpdateView()
	{
		// Lock the camera's rotation
		// m_Pitch = 0.0f;
		// m_Yaw = 0.0f;

		m_Position = CalculatePosition();
		glm::vec3 translation = m_Position;
		if (m_FlipY) {
			translation.y *= -1; // Invert Y coordinate for Vulkan
		}

		const glm::quat orientation = GetOrientation();
		m_View = glm::translate(glm::mat4(1.0f), translation) * glm::toMat4(orientation);
		m_View = glm::inverse(m_View);

		m_ViewLH = glm::lookAtLH(m_Position, m_Position + GetForward(), GetUp());
	}

	void EditorCamera::MousePan(const glm::vec2& delta)
	{
		auto [xFactor, yFactor] = GetPanSpeed();
		m_FocalPoint += -GetRight() * delta.x * xFactor * m_Distance;
		m_FocalPoint += GetUp() * delta.y * yFactor * m_Distance;
	}

	void EditorCamera::MouseRotate(const glm::vec2& delta)
	{
		const float yawSign = GetUp().y < 0.0f ? -1.0f : 1.0f;
		const float rotationSpeed = GetRotationSpeed();

		m_Yaw += delta.x * rotationSpeed * yawSign;
		m_Pitch += delta.y * rotationSpeed;
	}

	void EditorCamera::MouseZoom(const float delta)
	{
		m_Distance -= delta * GetZoomSpeed();
		if (m_Distance < 1.0f)
		{
			m_FocalPoint += GetForward();
			m_Distance = 1.0f;
		}
	}

	glm::vec3 EditorCamera::CalculatePosition() const
	{
		return m_FocalPoint - GetForward() * m_Distance;
	}

	std::pair<float, float> EditorCamera::GetPanSpeed() const
	{
		const float x = std::min(m_ViewportWidth / 1000.f, 2.0f);
		float xFactor = 0.0366f * (x * x) - 0.1778f * x + 0.3021f;

		const float y = std::min(m_ViewportHeight / 1000.f, 2.0f);
		float yFactor = 0.0366f * (y * y) - 0.1778f * y + 0.3021f;

		return { xFactor, yFactor };
	}

	float EditorCamera::GetRotationSpeed() const
	{
		return 0.8f;
	}

	float EditorCamera::GetZoomSpeed() const
	{
		float distance = m_Distance * 0.2f;
		distance = std::max(distance, 0.0f);

		float speed = distance * distance;
		speed = std::min(speed, 100.0f);

		return speed;
	}

	void EditorCamera::OnEvent(Event& event)
	{
		EventDispatcher dispatcher(event);
		dispatcher.Dispatch<MouseScrolledEvent>([this](const MouseScrolledEvent& e)
		{
			if (!IsInputBlocked())
			{
				OnMouseScrolled(e);
			}

			return true;
		});
	}

	void EditorCamera::SetPosition(const glm::vec3& position)
	{
		// We cannot just set the position directly, as the camera's position is derived from the focal point and distance
		const glm::vec3 offset = position - m_Position;
		m_FocalPoint += offset;

		m_Distance = glm::length(m_FocalPoint - position);

		UpdateView();
	}

	void EditorCamera::SetRotation(const glm::vec3& rotation) 
	{
		m_Pitch = rotation.x;
		m_Yaw = rotation.y;
	}

	void EditorCamera::Rotate(const float pitch, const float yaw) 
	{
		m_Pitch += pitch;
		m_Yaw += yaw;
	}

	void EditorCamera::Rotate(const glm::vec3& axis, const float angle) 
	{
		m_Pitch += axis.x * angle;
		m_Yaw += axis.y * angle;
	}

	void EditorCamera::OnMouseScrolled(const MouseScrolledEvent& mouseScrolled)
	{
		constexpr float sensitivity = 0.1f;
		const float delta = static_cast<float>(mouseScrolled.GetYOffset()) * sensitivity;
		MouseZoom(delta);

		UpdateView();
	}

	glm::mat4 EditorCamera::GetLightSpaceMatrix(const float nearPlane, const float farPlane, const glm::vec3& lightDir) const 
	{
		const auto proj = glm::perspective(
			glm::radians(m_Fov), m_AspectRatio, nearPlane,
			farPlane);
		const auto corners = GetFrustumCornersWorldSpace(proj, GetViewMatrix());

		glm::vec3 center = glm::vec3(0, 0, 0);
		for (const auto& v : corners)
		{
			center += glm::vec3(v);
		}
		center /= static_cast<float>(corners.size());

		const auto lightView = glm::lookAt(center + lightDir, center, glm::vec3(0.0f, 1.0f, 0.0f));

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

		const glm::mat4 lightProjection = glm::ortho(minX, maxX, minY, maxY, minZ, maxZ);
		return lightProjection * lightView;
	}
}
