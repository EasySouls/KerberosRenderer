#include "kbrpch.hpp"
#include "Frustum.hpp"

namespace Kerberos
{
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
					const auto ndc = glm::vec4(2.0f * static_cast<float>(x) - 1.0f, 2.0f * static_cast<float>(y) - 1.0f, 2.0f * static_cast<float>(z) - 1.0f, 1.0f);
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
