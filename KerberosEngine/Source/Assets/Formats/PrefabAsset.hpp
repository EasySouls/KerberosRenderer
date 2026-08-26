#pragma once

#include "Assets/Asset.hpp"
#include "Scene/Components.hpp"

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace Kerberos {
using PrefabLocalIndex = uint32_t;
static constexpr PrefabLocalIndex InvalidPrefabLocalIndex = std::numeric_limits<uint32_t>::max();

struct SkinComponentTemplate
{
    AssetHandle SkeletonAsset = AssetHandle::Invalid();
    std::vector<PrefabLocalIndex> JointEntityIndices; // prefab-local refs, remapped on instantiate
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
    std::string Name;
    PrefabLocalIndex ParentLocalIndex = InvalidPrefabLocalIndex;
    std::vector<PrefabLocalIndex> Children;

    TransformComponent Transform;

    std::optional<SkinComponentTemplate> Skin;
    std::optional<SkeletalMeshComponentTemplate> SkeletalMesh;
    std::optional<AnimationComponentTemplate> Animation;
    std::optional<RigidBody3DComponent> RigidBody;
    std::optional<BoxCollider3DComponent> BoxCollider;
    std::optional<SphereCollider3DComponent> SphereCollider;
    std::optional<CapsuleCollider3DComponent> CapsuleCollider;
};

class PrefabAsset : public Asset
{
public:
    AssetType GetType() override
    {
        return AssetType::Prefab;
    }

    std::string Name;
    PrefabLocalIndex RootLocalIndex = InvalidPrefabLocalIndex;
    std::vector<PrefabEntityTemplate> Entities;
};

}