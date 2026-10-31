#pragma once

#include "SkinningTypes.hpp"
#include "Renderer/DescriptorAllocator.hpp"
#include "Renderer/Graph.hpp"
#include "Renderer/VulkanBuffer.hpp"
#include "Scene/Scene.hpp"

#include <unordered_map>
#include <vector>

namespace Kerberos {

class Skeleton;

struct JointPose
{
    glm::vec3 Translation;
    glm::quat Rotation;
    glm::vec3 Scale;
};

struct SkeletonPose
{
    std::vector<JointPose> Local;
    std::vector<glm::mat4> Global;
    std::vector<glm::mat4> SkinMatrices;
};

struct SkeletonInstanceState
{
    UUID Entity;
    AssetHandle SkeletonAsset;
    Ref<Skeleton> Skeleton;

    uint32_t FirstSkinMatrix;
    uint32_t JointCount;
    uint64_t SkinMatrixVersion;

    SkeletonPose Pose;

    // Per-clip cache
    std::vector<int32_t> TrackLookup; 
};

class AnimationSystem
{
public:
    void Initialize(const Owner<DescriptorAllocator>& allocator);
    void Shutdown();

    void BeginFrame(const Ref<Scene>& scene, uint32_t frameIndex,float dt, bool paused);
    void AddPasses(RenderGraph::Graph& graph, uint32_t frameIndex);

    const SkinningFrameSnapshot& GetSkinningSnapshot() const;

    vk::Buffer GetSkinMatricesBuffer(uint32_t frameIndex) const;
    vk::DeviceSize GetSkinMatricesBufferSize(uint32_t frameIndex) const;

    void RecompileShaders() const;
    void Reset();

private:
    struct SkeletonInstanceState
    {
        SkeletonInstanceId Id = 0;
        UUID Entity = UUID::Invalid();
        AssetHandle SkeletonAsset = AssetHandle::Invalid();
        uint32_t FirstSkinMatrix = 0;
        uint32_t JointCount = 0;
        uint64_t SkinMatrixVersion = 0;

        Ref<Skeleton> Skeleton = nullptr;
    };

    static constexpr uint32_t MaxSkinMatrixJoints = 262144; // ~12.5 MB of BoneMatrix data

    void AllocateSkinMatricesBuffer();
    void WriteBindPose(const SkeletonInstanceState& instance, uint32_t frameIndex);
    static BoneMatrix ToBoneMatrix(const glm::mat4& matrix);

private:
    DescriptorAllocator* m_DescriptorAllocator = nullptr;

    std::vector<VulkanBuffer> m_SkinMatricesBuffers;
    uint32_t m_NextSkinMatrix = 0;
    SkeletonInstanceId m_NextInstanceId = 0;

    std::unordered_map<UUID, SkeletonInstanceState> m_Instances;
    std::vector<SkeletonFrame> m_FrameSkeletons;
    SkinningFrameSnapshot m_Snapshot{};
};

}
