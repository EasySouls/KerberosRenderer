#pragma once

#include "Assets/AssetRegistryTypes.hpp"
#include "AssetMeta.hpp"

#include <filesystem>
#include <string_view>

namespace Kerberos {

enum class AssetStaleReason : uint8_t
{
	None = 0,
	SourceChanged,
	SettingsChanged,
	OutputMissing,
	RegistryMissing,
	Forced,
};

class AssetStalenessEvaluator
{
public:
	static AssetStaleReason Evaluate(const std::filesystem::path& sourcePath,
		const std::filesystem::path& cacheRoot,
		const AssetMetaFile& meta,
		bool registryComplete,
		bool forceRebuild = false);

	static std::string_view ToString(AssetStaleReason reason);
};

}
