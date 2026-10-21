#pragma once

#include <glm/glm.hpp>

#include <vector>
#include <array>
#include <functional>
#include <utility>

namespace Kerberos
{
	struct Plane
	{
		glm::vec3 Normal{0.0f, 1.0f, 0.0f};
		float Distance{ 0.0f };

		void Normalize()
		{
			const float length = glm::length(Normal);
			Normal /= length;
			Distance /= length;
		}
	};

	struct Frustum
	{
		std::array<Plane, 6> Planes;

		void Update(const glm::mat4& viewProj);

		static Frustum CreateFromViewProjection(const glm::mat4& viewProj);
	};

	std::vector<glm::vec4> GetFrustumCornersWorldSpace(const glm::mat4& projview);
	std::vector<glm::vec4> GetFrustumCornersWorldSpace(const glm::mat4& proj, const glm::mat4& view);

	/**
	 * Builds a single light-space matrix for the given view frustum slice.
	 * The result is fitted to the frustum and texel-snapped to the shadow map
	 * resolution so cascades do not swim while the camera moves.
	 *
	 * @param fov The vertical field of view in radians.
	 * @param aspectRatio The camera aspect ratio.
	 */
	glm::mat4 CalculateLightSpaceMatrix(
		const glm::mat4& view,
		float fov,
		float aspectRatio,
		float nearClip,
		float farClip,
		const glm::vec3& lightDir);

	/**
	 * Builds the cascaded light-space matrices from a camera view matrix.
	 *
	 * @param fov The vertical field of view in radians.
	 * @param aspectRatio The camera aspect ratio.
	 */
	std::pair<std::vector<glm::mat4>, glm::vec4> CalculateLightSpaceMatrices(
		const glm::mat4& view,
		float fov,
		float aspectRatio,
		float nearClip,
		float farClip,
		const glm::vec3& lightDir,
		const std::function<glm::vec4(float)>& getCascadeSplits);
}