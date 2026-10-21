#include "kbrpch.hpp"
#include "Frustum.hpp"

#include "Profiling/Profilers.hpp"
#include "Renderer/Renderer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Kerberos
{
	namespace
	{
		glm::mat4 CalculateLightSpaceMatrix(const glm::mat4& view,
		                                    const float fov,
		                                    const float aspectRatio,
		                                    const float nearPlane,
		                                    const float farPlane,
		                                    const glm::vec3& lightDir)
		{
			const glm::mat4 proj = glm::perspective(fov, aspectRatio, nearPlane, farPlane);

			const auto corners = GetFrustumCornersWorldSpace(proj, view);

			glm::vec3 center = glm::vec3(0, 0, 0);
			for (const auto& v : corners)
			{
				center += glm::vec3(v);
			}
			center /= static_cast<float>(corners.size());

			glm::vec3 lightUp = glm::vec3(0.0f, 1.0f, 0.0f);
			if (glm::abs(glm::dot(lightDir, lightUp)) > 0.99f)
			{
				lightUp = glm::vec3(1.0f, 0.0f, 0.0f);
			}

			const auto lightView = glm::lookAt(center + lightDir, center, lightUp);

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

			// Add a slight offset to prevent negative near plane bounding issues if scene bounds are close to near clip
			minZ = std::min(minZ, -100.0f);
			maxZ = std::max(maxZ, 100.0f);

			// Apply texel snapping
			const float orthoWidth = maxX - minX;
			const uint32_t shadowMapResolution = Renderer::GetShadowMapResolution();
			const float worldUnitsPerTexel = orthoWidth / static_cast<float>(shadowMapResolution);

			minX = std::floor(minX / worldUnitsPerTexel) * worldUnitsPerTexel;
			maxX = std::floor(maxX / worldUnitsPerTexel) * worldUnitsPerTexel;
			minY = std::floor(minY / worldUnitsPerTexel) * worldUnitsPerTexel;
			maxY = std::floor(maxY / worldUnitsPerTexel) * worldUnitsPerTexel;

			const glm::mat4 lightProjection = glm::ortho(minX, maxX, minY, maxY, minZ, maxZ);
			return lightProjection * lightView;
		}
	}

	void Frustum::Update(const glm::mat4& viewProj)
	{
		KBR_TRACY_FUNCTION();

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
		KBR_TRACY_FUNCTION();

		Frustum frustum;
		frustum.Update(viewProj);
		return frustum;
	}

	std::vector<glm::vec4> GetFrustumCornersWorldSpace(const glm::mat4& projview)
	{
		KBR_TRACY_FUNCTION();

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

	std::pair<std::vector<glm::mat4>, glm::vec4> CalculateLightSpaceMatrices(
		const glm::mat4& view,
		const float fov,
		const float aspectRatio,
		const float nearClip,
		const float farClip,
		const glm::vec3& lightDir,
		const std::function<glm::vec4(float)>& getCascadeSplits)
	{
		KBR_TRACY_FUNCTION();

		const glm::vec4 cascadeSplits = getCascadeSplits(farClip);

		std::vector<glm::mat4> matrices;
		for (uint32_t i = 0; i < 3 + 1; ++i)
		{
			if (i == 0)
			{
				matrices.push_back(CalculateLightSpaceMatrix(view, fov, aspectRatio, nearClip, cascadeSplits[i], lightDir));
			}
			else if (i < 3)
			{
				matrices.push_back(CalculateLightSpaceMatrix(view, fov, aspectRatio, cascadeSplits[i - 1], cascadeSplits[i], lightDir));
			}
			else
			{
				matrices.push_back(CalculateLightSpaceMatrix(view, fov, aspectRatio, cascadeSplits[i - 1], farClip, lightDir));
			}
		}

		return { matrices, cascadeSplits };
	}
}
