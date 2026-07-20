#include "kbrpch.hpp"
#include "Frustum.hpp"

namespace Kerberos
{
	void Frustum::Update(const glm::mat4& viewProj)
	{
		Planes[0].Normal.x = viewProj[0][3] + viewProj[0][0];
		Planes[0].Normal.y = viewProj[1][3] + viewProj[1][0];
        Planes[0].Normal.z = viewProj[2][3] + viewProj[2][0];
        Planes[0].Distance = viewProj[3][3] + viewProj[3][0];

        Planes[1].Normal.x = viewProj[0][3] - viewProj[0][0];
        Planes[1].Normal.y = viewProj[1][3] - viewProj[1][0];
        Planes[1].Normal.z = viewProj[2][3] - viewProj[2][0];
        Planes[1].Distance = viewProj[3][3] - viewProj[3][0];

        Planes[2].Normal.x = viewProj[0][3] + viewProj[0][1];
        Planes[2].Normal.y = viewProj[1][3] + viewProj[1][1];
        Planes[2].Normal.z = viewProj[2][3] + viewProj[2][1];
        Planes[2].Distance = viewProj[3][3] + viewProj[3][1];

        Planes[3].Normal.x = viewProj[0][3] - viewProj[0][1];
        Planes[3].Normal.y = viewProj[1][3] - viewProj[1][1];
        Planes[3].Normal.z = viewProj[2][3] - viewProj[2][1];
        Planes[3].Distance = viewProj[3][3] - viewProj[3][1];

        Planes[4].Normal.x = viewProj[0][2];
        Planes[4].Normal.y = viewProj[1][2];
        Planes[4].Normal.z = viewProj[2][2];
        Planes[4].Distance = viewProj[3][2];

        Planes[5].Normal.x = viewProj[0][3] - viewProj[0][2];
        Planes[5].Normal.y = viewProj[1][3] - viewProj[1][2];
        Planes[5].Normal.z = viewProj[2][3] - viewProj[2][2];
        Planes[5].Distance = viewProj[3][3] - viewProj[3][2];

		for (auto& plane : Planes)
		{
			plane.Normalize();
		}
	}

	Frustum Frustum::CreateFromViewProjection(const glm::mat4& viewProj)
	{
		Frustum frustum;
		frustum.Update(viewProj);
		return frustum;
	}

	std::vector<glm::vec4> GetFrustumCornersWorldSpace(const glm::mat4& projview)
	{
		const auto inv = glm::inverse(projview);

		std::vector<glm::vec4> frustumCorners;
		for (uint32_t x = 0; x < 2; ++x)
		{
			for (uint32_t y = 0; y < 2; ++y)
			{
				for (uint32_t z = 0; z < 2; ++z)
				{
					const auto ndc = glm::vec4(2.0f * static_cast<float>(x) - 1.0f, 2.0f * static_cast<float>(y) - 1.0f, static_cast<float>(z), 1.0f);
					const glm::vec4 pt = inv * ndc;
					frustumCorners.push_back(pt / pt.w);
				}
			}
		}

		return frustumCorners;
	}

    std::vector<glm::vec4> GetFrustumCornersWorldSpace(const glm::mat4& proj, const glm::mat4& view)
    {
        return GetFrustumCornersWorldSpace(proj * view);
    }
}
