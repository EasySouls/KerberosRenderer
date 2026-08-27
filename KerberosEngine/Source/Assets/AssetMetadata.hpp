#pragma once

#include "Asset.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Kerberos
{
	struct AssetMetadata
	{
		AssetType Type = AssetType::Texture2D;
		std::filesystem::path Filepath;
		std::filesystem::path LibraryPath;
		AssetHandle RootSourceHandle = AssetHandle::Invalid();
		AssetHandle ParentHandle = AssetHandle::Invalid();
		std::string SubAssetKey;
		uint64_t ImportVersion = 1;
		std::vector<AssetHandle> Dependencies;
	};
}