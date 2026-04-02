#pragma once

#include "Assets/AssetMetadata.hpp"
#include "Renderer/Material.hpp"

namespace Kerberos
{
	class MaterialImporter
	{
	public:
		static Ref<Material> ImportMaterial(AssetHandle handle, const AssetMetadata& metadata);
		static Ref<Material> ImportMaterial(const std::filesystem::path& filepath);
		static bool SaveMaterial(const std::filesystem::path& filepath, const Material& material);
	};
}
