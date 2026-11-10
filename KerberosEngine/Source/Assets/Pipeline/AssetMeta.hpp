#pragma once

#include "Assets/Asset.hpp"
#include "Assets/Formats/AnimationFormat.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace Kerberos {

enum class ImporterType : uint8_t
{
    None = 0,
    Texture2D,
    GLTFScene,
    Standalone,
    Material,
    AIStateTree,
};

enum class SubAssetKind : uint8_t
{
    Prefab = 0,
    Material,
    Mesh,
    Skeleton,
    AnimationClip,
    Texture,
    SkinnedMesh
};

struct SubAssetMetaEntry
{
    std::string LocalKey{};
    AssetHandle Handle = AssetHandle::Invalid();
    SubAssetKind Kind = SubAssetKind::Prefab;
};

struct AnimationClipMetaEntry
{
    std::vector<AnimationMarkerFile> Markers;
    std::vector<AnimationEventFile> Events;
};

struct AssetMetaFile
{
    static constexpr uint32_t CurrentSchemaVersion = 3;
    uint32_t SchemaVersion = CurrentSchemaVersion;
    /* The source asset file handle */
    AssetHandle SourceHandle = AssetHandle::Invalid();
    ImporterType ImporterType = ImporterType::None;
    uint32_t ImporterVersion = 0;
    /* Content hash for staleness checks */
    std::string SourceHash{};
    std::vector<SubAssetMetaEntry> SubAssets;

    /**
     * User-editable import options. `TextureSrgb` maps a texture sub-asset to
     * its sRGB color-space interpretation:
     *   "texture:<imageIndex>" for glTF-generated textures,
     *   "source"               for a standalone texture source.
     * An absent key means "use the importer default". Importers materialize
     * defaults into this map on first import.
     */
    std::map<std::string, bool> TextureSrgb;

    /**
     * Animation markers and events authored per clip. The key is the clip's stable
     * LocalKey / SubAssetKey, e.g. "animation:0". Editing these options requires a
     * reimport so the generated .kbranimation artifact picks them up.
     */
    std::map<std::string, AnimationClipMetaEntry> AnimationClips;
};

};
