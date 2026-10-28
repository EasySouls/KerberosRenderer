#pragma once

#include "Core/Core.hpp"
#include "SkinningTypes.hpp"
#include "Renderer/ComputePipeline.hpp"
#include "Renderer/DescriptorAllocator.hpp"
#include "Renderer/Graph.hpp"
#include "Renderer/VulkanBuffer.hpp"
#include "Scene/Scene.hpp"

#include <limits>
#include <optional>
#include <unordered_map>
#include <vector>

namespace Kerberos {

class SkeletalMesh;

class SkinningSystem
{
public:
    void Initialize(const Owner<DescriptorAllocator>& allocator,
                    vk::Buffer skinMatricesBuffer,
                    vk::DeviceSize skinMatricesBufferSize);
    void Shutdown();

    void BeginFrame(const Ref<Scene>& scene, const SkinningFrameSnapshot& snapshot);
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
        uint64_t LastSkinnedVersion = std::numeric_limits<uint64_t>::max();
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
    void AllocateDescriptorSet();
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
    ShaderResourceSet m_SkinningSet{};

    VulkanBuffer m_InputVerticesBuffer{};
    VulkanBuffer m_OutputVerticesBuffer{};
    VulkanBuffer m_JointIndicesBuffer{};
    VulkanBuffer m_JointWeightsBuffer{};
    VulkanBuffer m_SharedIndexBuffer{};

    // Owned by AnimationSystem, referenced by the skinning descriptor set.
    vk::Buffer m_SkinMatricesBuffer = nullptr;
    vk::DeviceSize m_SkinMatricesBufferSize = 0;

    uint32_t m_NextSourceVertex = 0;
    uint32_t m_NextIndex = 0;
    uint32_t m_NextOutputVertex = 0;
    MeshInstanceId m_NextMeshInstanceId = 0;

    std::unordered_map<AssetHandle, MeshGpuSlice> m_MeshSlices;
    std::unordered_map<UUID, MeshInstance> m_Instances;
    std::vector<SkinJob> m_FrameJobs;
};

}
