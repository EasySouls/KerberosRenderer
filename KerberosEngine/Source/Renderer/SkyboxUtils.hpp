#pragma once

#include "Textures/Texture2D.hpp"
#include "Textures/TextureCube.hpp"
#include "Mesh.hpp"

namespace Kerberos::SkyboxUtils
{
	void GenerateBRDFLUT(Texture2D& texture);

	void GenerateIrradianceCube(TextureCube& irradianceTexture, const vk::DescriptorImageInfo& envMapDescriptor, const Mesh& cubeMesh);

	void GeneratePrefilteredEnvMap(TextureCube& prefilteredEnvMap, const vk::DescriptorImageInfo& envMapDescriptor, const Mesh& cubeMesh);
}