#pragma once

#include "Core/UUID.hpp"
#include "Assets/Asset.hpp"
#include "Scene/AABB.hpp"
#include "Renderer/Material.hpp"
#include "Renderer/SkeletalMesh.hpp"

#include <glm/mat4.hpp>

#include <vector>

namespace Kerberos
{
class Texture2D;

struct SkeletalMeshComponent
{
    AssetHandle SkinAsset = AssetHandle::Invalid();
    std::vector<UUID> Joints;

    std::vector<glm::mat4> JointMatrices;

    Ref<SkeletalMesh> Mesh = nullptr;
    Ref<Material> MeshMaterial = nullptr;

    AABB WorldAABB;

    bool Visible = true;
    bool CastShadows = true;

    SkeletalMeshComponent() = default;

    SkeletalMeshComponent(const Ref<SkeletalMesh>& mesh, const Ref<Material>& material)
        : SkeletalMesh(mesh), MeshMaterial(material)
    {
    }
    SkeletalMeshComponent(const SkeletalMeshComponent&) = default;
};

struct AnimationComponent
{
    AssetHandle AnimationAsset = AssetHandle::Invalid();

    float CurrentTime = 0.0f;
    bool IsPlaying = false;
    bool IsLooping = false;
};

}