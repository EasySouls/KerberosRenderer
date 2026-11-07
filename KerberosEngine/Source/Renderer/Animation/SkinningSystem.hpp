#pragma once

#include "Core/Core.hpp"
#include "SkinningTypes.hpp"
#include "Renderer/ComputePipeline.hpp"
#include "Renderer/DescriptorAllocator.hpp"
#include "Renderer/Graph.hpp"
#include "Renderer/VulkanBuffer.hpp"
#include "Scene/Scene.hpp"
#include "VulkanContext.hpp"

#include <array>
#include <optional>
#include <unordered_map>
#include <vector>

namespace Kerberos {

class SkeletalMesh;

class SkinningSystem
{
public:
    void Initialize(const Owner<DescriptorAllocator>& allocator,
                    const std::vector<vk::Buffer>& skinMatricesBuffers,
                    const std::vector<vk::DeviceSize>& skinMatricesBufferSizes);
    void Shutdown();

    void BeginFrame(const Ref<Scene>& scene, const SkinningFrameSnapshot& snapshot, uint32_t frameIndex);
    void AddPasses(RenderGraph::Graph& graph, uint32_t frameIndex) const;

    std::optional<SkinnedDrawView> FindMesh(UUID meshEntity) const;

    void RecompileShaders() const;
    void Reset();

private:
    struct MeshGpuSlice
    {
        uint32_t FirstVertex = 0;
        uint32_t VertexCount = 0;
        uint32_t FirstIndex = 0;
        uint32_t IndexCount = 0;
    };

    struct MeshInstance
    {
        UUID Entity = UUID::Invalid();
        MeshInstanceId Id = 0;
        AssetHandle MeshAsset = AssetHandle::Invalid();
        MeshGpuSlice Slice{};
        uint32_t OutputFirstVertex = 0;
        uint32_t OutputVertexCount = 0;
        // Pose versions are >= 1, so zero means "never skinned on this frame slot".
        std::array<uint64_t, VulkanContext::MaxFramesInFlight> LastSkinnedVersions{};
        bool Initialized = false;
        SkinnedDrawView View{};
    };

    struct SkinJob
    {
        uint32_t SourceFirst = 0;
        uint32_t VertexCount = 0;
        uint32_t OutputFirst = 0;
        uint32_t FirstSkinMatrix = 0;
        uint32_t JointCount = 0;
    };

    static constexpr uint32_t MaxSourceVertices = 262144;
    static constexpr uint32_t MaxIndices = 1048576;
    static constexpr uint32_t MaxOutputVertices = 262144;

    void AllocateBuffers();
    void SetupDescriptors();
    void AllocateDescriptorSets();
    void SetupPipeline();

    void UploadMesh(AssetHandle handle, const Ref<SkeletalMesh>& mesh);
    uint32_t AllocateOutputRegion(uint32_t vertexCount);
    static const SkeletonFrame* FindSkeletonFrame(const SkinningFrameSnapshot& snapshot, UUID skeletonEntity);
    static void FlushBuffer(const VulkanBuffer& buffer, vk::DeviceSize offset, vk::DeviceSize size);

private:
    DescriptorAllocator* m_DescriptorAllocator = nullptr;

    vk::raii::DescriptorSetLayout m_SkinningDescriptorSetLayout = nullptr;
    vk::raii::PipelineLayout m_SkinningPipelineLayout = nullptr;

    Ref<ComputePipeline> m_SkinningPipeline = nullptr;
    std::vector<ShaderResourceSet> m_SkinningSets;

    VulkanBuffer m_InputVerticesBuffer{};
    VulkanBuffer m_OutputVerticesBuffer{};
    VulkanBuffer m_JointIndicesBuffer{};
    VulkanBuffer m_JointWeightsBuffer{};
    VulkanBuffer m_SharedIndexBuffer{};

    // Owned by AnimationSystem, referenced by the per-frame skinning descriptor sets.
    std::vector<vk::Buffer> m_SkinMatricesBuffers;
    std::vector<vk::DeviceSize> m_SkinMatricesBufferSizes;

    uint32_t m_NextSourceVertex = 0;
    uint32_t m_NextIndex = 0;
    uint32_t m_NextOutputVertex = 0;
    MeshInstanceId m_NextMeshInstanceId = 0;

    std::unordered_map<AssetHandle, MeshGpuSlice> m_MeshSlices;
    std::unordered_map<UUID, MeshInstance> m_Instances;
    std::vector<SkinJob> m_FrameJobs;
};

}
