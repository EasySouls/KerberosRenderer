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
    if (typeStr == "Model")
        return AssetType::Model;
    if (typeStr == "Prefab")
        return AssetType::Prefab;
    if (typeStr == "Animation")
        return AssetType::Animation;
    if (typeStr == "Skin")
        return AssetType::Skin;

    KBRAssert(false, "Unknown asset type: {0}", typeStr);
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
    case AssetType::Model:
        return "Model";
    case AssetType::Prefab:
        return "Prefab";
    case AssetType::Animation:
        return "Animation";
    case AssetType::Skin:
        return "Skin";
    }

    KBRAssert(false, "Unknown Asset Type!");
    return "";
}

}