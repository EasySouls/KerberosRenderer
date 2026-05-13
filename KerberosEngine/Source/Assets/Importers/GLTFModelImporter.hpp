#pragma once

#include "Core/Core.hpp"
#include "Assets/Asset.hpp"
#include "Assets/AssetMetadata.hpp"
#include "Assets/Model.hpp"
#include "Renderer/Material.hpp"
#include "Renderer/Mesh.hpp"

#include <filesystem>

namespace Kerberos
{
	class GLTFModelImporter
	{
	public:
		static Ref<Model> ImportModel(AssetHandle handle, const AssetMetadata& metadata);
		static Ref<Model> ImportModel(const std::filesystem::path& filepath);
	};
}
