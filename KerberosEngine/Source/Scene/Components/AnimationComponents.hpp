#pragma once

#include "Assets/Asset.hpp"
#include "Core/UUID.hpp"
#include "Renderer/Material.hpp"
#include "Renderer/Animation/AnimationClip.hpp"
#include "Renderer/Animation/SkeletalMesh.hpp"
#include "Renderer/Animation/Skeleton.hpp"
#include "Scene/AABB.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace Kerberos {

struct JointComponent
{
    uint32_t JointIndex = 0;
};

/// Attaches this entity to a joint of a skeleton. The animation pose remains authoritative,
/// the runtime writes only the entity's derived world transform, never its authored TRS.
struct SocketComponent
{
    UUID SkeletonEntity = UUID::Invalid();
    std::string JointName;
    glm::vec3 LocalTranslation = glm::vec3(0.0f);
    glm::vec3 LocalRotation = glm::vec3(0.0f);
    glm::vec3 LocalScale = glm::vec3(1.0f);

    SocketComponent() = default;
    SocketComponent(const SocketComponent&) = default;
    SocketComponent(SocketComponent&&) = default;
    SocketComponent& operator=(const SocketComponent&) = default;
    SocketComponent& operator=(SocketComponent&&) = default;
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

struct AnimatorComponent
{
    UUID SkeletonEntity = UUID::Invalid();

    std::vector<AssetHandle> AnimationClips;

    int32_t ActiveClip = -1;
    float CurrentTime = 0.0f;
    float PlaybackSpeed = 1.0f;
    bool IsPlaying = false;
    bool IsLooping = true;
    bool PlayOnStart = false;

    // Incremented whenever the animator's playback state changes, so systems can
    // detect changes without storing a copy of the previous state.
    // Not serialized.
    uint32_t PlaybackRevision = 0;

    // Transient cross-fade request consumed by AnimationRuntime on the next clock advance or
    // pose evaluation. Negative means no request. Not serialized.
    int32_t PendingCrossFadeClip = -1;
    float PendingCrossFadeDuration = 0.0f;
    bool PendingCrossFadeRestart = true;

    AnimatorComponent() = default;
    explicit AnimatorComponent(const UUID& skeletonEntity) : SkeletonEntity(skeletonEntity) {}

    AnimatorComponent(const UUID& skeletonEntity, const std::vector<AssetHandle>& animationClips)
        : SkeletonEntity(skeletonEntity), AnimationClips(animationClips) {}

    AnimatorComponent(const AnimatorComponent&) = default;
    AnimatorComponent(AnimatorComponent&&) = default;
    AnimatorComponent& operator=(const AnimatorComponent&) = default;
    AnimatorComponent& operator=(AnimatorComponent&&) = default;

    Ref<Skeleton> Skeleton = nullptr;
    std::vector<Ref<AnimationClip>> LoadedClips;
};

enum class RagdollState : std::uint8_t
{
    Animated = 0,
    BlendingToRagdoll,
    Ragdoll,
    BlendingToAnimation
};

/// Turns a skeleton entity into a physics-driven ragdoll. 
/// The runtime bodies and constraints are owned by RagdollSystem.
struct RagdollComponent
{
    /// Skeleton to simulate. When invalid, the component must live on the skeleton entity.
    UUID SkeletonEntity = UUID::Invalid();

    /// Seconds used to blend between the animation pose and the physics pose in both directions.
    float BlendDuration = 0.25f;

    /// Enter the ragdoll state as soon as the runtime starts.
    bool StartInRagdoll = false;

    /// Scales the generated capsule/sphere radii relative to the bone length.
    float JointRadiusScale = 1.0f;

    RagdollComponent() = default;
    explicit RagdollComponent(const UUID& skeletonEntity) : SkeletonEntity(skeletonEntity) {}
    RagdollComponent(const RagdollComponent&) = default;
    RagdollComponent(RagdollComponent&&) = default;
    RagdollComponent& operator=(const RagdollComponent&) = default;
    RagdollComponent& operator=(RagdollComponent&&) = default;
};

}