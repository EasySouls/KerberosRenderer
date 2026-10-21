#pragma once

#include "Assets/Asset.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Kerberos {

enum class AssetBuildState : uint8_t
{
    Unknown = 0,
    UpToDate,
    Stale,
    Building,
    Failed,
};

struct AssetRegistryEntry
{
    AssetHandle Handle;
    AssetType Type;

    /** Relative to the asset directory */
    std::filesystem::path SourcePath;
    /** Relative to the cache directory */
    std::filesystem::path LibraryPath;

    /** Source file UUID from .meta */
    AssetHandle RootSourceHandle = AssetHandle::Invalid();
    /** Invalid for root, root handle for sub-assets */
    AssetHandle ParentHandle = AssetHandle::Invalid();

    /** Matches .meta LocalKey */
    std::string SubAssetKey;

    AssetBuildState BuildState = AssetBuildState::Unknown;
    uint64_t ImportVersion = 1;
};

struct AssetRegistryFile
{
    uint32_t SchemaVersion = 1;
    std::vector<AssetRegistryEntry> Entries;
};

};