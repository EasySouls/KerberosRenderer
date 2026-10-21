#include "kbrpch.hpp"
#include "SceneCamera.hpp"

#include "Frustum.hpp"

#include <glm/gtc/matrix_transform.hpp>

namespace Kerberos
{
	SceneCamera::SceneCamera()
	{
		RecalculateProjection();
	}

	void SceneCamera::SetOrthographic(const float size, const float nearClip, const float farClip)
	{
		m_ProjectionType = ProjectionType::Orthographic;

		m_OrthoSize = size;
		m_OrthoNear = nearClip;
		m_OrthoFar = farClip;

		RecalculateProjection();
	}

	void SceneCamera::SetPerspective(const float fov, const float nearClip, const float farClip)
	{
		m_ProjectionType = ProjectionType::Perspective;

		m_PerspectiveFov = fov;
		m_PerspectiveNear = nearClip;
		m_PerspectiveFar = farClip;

		RecalculateProjection();
	}

	void SceneCamera::SetViewportSize(const uint32_t width, const uint32_t height)
	{
		if (height == 0) return;
		m_AspectRatio = static_cast<float>(width) / static_cast<float>(height);

		RecalculateProjection();
	}

	void SceneCamera::RecalculateProjection()
	{
		KBR_TRACY_FUNCTION();

		const float aspectRatio = GetAspectRatio();

		if (m_ProjectionType == ProjectionType::Orthographic)
		{
			const float orthoLeft = -m_OrthoSize * aspectRatio * 0.5f;
			const float orthoRight = m_OrthoSize * aspectRatio * 0.5f;
			const float orthoBottom = -m_OrthoSize * 0.5f;
			const float orthoTop = m_OrthoSize * 0.5f;

			m_Projection = glm::ortho(orthoLeft, orthoRight, orthoBottom, orthoTop, m_OrthoNear, m_OrthoFar);
		}
		else
		{
			m_Projection = glm::perspective(m_PerspectiveFov, aspectRatio, m_PerspectiveNear, m_PerspectiveFar);
		}

		// Vulkan's normalized device coordinates have Y pointing down.
		m_Projection[1][1] *= -1.0f;
	}

	float SceneCamera::GetNearClip() const
	{
		return m_ProjectionType == ProjectionType::Orthographic ? m_OrthoNear : m_PerspectiveNear;
	}

	float SceneCamera::GetFarClip() const
	{
		return m_ProjectionType == ProjectionType::Orthographic ? m_OrthoFar : m_PerspectiveFar;
	}

	std::pair<std::vector<glm::mat4>, glm::vec4> SceneCamera::GetLightSpaceMatrices(
		const glm::vec3& lightDir,
		const std::function<glm::vec4(float)>& getCascadeSplits) const
	{
		return CalculateLightSpaceMatrices(m_View,
		                                   m_PerspectiveFov,
		                                   GetAspectRatio(),
		                                   m_PerspectiveNear,
		                                   m_PerspectiveFar,
		                                   lightDir,
		                                   getCascadeSplits);
	}
}
