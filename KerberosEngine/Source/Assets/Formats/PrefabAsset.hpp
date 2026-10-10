#pragma once

#include "Assets/Asset.hpp"
#include "Scene/Components.hpp"
#include "Scene/Components/PhysicsComponents.hpp"

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace Kerberos {

using PrefabLocalIndex = uint32_t;
static constexpr PrefabLocalIndex InvalidPrefabLocalIndex = std::numeric_limits<uint32_t>::max();

struct SkinComponentTemplate
{
    AssetHandle SkeletonAsset = AssetHandle::Invalid();
    std::vector<PrefabLocalIndex> JointEntityIndices; // prefab-local refs, remapped on instantiate
    std::vector<UUID> JointEntityIDs; // legacy UUID refs
};

struct SkeletalMeshComponentTemplate
{
    AssetHandle MeshAsset = AssetHandle::Invalid();
    AssetHandle SkeletonAsset = AssetHandle::Invalid();
    AssetHandle MaterialAsset = AssetHandle::Invalid();
    bool Visible = true;
    bool CastShadows = true;
};

struct AnimationComponentTemplate
{
    AssetHandle AnimationAsset = AssetHandle::Invalid();
    float PlaybackSpeed = 1.0f;
    bool Loop = true;
    bool AutoPlay = true;
};

struct PrefabEntityTemplate
{
    PrefabLocalIndex LocalIndex = InvalidPrefabLocalIndex;
    UUID ID = UUID::Invalid(); // legacy prefab reference
    std::string Name;
    std::string Tag; // legacy name
    PrefabLocalIndex ParentLocalIndex = InvalidPrefabLocalIndex;
    std::vector<PrefabLocalIndex> Children;
    UUID Parent = UUID::Invalid(); // legacy prefab reference
    std::vector<UUID> ChildIDs; // legacy prefab references

    TransformComponent Transform;
    glm::vec3 Translation = glm::vec3(0.0f);
    glm::vec3 EulerRotation = glm::vec3(0.0f);

    std::optional<SkinComponentTemplate> Skin;
    std::optional<SkeletalMeshComponentTemplate> SkeletalMesh;
    std::optional<AnimationComponentTemplate> Animation;
    std::optional<RigidBody3DComponent> RigidBody;
    std::optional<BoxCollider3DComponent> BoxCollider;
    std::optional<SphereCollider3DComponent> SphereCollider;
    std::optional<CapsuleCollider3DComponent> CapsuleCollider;

    // Legacy static mesh fields are retained for old prefab files.
    std::string MeshAssetPath;
    std::string MaterialAssetPath;
    bool HasStaticMesh = false;
    AssetHandle StaticMeshAsset = AssetHandle::Invalid();
    AssetHandle StaticMaterialAsset = AssetHandle::Invalid();

    void ToEntity(Entity newEntity) const
    {
        auto& transform = newEntity.GetComponent<TransformComponent>();
        transform.Translation = Transform.Translation;
        transform.Rotation = Transform.Rotation;
        transform.Scale = Transform.Scale;
        transform.WorldTransform = glm::mat4(1.0f);
        if (SkeletalMesh) 
        {
            auto& c = newEntity.AddComponent<SkeletalMeshComponent>();
            c.MeshAsset = SkeletalMesh->MeshAsset;
            c.SkeletonAsset = SkeletalMesh->SkeletonAsset;
            c.MaterialAsset = SkeletalMesh->MaterialAsset;
            c.Visible = SkeletalMesh->Visible;
            c.CastShadows = SkeletalMesh->CastShadows;
        }
        if (Animation) 
        {
            auto& c = newEntity.AddComponent<AnimationComponent>();
            c.AnimationAsset = Animation->AnimationAsset;
            c.PlaybackSpeed = Animation->PlaybackSpeed;
            c.IsLooping = Animation->Loop;
            c.IsPlaying = Animation->AutoPlay;
            c.CurrentTime = 0.0f;
        }
        if (Skin) 
        {
            auto& c = newEntity.AddComponent<SkinComponent>();
            c.SkeletonAsset = Skin->SkeletonAsset;
            c.JointMatrices.clear();
        }
        if (HasStaticMesh && StaticMeshAsset.IsValid())
        {
            auto& c = newEntity.AddComponent<StaticMeshComponent>();
            c.StaticMesh = AssetManager::ResolveMeshAsset(StaticMeshAsset);
            c.MeshMaterial = AssetManager::AsAsset<Material>(StaticMaterialAsset);
        }
        if (RigidBody)
        {
            auto& c = newEntity.AddComponent<RigidBody3DComponent>(RigidBody);
            c.RuntimeBody = nullptr;
            c.IsDirty = true;
        }
        if (BoxCollider)
        {
            auto& c = newEntity.AddComponent<BoxCollider3DComponent>(BoxCollider);
            c.RuntimeCollider = nullptr;
        }
        if (SphereCollider)
        {
            auto& c = newEntity.AddComponent<SphereCollider3DComponent>(SphereCollider);
            c.RuntimeCollider = nullptr;
        }
        if (CapsuleCollider)
        {
            auto& c = newEntity.AddComponent<CapsuleCollider3DComponent>(CapsuleCollider);
            c.RuntimeCollider = nullptr;
        }
    }
};

}