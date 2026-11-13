#pragma once

#include "Camera/Frustum.hpp"

#include <glm/vec3.hpp>

namespace Kerberos
{
	struct AABB
	{
		glm::vec3 Min{ 0.0f, 0.0f, 0.0f };
		glm::vec3 Max{ 0.0f, 0.0f, 0.0f };

		AABB() = default;
		AABB(const glm::vec3& min, const glm::vec3& max);
		AABB(const glm::vec3& center, float halfSize);

		glm::vec3 GetCenter() const { return (Min + Max) * 0.5f; }
		glm::vec3 GetSize() const { return Max - Min; }

		bool Intersects(const AABB& other) const;
		bool Contains(const glm::vec3& point) const;

		bool IsInsideFrustum(const Frustum& frustum) const;
	    bool IsIntersectingSphere(const glm::vec3& center, float radius) const;
	};

	bool IsAABBInsideFrustum(const AABB& aabb, const Frustum& frustum);
    bool IsAABBIntersectingSphere(const AABB& aabb, const glm::vec3& center, float radius);
}