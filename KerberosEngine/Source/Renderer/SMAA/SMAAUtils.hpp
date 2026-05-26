#pragma once

#include "Vulkan.hpp"
#include "Renderer/Textures/Texture2D.hpp"

#include <glm/vec2.hpp>

namespace Kerberos
{
	struct SMAAEdgeDetectionShaderData
	{
		glm::vec2 InverseTextureSize;
	};
	struct SMAABlendingWeightCalculationShaderData
	{
		glm::vec2 InverseTextureSize;
	};
	struct SMAANeighborhoodBlendingShaderData
	{
		glm::vec2 InverseTextureSize;
	};

	Ref<Texture2D> LoadSMAAAreaTexture();
	Ref<Texture2D> LoadSMAASearchTexture();
}