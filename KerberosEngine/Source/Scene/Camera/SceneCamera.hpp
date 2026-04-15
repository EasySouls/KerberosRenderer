#pragma once

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

namespace Kerberos
{
	class SceneCamera
	{
	public:
		enum class ProjectionType : uint8_t
		{
			Perspective, Orthographic
		};

	public:
		SceneCamera();
		~SceneCamera() = default;

		SceneCamera(const SceneCamera& other) = default;
		SceneCamera(SceneCamera&& other) noexcept = default;
		SceneCamera& operator=(const SceneCamera& other) = default;
		SceneCamera& operator=(SceneCamera&& other) noexcept = default;

		void SetOrthographic(float size, float nearClip, float farClip);
		void SetPerspective(float fov, float nearClip, float farClip);
		void SetViewportSize(uint32_t width, uint32_t height);

		ProjectionType GetProjectionType() const { return m_ProjectionType; }
		float GetOrthographicSize() const { return m_OrthoSize; }
		float GetOrthographicNearClip() const { return m_OrthoNear; }
		float GetOrthographicFarClip() const { return m_OrthoFar; }
		float GetPerspectiveFov() const { return m_PerspectiveFov; }
		float GetPerspectiveNearClip() const { return m_PerspectiveNear; }
		float GetPerspectiveFarClip() const { return m_PerspectiveFar; }

		void SetProjectionType(const ProjectionType projection) { m_ProjectionType = projection; RecalculateProjection(); }
		void SetOrthographicSize(const float size) { m_OrthoSize = size; RecalculateProjection(); }
		void SetOrthographicNearClip(const float nearClip) { m_OrthoNear = nearClip; RecalculateProjection(); }
		void SetOrthographicFarClip(const float farClip) { m_OrthoFar = farClip; RecalculateProjection(); }
		void SetPerspectiveFov(const float fov) { m_PerspectiveFov = fov; RecalculateProjection(); }
		void SetPerspectiveNearClip(const float nearClip) { m_PerspectiveNear = nearClip; RecalculateProjection(); }
		void SetPerspectiveFarClip(const float farClip) { m_PerspectiveFar = farClip; RecalculateProjection(); }

	private:
		void RecalculateProjection();
		void RecalculateView();

	private:
		ProjectionType m_ProjectionType = ProjectionType::Orthographic;

		glm::mat4 m_View{ 1.0f };
		glm::mat4 m_Projection{ 1.0f };

		float m_OrthoSize = 10.0f;
		float m_OrthoNear = -1.0f;
		float m_OrthoFar = 1.0f;

		float m_PerspectiveFov = glm::radians(45.0f);
		float m_PerspectiveNear = 0.01f;
		float m_PerspectiveFar = 100.0f;

		float m_AspectRatio = 0.0f;
	};
}
