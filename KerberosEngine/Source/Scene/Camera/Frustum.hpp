#pragma once

#include <glm/glm.hpp>

#include <vector>

namespace Kerberos
{
	std::vector<glm::vec4> GetFrustumCornersWorldSpace(const glm::mat4& projview);
	std::vector<glm::vec4> GetFrustumCornersWorldSpace(const glm::mat4& proj, const glm::mat4& view);
}