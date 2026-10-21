#pragma once

#include "Asset.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Kerberos
{
	/// Identifies which content root owns an asset. Project assets live in the
	/// user's project; engine assets are packaged with the engine and are
	/// read-only from the editor's perspective.
	enum class AssetSourceRoot : uint8_t
	{
		Project = 0,
		Engine = 1
	};

	struct AssetMetadata
	{
		AssetType Type = AssetType::Texture2D;
		AssetSourceRoot SourceRoot = AssetSourceRoot::Project;
		std::filesystem::path Filepath;
		std::filesystem::path LibraryPath;
		AssetHandle RootSourceHandle = AssetHandle::Invalid();
		AssetHandle ParentHandle = AssetHandle::Invalid();
		std::string SubAssetKey;
		uint64_t ImportVersion = 1;
		std::vector<AssetHandle> Dependencies;
	};
}