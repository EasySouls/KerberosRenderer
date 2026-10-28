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

class AnimationSystem
{
public:
    void Initialize(const Owner<DescriptorAllocator>& allocator);
    void Shutdown();

    void BeginFrame(const Ref<Scene>& scene, float dt, bool paused);
    void AddPasses(RenderGraph::Graph& graph, uint32_t frameIndex);

    const SkinningFrameSnapshot& GetSkinningSnapshot() const;

    vk::Buffer GetSkinMatricesBuffer() const { return m_SkinMatricesBuffer.Handle; }
    vk::DeviceSize GetSkinMatricesBufferSize() const { return m_SkinMatricesBuffer.Size; }

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
    void WriteBindPose(const SkeletonInstanceState& instance);
    static BoneMatrix ToBoneMatrix(const glm::mat4& matrix);

private:
    DescriptorAllocator* m_DescriptorAllocator = nullptr;

    VulkanBuffer m_SkinMatricesBuffer{};
    uint32_t m_NextSkinMatrix = 0;
    SkeletonInstanceId m_NextInstanceId = 0;

    std::unordered_map<UUID, SkeletonInstanceState> m_Instances;
    std::vector<SkeletonFrame> m_FrameSkeletons;
    SkinningFrameSnapshot m_Snapshot{};
};

}
