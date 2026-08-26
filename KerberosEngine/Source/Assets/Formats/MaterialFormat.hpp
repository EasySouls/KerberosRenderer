#pragma once

#include "Assets/Asset.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>

namespace Kerberos {

struct MaterialFile
{
    std::string Name;
    glm::vec4 BaseColorFactor = glm::vec4(1.0f);
    float MetallicFactor = 1.0f;
    float RoughnessFactor = 1.0f;

    AssetHandle BaseColorTexture = AssetHandle::Invalid();
    AssetHandle NormalTexture = AssetHandle::Invalid();
    AssetHandle MetallicRoughnessTexture = AssetHandle::Invalid();
    AssetHandle OcclusionTexture = AssetHandle::Invalid();
    AssetHandle EmissiveTexture = AssetHandle::Invalid();
};

}