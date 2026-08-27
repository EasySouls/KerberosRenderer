#include "kbrpch.hpp"
#include "AssetStalenessEvaluator.hpp"

#include "AssetMetaService.hpp"

namespace Kerberos {

AssetStaleReason AssetStalenessEvaluator::Evaluate(
	const std::filesystem::path& sourcePath,
	const std::filesystem::path& cacheRoot,
	const AssetMetaFile& meta,
	const bool registryComplete,
	const bool forceRebuild)
{
	if (forceRebuild)
		return AssetStaleReason::Forced;
	if (!std::filesystem::exists(sourcePath))
		return AssetStaleReason::SourceChanged;
	if (meta.SourceHash.empty() || meta.SourceHash != AssetMetaService::ComputeSourceHash(sourcePath))
		return AssetStaleReason::SourceChanged;
	if (!registryComplete)
		return AssetStaleReason::RegistryMissing;
	for (const auto& subAsset : meta.SubAssets)
	{
		if (!subAsset.Handle.IsValid())
			return AssetStaleReason::SettingsChanged;
		(void)cacheRoot;
	}
	return AssetStaleReason::None;
}

std::string_view AssetStalenessEvaluator::ToString(const AssetStaleReason reason)
{
	switch (reason)
	{
		case AssetStaleReason::None: return "None";
		case AssetStaleReason::SourceChanged: return "SourceChanged";
		case AssetStaleReason::SettingsChanged: return "SettingsChanged";
		case AssetStaleReason::OutputMissing: return "OutputMissing";
		case AssetStaleReason::RegistryMissing: return "RegistryMissing";
		case AssetStaleReason::Forced: return "Forced";
	}
	return "Unknown";
}

}
