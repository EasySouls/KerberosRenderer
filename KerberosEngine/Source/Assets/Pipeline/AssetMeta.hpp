#pragma once

#include "Assets/Asset.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace Kerberos {

enum class ImporterType : uint8_t
{
    None = 0,
    Texture2D,
    GLTFScene,
};

enum class SubAssetKind : uint8_t
{
    Prefab = 0,
    Material,
    Mesh,
    Skeleton,
    AnimationClip,
    Texture,
};

struct SubAssetMetaEntry
{
    std::string LocalKey{};
    AssetHandle Handle = AssetHandle::Invalid();
    SubAssetKind Kind = SubAssetKind::Prefab;
};

struct AssetMetaFile
{
    static constexpr uint32_t CurrentSchemaVersion = 1;
    uint32_t SchemaVersion = CurrentSchemaVersion;
    /* The source asset file handle */
    AssetHandle SourceHandle = AssetHandle::Invalid();
    ImporterType ImporterType = ImporterType::None;
    uint32_t ImporterVersion = 0;
    /* Content hash for staleness checks */
    std::string SourceHash{};
    std::vector<SubAssetMetaEntry> SubAssets;
};

};