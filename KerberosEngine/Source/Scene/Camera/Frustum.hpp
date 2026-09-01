#pragma once

#include <glm/glm.hpp>

#include <vector>
#include <array>

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
}