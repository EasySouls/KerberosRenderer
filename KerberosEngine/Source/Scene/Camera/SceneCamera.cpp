#include "kbrpch.hpp"
#include "SceneCamera.hpp"


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
		if (m_ProjectionType == ProjectionType::Orthographic)
		{
			const float orthoLeft = -m_OrthoSize * m_AspectRatio * 0.5f;
			const float orthoRight = m_OrthoSize * m_AspectRatio * 0.5f;
			const float orthoBottom = -m_OrthoSize * 0.5f;
			const float orthoTop = m_OrthoSize * 0.5f;

			m_Projection = glm::ortho(orthoLeft, orthoRight, orthoBottom, orthoTop, m_OrthoNear, m_OrthoFar);
		}
		else
		{
			m_Projection = glm::perspective(m_PerspectiveFov, m_AspectRatio, m_PerspectiveNear, m_PerspectiveFar);
		}
	}
}