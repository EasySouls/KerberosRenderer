#pragma once

#include "Textures.hpp"
#include "Mesh.hpp"

namespace Kerberos::SkyboxUtils
{
	void GenerateBRDFLUT(Texture2D& texture);

	void GenerateIrradianceCube(TextureCube& irradianceTexture, const vk::DescriptorImageInfo& envMapDescriptor, const Mesh& cubeMesh);

	void GeneratePrefilteredEnvMap(TextureCube& prefilteredEnvMap, const vk::DescriptorImageInfo& envMapDescriptor, const Mesh& cubeMesh);
}