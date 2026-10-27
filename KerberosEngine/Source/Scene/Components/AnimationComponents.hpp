#pragma once

#include "Assets/Asset.hpp"
#include "Core/UUID.hpp"
#include "Renderer/Material.hpp"
#include "Renderer/Animation/AnimationClip.hpp"
#include "Renderer/Animation/SkeletalMesh.hpp"
#include "Renderer/Animation/Skeleton.hpp"
#include "Scene/AABB.hpp"

#include <glm/glm.hpp>

#include <vector>

namespace Kerberos {

struct JointComponent
{
    uint32_t JointIndex = 0;
};

struct SkeletonComponent
{
    AssetHandle SkeletonAsset = AssetHandle::Invalid();
    // Joint entities ordered by joint index in the skeleton asset.
    std::vector<UUID> JointEntities;

    SkeletonComponent() = default;
    explicit SkeletonComponent(const AssetHandle& skeletonAsset, const std::vector<UUID>& jointEntities) 
        : SkeletonAsset(skeletonAsset), JointEntities(jointEntities) 
    {}

    SkeletonComponent(const SkeletonComponent&) = default;
    SkeletonComponent(SkeletonComponent&&) = default;
    SkeletonComponent& operator=(const SkeletonComponent&) = default;
    SkeletonComponent& operator=(SkeletonComponent&&) = default;

    Ref<Skeleton> Skeleton = nullptr;
};

struct SkinnedMeshComponent
{
    AssetHandle MeshAsset = AssetHandle::Invalid();
    AssetHandle SkeletonAsset = AssetHandle::Invalid();
    AssetHandle MaterialAsset = AssetHandle::Invalid();

    AABB WorldAABB;

    bool Visible = true;
    bool CastShadows = true;

    SkinnedMeshComponent() = default;
    explicit SkinnedMeshComponent(const AssetHandle& meshAsset, const AssetHandle& skeletonAsset, const AssetHandle& materialAsset)
        : MeshAsset(meshAsset), SkeletonAsset(skeletonAsset), MaterialAsset(materialAsset)
    {
    }

    SkinnedMeshComponent(const SkinnedMeshComponent&) = default;
    SkinnedMeshComponent(SkinnedMeshComponent&&) = default;
    SkinnedMeshComponent& operator=(const SkinnedMeshComponent&) = default;
    SkinnedMeshComponent& operator=(SkinnedMeshComponent&&) = default;

    // Link to skeleton root.
    UUID SkeletonEntity = UUID::Invalid();
    Ref<SkeletalMesh> Mesh = nullptr;
    Ref<Material> Material = nullptr;
};

struct AnimationComponent
{
    /**
     * @brief Handle to the assigned animation asset.
     *
     * Refers specifically to an @ref Animation asset. Passing any other 
     * asset type will result in a runtime validation failure.
     * 
     * @see Animation
     * @see AssetHandle
     */
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

    Ref<AnimationClip> Clip = nullptr;
};

}