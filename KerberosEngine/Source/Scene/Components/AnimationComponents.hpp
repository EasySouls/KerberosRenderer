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

    SkinComponent(const SkinComponent&) = default;
    SkinComponent(SkinComponent&&) = default;
    SkinComponent& operator=(const SkinComponent&) = default;
    SkinComponent& operator=(SkinComponent&&) = default;
};

struct SkeletalMeshComponent
{
    AssetHandle MeshAsset = AssetHandle::Invalid();
    AssetHandle SkeletonAsset = AssetHandle::Invalid();
    AssetHandle MaterialAsset = AssetHandle::Invalid();

    AABB WorldAABB;

    bool Visible = true;
    bool CastShadows = true;

    SkeletalMeshComponent() = default;
    explicit SkeletalMeshComponent(const AssetHandle& meshAsset, const AssetHandle& skeletonAsset, const AssetHandle& materialAsset)
        : MeshAsset(meshAsset), SkeletonAsset(skeletonAsset), MaterialAsset(materialAsset)
    {
    }

    SkeletalMeshComponent(const SkeletalMeshComponent&) = default;
    SkeletalMeshComponent(SkeletalMeshComponent&&) = default;
    SkeletalMeshComponent& operator=(const SkeletalMeshComponent&) = default;
    SkeletalMeshComponent& operator=(SkeletalMeshComponent&&) = default;
};

struct AnimationComponent
{
    AssetHandle AnimationAsset = AssetHandle::Invalid();

    float CurrentTime = 0.0f;
    float PlaybackSpeed = 1.0f;
    bool IsPlaying = false;
    bool IsLooping = true;

    AnimationComponent() = default;
    explicit AnimationComponent(const AssetHandle& animationAsset)
        : AnimationAsset(animationAsset)
    {
    }

    AnimationComponent(const AnimationComponent&) = default;
    AnimationComponent(AnimationComponent&&) = default;
    AnimationComponent& operator=(const AnimationComponent&) = default;
    AnimationComponent& operator=(AnimationComponent&&) = default;
};

}