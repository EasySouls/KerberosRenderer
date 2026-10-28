#pragma once

#include "Core/Core.hpp"
#include "SkinningTypes.hpp"
#include "Renderer/Graph.hpp"
#include "Renderer/DescriptorAllocator.hpp"
#include "Renderer/ComputePipeline.hpp"
#include "Renderer/VulkanBuffer.hpp"
#include "Scene/Scene.hpp"

#include <unordered_map>

namespace Kerberos {

class SkinningSystem
{
public:
    void Initialize(const Owner<DescriptorAllocator>& allocator, const vk::Buffer& skinMatricesBuffer);
    void Shutdown();

    void BeginFrame(const Ref<Scene>& scene,
                    const SkinningFrameSnapshot&);

    void AddPasses(RenderGraph::Graph& graph, uint32_t frameIndex) const;

    std::optional<SkinnedDrawView> FindMesh(UUID meshEntity) const;

    void RecompileShaders() const;

    void Reset();

private:
    void AllocateBuffers();
    void SetupDescriptors();
    void AllocateDescriptorBuffer();
    void SetupPipeline();

private:
    DescriptorAllocator* m_DescriptorAllocator = nullptr;

    vk::raii::DescriptorSetLayout m_SkinningDescriptorSetLayout = nullptr;
    vk::raii::PipelineLayout m_SkinningPipelineLayout = nullptr;

    Ref<ComputePipeline> m_SkinningPipeline = nullptr;

    ShaderResourceSet m_SkinningSet{};

    static constexpr size_t MaxInputVertices = 1'000'000;
    static constexpr size_t MaxOutputVertices = 1'000'000;
    static constexpr size_t MaxIndices = 1'000'000;

    VulkanBuffer m_InputVerticesBuffer{};
    VulkanBuffer m_OutputVerticesBuffer{};
    VulkanBuffer m_JointIndicesBuffer{};
    VulkanBuffer m_JointWeightsBuffer{};

    VulkanBuffer m_SharedIndexBuffer{};

    // AnimationSystem owns the skinning matrices buffer, since it is responsible for calculating the skinning matrices
    // each frame.
    vk::Buffer m_SkinMatricesBuffer = nullptr;

    std::unordered_map<UUID, SkinnedDrawView> m_SkinnedDrawViews;

};

}