#pragma once

#include "Assets/Asset.hpp"
#include "Core/UUID.hpp"
#include "Renderer/Material.hpp"
#include "Renderer/SkeletalMesh.hpp"
#include "Scene/AABB.hpp"

#include <glm/glm.hpp>

#include <vector>

namespace Kerberos {

struct SkinComponent
{
    AssetHandle SkeletonAsset = AssetHandle::Invalid();
    std::vector<UUID> JointEntities;
    std::vector<glm::mat4> JointMatrices;

    SkinComponent() = default;
    explicit SkinComponent(const AssetHandle& skeletonAsset) : SkeletonAsset(skeletonAsset) {}
};

struct SkeletalMeshComponent
{
    AssetHandle MeshAsset = AssetHandle::Invalid();
    AssetHandle SkeletonAsset = AssetHandle::Invalid();
    AssetHandle MaterialAsset = AssetHandle::Invalid();

    AABB WorldAABB;

    bool Visible = true;
    bool CastShadows = true;
};

struct AnimationComponent
{
    AssetHandle AnimationAsset = AssetHandle::Invalid();

    float CurrentTime = 0.0f;
    float PlaybackSpeed = 1.0f;
    bool IsPlaying = false;
    bool IsLooping = true;
};

}