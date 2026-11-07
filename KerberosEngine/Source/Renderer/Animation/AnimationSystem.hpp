#pragma once

#include "SkinningTypes.hpp"
#include "Renderer/DescriptorAllocator.hpp"
#include "Renderer/Graph.hpp"
#include "Renderer/VulkanBuffer.hpp"
#include "Scene/Scene.hpp"
#include "VulkanContext.hpp"

#include <array>
#include <unordered_map>
#include <vector>

namespace Kerberos {

class Skeleton;

class AnimationSystem
{
public:
    void Initialize(const Owner<DescriptorAllocator>& allocator);
    void Shutdown();

    void BeginFrame(const Ref<Scene>& scene, uint32_t frameIndex);
    void AddPasses(RenderGraph::Graph& graph, uint32_t frameIndex);

    const SkinningFrameSnapshot& GetSkinningSnapshot() const;

    vk::Buffer GetSkinMatricesBuffer(uint32_t frameIndex) const;
    vk::DeviceSize GetSkinMatricesBufferSize(uint32_t frameIndex) const;
    std::vector<vk::Buffer> GetSkinMatricesBufferHandles() const;
    std::vector<vk::DeviceSize> GetSkinMatricesBufferSizes() const;

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
        Ref<Skeleton> Skeleton = nullptr;

        // Last pose version uploaded into each frame-in-flight slot.
        std::array<uint64_t, VulkanContext::MaxFramesInFlight> FrameVersions{};
    };

    static constexpr uint32_t MaxSkinMatrixJoints = 262144; // ~12.5 MB of BoneMatrix data

    void AllocateSkinMatricesBuffer();
    void
    UploadSkinMatrices(SkeletonInstanceState& instance, const AnimationInstance& animation, uint32_t frameIndex) const;
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
