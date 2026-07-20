#include "kbrpch.hpp"
#include "AABB.hpp"

namespace Kerberos
{
	AABB::AABB(const glm::vec3& min, const glm::vec3& max) 
		: Min(min), Max(max) 
	{}

	AABB::AABB(const glm::vec3& center, const float halfSize) : 
		Min(center - glm::vec3(halfSize)), Max(center + glm::vec3(halfSize)) 
	{}

	bool AABB::Intersects(const AABB& other) const
	{
		return (Min.x <= other.Max.x && Max.x >= other.Min.x) &&
			(Min.y <= other.Max.y && Max.y >= other.Min.y) &&
			(Min.z <= other.Max.z && Max.z >= other.Min.z);
	}

	bool AABB::Contains(const glm::vec3& point) const
	{
		return (point.x >= Min.x && point.x <= Max.x) &&
			(point.y >= Min.y && point.y <= Max.y) &&
			(point.z >= Min.z && point.z <= Max.z);
	}

	bool AABB::IsInsideFrustum(const Frustum& frustum) const
	{
		return IsAABBInsideFrustum(*this, frustum);
	}

	bool IsAABBInsideFrustum(const AABB& aabb, const Frustum& frustum)
	{
		for (const auto& [Normal, Distance] : frustum.Planes)
		{
			glm::vec3 positiveVertex = aabb.Min;
			if (Normal.x >= 0) positiveVertex.x = aabb.Max.x;
			if (Normal.y >= 0) positiveVertex.y = aabb.Max.y;
			if (Normal.z >= 0) positiveVertex.z = aabb.Max.z;
			if (glm::dot(Normal, positiveVertex) + Distance < 0)
			{
				return false;
			}
		}
		return true;
	}
}