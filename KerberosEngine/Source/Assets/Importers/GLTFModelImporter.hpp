#pragma once

#include "Core/Core.hpp"
#include "Assets/Asset.hpp"
#include "Assets/AssetMetadata.hpp"
#include "Renderer/Material.hpp"
#include "Renderer/Mesh.hpp"

#include <filesystem>

namespace Kerberos
{
	class GLTFModelImporter
	{
	public:
		static Ref<Mesh> ImportModel(AssetHandle handle, const AssetMetadata& metadata);
		static Ref<Mesh> ImportModel(const std::filesystem::path& filepath);
	};
}