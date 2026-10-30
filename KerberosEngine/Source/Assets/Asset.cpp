#include "Asset.hpp"

import Kerberos;

namespace Kerberos {

AssetType AssetTypeFromString(const std::string_view typeStr)
{
    if (typeStr == "Texture2D")
        return AssetType::Texture2D;
    if (typeStr == "TextureCube")
        return AssetType::TextureCube;
    if (typeStr == "Material")
        return AssetType::Material;
    if (typeStr == "Mesh")
        return AssetType::Mesh;
    if (typeStr == "Scene")
        return AssetType::Scene;
    if (typeStr == "Sound")
        return AssetType::Sound;
    if (typeStr == "Prefab")
        return AssetType::Prefab;
    if (typeStr == "Skeleton" || typeStr == "Skin") // "Skin" is the legacy serialized name
        return AssetType::Skeleton;
    if (typeStr == "SkinnedMesh")
        return AssetType::SkinnedMesh;
    if (typeStr == "Animation" || typeStr == "AnimationClip") // "AnimationClip" is the legacy serialized name
        return AssetType::Animation;

    KBRAssert("Unknown asset type: {0}", typeStr);
    return AssetType::Texture2D;
}

std::string_view AssetTypeToString(const AssetType type)
{
    switch (type) {
    case AssetType::Texture2D:
        return "Texture2D";
    case AssetType::TextureCube:
        return "TextureCube";
    case AssetType::Material:
        return "Material";
    case AssetType::Mesh:
        return "Mesh";
    case AssetType::Scene:
        return "Scene";
    case AssetType::Sound:
        return "Sound";
    case AssetType::ModelDEPRECATED:
        return "ModelDEPRECATED";
    case AssetType::Prefab:
        return "Prefab";
    case AssetType::Skeleton:
        return "Skeleton";
    case AssetType::SkinnedMesh:
        return "SkinnedMesh";
    case AssetType::Animation:
        return "Animation";
    default:
        break;
    }

    KBRAssert("Unknown Asset Type!");
    return "";
}

}