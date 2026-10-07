#pragma once

#include "Assets/Asset.hpp"
#include "Assets/AssetMetadata.hpp"
#include "Assets/AssetRegistry.hpp"

#include <filesystem>

namespace Kerberos {

class RuntimeAssetLoader
{
public:
	static Ref<Asset> Load(AssetHandle handle, const AssetMetadata& metadata,
		const std::filesystem::path& path, const AssetRegistry* registry = nullptr);
};

}
