#include "SkinningSystem.hpp"

#include "Assets/AssetManager.hpp"
#include "Renderer/Animation/SkeletalMesh.hpp"
#include "Renderer/DescriptorManager.hpp"
#include "Renderer/DescriptorWriter.hpp"
#include "Profiling/Profilers.hpp"
#include "Renderer/Vertex.hpp"
#include "Scene/Components/AnimationComponents.hpp"
#include "Scene/Entity.hpp"
#include "VulkanContext.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <unordered_set>

import Kerberos;

namespace Kerberos {

namespace {
vk::BufferUsageFlags ToVkUsage(const VkBufferUsageFlags usage)
{
    return static_cast<vk::BufferUsageFlags>(usage);
}
}

void SkinningSystem::Initialize(const Owner<DescriptorAllocator>& allocator,
                                const vk::Buffer skinMatricesBuffer,
                                const vk::DeviceSize skinMatricesBufferSize)
{
    m_DescriptorAllocator = allocator.get();
    m_SkinMatricesBuffer = skinMatricesBuffer;
    m_SkinMatricesBufferSize = skinMatricesBufferSize;

    AllocateBuffers();
    SetupDescriptors();
    AllocateDescriptorSet();
    SetupPipeline();
}

void SkinningSystem::Shutdown()
{
    m_InputVerticesBuffer.Reset();
    m_OutputVerticesBuffer.Reset();
    m_JointIndicesBuffer.Reset();
    m_JointWeightsBuffer.Reset();
    m_SharedIndexBuffer.Reset();

    m_MeshSlices.clear();
    m_Instances.clear();
    m_FrameJobs.clear();
}

void SkinningSystem::BeginFrame(const Ref<Scene>& scene, const SkinningFrameSnapshot& snapshot)
{
    KBR_TRACY_FUNCTION();

    m_FrameJobs.clear();

    if (!scene)
        return;

    std::unordered_set<UUID> liveInstances;

    const auto view = scene->m_Registry.view<SkinnedMeshComponent>();
    for (const auto entity : view)
    {
        auto& component = view.get<SkinnedMeshComponent>(entity);

        if (!component.Mesh && component.MeshAsset.IsValid())
            component.Mesh = AssetManager::AsAsset<SkeletalMesh>(component.MeshAsset);

        if (!component.Mesh)
        {
            Log::CoreWarn("SkinnedMeshComponent on entity {} has no resolvable SkinnedMesh asset",
                          static_cast<uint32_t>(entity));
            continue;
        }

        AssetHandle meshKey = component.MeshAsset;
        if (!meshKey.IsValid())
            meshKey = component.Mesh->GetHandle();

        if (!meshKey.IsValid())
        {
            Log::CoreError("SkinnedMeshComponent on entity {} has neither a valid asset handle nor a mesh handle",
                           static_cast<uint32_t>(entity));
            continue;
        }

        auto sliceIt = m_MeshSlices.find(meshKey);
        if (sliceIt == m_MeshSlices.end())
        {
            UploadMesh(meshKey, component.Mesh);
            sliceIt = m_MeshSlices.find(meshKey);
            if (sliceIt == m_MeshSlices.end())
                continue;
        }

        const SkeletonFrame* skeletonFrame = nullptr;
        if (component.SkeletonEntity.IsValid())
            skeletonFrame = FindSkeletonFrame(snapshot, component.SkeletonEntity);

        // Fallback: use the nearest ancestor that owns a SkeletonComponent.
        if (skeletonFrame == nullptr)
        {
            UUID parent = scene->m_Registry.get<HierarchyComponent>(entity).Parent;
            while (parent.IsValid() && skeletonFrame == nullptr)
            {
                const Entity parentEntity = scene->GetEntityByUUID(parent);
                if (!parentEntity)
                    break;

                if (parentEntity.HasComponent<SkeletonComponent>())
                    skeletonFrame = FindSkeletonFrame(snapshot, parent);

                parent = parentEntity.GetComponent<HierarchyComponent>().Parent;
            }
        }

        if (skeletonFrame == nullptr)
        {
            Log::CoreWarn("SkinnedMeshComponent on entity {} has no matching skeleton instance",
                          static_cast<uint32_t>(entity));
            continue;
        }

        const UUID entityUUID = scene->m_Registry.get<IDComponent>(entity).ID;

        auto [instanceIt, inserted] = m_Instances.try_emplace(entityUUID);
        MeshInstance& instance = instanceIt->second;

        if (inserted)
        {
            instance.Entity = entityUUID;
            instance.Id = m_NextMeshInstanceId++;
            instance.MeshAsset = meshKey;
            instance.Slice = sliceIt->second;
            instance.OutputFirstVertex = AllocateOutputRegion(sliceIt->second.VertexCount);
            instance.OutputVertexCount = sliceIt->second.VertexCount;

            if (instance.OutputFirstVertex == std::numeric_limits<uint32_t>::max())
            {
                m_Instances.erase(instanceIt);
                continue;
            }
        }
        else
        {
            instance.Slice = sliceIt->second;
        }

        instance.Slice = sliceIt->second;

        instance.View = {
            .Id = instance.Id,
            .VertexBuffer = m_OutputVerticesBuffer.Handle,
            .VertexOffsetBytes = static_cast<vk::DeviceSize>(instance.OutputFirstVertex) * sizeof(Vertex),
            .VertexCount = instance.OutputVertexCount,
            .IndexBuffer = m_SharedIndexBuffer.Handle,
            .IndexOffsetBytes = static_cast<vk::DeviceSize>(instance.Slice.FirstIndex) * sizeof(uint32_t),
            .IndexCount = instance.Slice.IndexCount,
            .SkinVersion = skeletonFrame->SkinMatrixVersion,
        };

        if (!instance.Initialized || instance.LastSkinnedVersion != skeletonFrame->SkinMatrixVersion)
        {
            m_FrameJobs.push_back({
                .SourceFirst = instance.Slice.FirstVertex,
                .VertexCount = instance.Slice.VertexCount,
                .OutputFirst = instance.OutputFirstVertex,
                .FirstSkinMatrix = skeletonFrame->FirstSkinMatrix,
                .JointCount = skeletonFrame->JointCount,
            });

            instance.LastSkinnedVersion = skeletonFrame->SkinMatrixVersion;
            instance.Initialized = true;
        }

        liveInstances.insert(entityUUID);
    }

    for (auto it = m_Instances.begin(); it != m_Instances.end();)
    {
        if (!liveInstances.contains(it->first))
        {
            // Output regions are returned in Reset() for the bind-pose milestone.
            it = m_Instances.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void SkinningSystem::AddPasses(RenderGraph::Graph& graph, const uint32_t frameIndex) const
{
    (void)frameIndex;

    if (m_FrameJobs.empty() || m_SkinningPipeline == nullptr)
        return;

    constexpr RenderGraph::BufferState readState{
        .Stages = vk::PipelineStageFlagBits2::eComputeShader,
        .Access = vk::AccessFlagBits2::eShaderRead,
    };
    constexpr RenderGraph::BufferState vertexReadState{
        .Stages = vk::PipelineStageFlagBits2::eVertexShader,
        .Access = vk::AccessFlagBits2::eShaderRead,
    };
    constexpr RenderGraph::BufferState writeState{
        .Stages = vk::PipelineStageFlagBits2::eComputeShader,
        .Access = vk::AccessFlagBits2::eShaderWrite,
    };

    const auto hInput = graph.ImportBuffer(m_InputVerticesBuffer.Handle, readState);
    const auto hOutput = graph.ImportBuffer(m_OutputVerticesBuffer.Handle, vertexReadState);
    const auto hJointIndices = graph.ImportBuffer(m_JointIndicesBuffer.Handle, readState);
    const auto hJointWeights = graph.ImportBuffer(m_JointWeightsBuffer.Handle, readState);
    const auto hSkinMatrices = graph.ImportBuffer(m_SkinMatricesBuffer, readState);

    graph.AddPass("Skinning", [&](RenderGraph::Graph::PassBuilder& builder) -> RenderGraph::PassExecuteFunction
    {
        builder.Read(hInput, readState.Stages, readState.Access);
        builder.Read(hJointIndices, readState.Stages, readState.Access);
        builder.Read(hJointWeights, readState.Stages, readState.Access);
        builder.Read(hSkinMatrices, readState.Stages, readState.Access);
        builder.Write(hOutput, writeState.Stages, writeState.Access);

        const auto jobs = m_FrameJobs;

        return [this, jobs](const vk::raii::CommandBuffer& cmd)
        {
            m_SkinningPipeline->Bind(cmd);

            const std::vector<ShaderResourceSet> sets{ m_SkinningSet };
            DescriptorManager::BindSets(cmd, vk::PipelineBindPoint::eCompute, m_SkinningPipelineLayout, 0, sets);

            for (const SkinJob& job : jobs)
            {
                const SkinningPushConstant push{
                    .sourceFirst = job.SourceFirst,
                    .outputFirst = job.OutputFirst,
                    .vertexCount = job.VertexCount,
                    .outputByteOffset = job.OutputFirst * static_cast<uint32_t>(sizeof(Vertex)),
                    .firstSkinMatrix = job.FirstSkinMatrix,
                    .jointCount = job.JointCount,
                };

                const std::array<SkinningPushConstant, 1> values{ push };
                cmd.pushConstants<SkinningPushConstant>(
                    m_SkinningPipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, values);

                cmd.dispatch((job.VertexCount + 63) / 64, 1, 1);
            }

            // The render graph does not know about the SVB as a vertex buffer, so make the
            // compute writes visible to the vertex input stage explicitly.
            vk::MemoryBarrier2 barrier{
                .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
                .srcAccessMask = vk::AccessFlagBits2::eShaderWrite,
                .dstStageMask = vk::PipelineStageFlagBits2::eVertexShader |
                    vk::PipelineStageFlagBits2::eDrawIndirect,
                .dstAccessMask = vk::AccessFlagBits2::eVertexAttributeRead |
                    vk::AccessFlagBits2::eShaderRead,
            };
            cmd.pipelineBarrier2({ .memoryBarrierCount = 1, .pMemoryBarriers = &barrier });
        };
    });
}

std::optional<SkinnedDrawView> SkinningSystem::FindMesh(const UUID meshEntity) const
{
    const auto it = m_Instances.find(meshEntity);
    if (it != m_Instances.end())
        return it->second.View;

    return std::nullopt;
}

void SkinningSystem::RecompileShaders() const
{
    if (m_SkinningPipeline)
        m_SkinningPipeline->Recompile();
}

void SkinningSystem::Reset()
{
    m_MeshSlices.clear();
    m_Instances.clear();
    m_FrameJobs.clear();

    m_NextSourceVertex = 0;
    m_NextIndex = 0;
    m_NextOutputVertex = 0;
    m_NextMeshInstanceId = 0;
}

void SkinningSystem::AllocateBuffers()
{
    CreateBuffer(m_InputVerticesBuffer,
                 static_cast<vk::DeviceSize>(MaxSourceVertices) * sizeof(Vertex),
                 ToVkUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                           VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT),
                 true,
                 "Skinning Input Vertices Buffer");

    CreateBuffer(m_JointIndicesBuffer,
                 static_cast<vk::DeviceSize>(MaxSourceVertices) * sizeof(glm::uvec4),
                 ToVkUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                           VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT),
                 true,
                 "Skinning Joint Indices Buffer");

    CreateBuffer(m_JointWeightsBuffer,
                 static_cast<vk::DeviceSize>(MaxSourceVertices) * sizeof(glm::vec4),
                 ToVkUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                           VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT),
                 true,
                 "Skinning Joint Weights Buffer");

    CreateBuffer(m_OutputVerticesBuffer,
                 static_cast<vk::DeviceSize>(MaxOutputVertices) * sizeof(Vertex),
                 ToVkUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                           VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT),
                 false,
                 "Skinning Shared Vertex Buffer");

    CreateBuffer(m_SharedIndexBuffer,
                 static_cast<vk::DeviceSize>(MaxIndices) * sizeof(uint32_t),
                 ToVkUsage(VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                           VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT),
                 true,
                 "Skinning Shared Index Buffer");
}

void SkinningSystem::SetupDescriptors()
{
    auto& context = VulkanContext::Get();
    const auto& device = context.GetDevice();

    const std::vector<vk::DescriptorSetLayoutBinding> bindings = {
        { .binding = 0,
          .descriptorType = vk::DescriptorType::eStorageBuffer,
          .descriptorCount = 1,
          .stageFlags = vk::ShaderStageFlagBits::eCompute }, // Input/source vertices
        { .binding = 1,
          .descriptorType = vk::DescriptorType::eStorageBuffer,
          .descriptorCount = 1,
          .stageFlags = vk::ShaderStageFlagBits::eCompute }, // Output/SVB
        { .binding = 2,
          .descriptorType = vk::DescriptorType::eStorageBuffer,
          .descriptorCount = 1,
          .stageFlags = vk::ShaderStageFlagBits::eCompute }, // Skin matrices (owned by AnimationSystem)
        { .binding = 3,
          .descriptorType = vk::DescriptorType::eStorageBuffer,
          .descriptorCount = 1,
          .stageFlags = vk::ShaderStageFlagBits::eCompute }, // Joint indices
        { .binding = 4,
          .descriptorType = vk::DescriptorType::eStorageBuffer,
          .descriptorCount = 1,
          .stageFlags = vk::ShaderStageFlagBits::eCompute }, // Joint weights
    };

    m_SkinningDescriptorSetLayout = DescriptorManager::CreateDescriptorSetLayout(bindings);
    context.SetObjectDebugName(m_SkinningDescriptorSetLayout, "Skinning Descriptor Set Layout");

    constexpr vk::PushConstantRange pushConstantRange{
        .stageFlags = vk::ShaderStageFlagBits::eCompute,
        .offset = 0,
        .size = sizeof(SkinningPushConstant),
    };

    const vk::PipelineLayoutCreateInfo pipelineLayoutInfo{
        .setLayoutCount = 1,
        .pSetLayouts = &*m_SkinningDescriptorSetLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushConstantRange,
    };

    m_SkinningPipelineLayout = vk::raii::PipelineLayout(device, pipelineLayoutInfo);
    context.SetObjectDebugName(m_SkinningPipelineLayout, "Skinning Pipeline Layout");
}

void SkinningSystem::AllocateDescriptorSet()
{
    KBRAssert(m_SkinningDescriptorSetLayout != nullptr,
              "Descriptor set layout must be created before allocating the skinning descriptor set");
    KBRAssert(m_SkinMatricesBuffer != nullptr,
              "AnimationSystem must provide the skin matrices buffer before the skinning descriptor is created");

    m_SkinningSet = m_DescriptorAllocator->Allocate(m_SkinningDescriptorSetLayout, "Skinning Descriptor Set");

    DescriptorWriter writer(m_SkinningDescriptorSetLayout, m_SkinningSet);
    writer.WriteStorageBuffer(0, m_InputVerticesBuffer.Handle, m_InputVerticesBuffer.Size);
    writer.WriteStorageBuffer(1, m_OutputVerticesBuffer.Handle, m_OutputVerticesBuffer.Size);
    writer.WriteStorageBuffer(2, m_SkinMatricesBuffer, m_SkinMatricesBufferSize);
    writer.WriteStorageBuffer(3, m_JointIndicesBuffer.Handle, m_JointIndicesBuffer.Size);
    writer.WriteStorageBuffer(4, m_JointWeightsBuffer.Handle, m_JointWeightsBuffer.Size);
    writer.Flush();
}

void SkinningSystem::SetupPipeline()
{
    const auto& context = VulkanContext::Get();

    ComputePipelineSpecification pipelineSpec{};
    pipelineSpec.Name = "Skinning Pipeline";
    pipelineSpec.Shader = CreateRef<Shader>("skinning", "Skinning");
    pipelineSpec.PipelineLayout = m_SkinningPipelineLayout;
    pipelineSpec.UsingDescriptorBuffers = context.UseDescriptorBuffers();

    m_SkinningPipeline = CreateRef<ComputePipeline>(pipelineSpec);
}

void SkinningSystem::UploadMesh(const AssetHandle handle, const Ref<SkeletalMesh>& mesh)
{
    if (!mesh)
        return;

    const auto& vertices = mesh->GetVertices();
    const auto& indices = mesh->GetIndices();

    if (vertices.empty() || indices.empty())
        return;

    if (m_NextSourceVertex + static_cast<uint32_t>(vertices.size()) > MaxSourceVertices ||
        m_NextIndex + static_cast<uint32_t>(indices.size()) > MaxIndices)
    {
        Log::CoreError("Skinning source buffers are full, cannot upload skinned mesh {}", handle);
        return;
    }

    const MeshGpuSlice slice{
        .FirstVertex = m_NextSourceVertex,
        .VertexCount = static_cast<uint32_t>(vertices.size()),
        .FirstIndex = m_NextIndex,
        .IndexCount = static_cast<uint32_t>(indices.size()),
    };

    auto* inputVertices = static_cast<Vertex*>(m_InputVerticesBuffer.MappedData) + slice.FirstVertex;
    auto* jointIndices = static_cast<glm::uvec4*>(m_JointIndicesBuffer.MappedData) + slice.FirstVertex;
    auto* jointWeights = static_cast<glm::vec4*>(m_JointWeightsBuffer.MappedData) + slice.FirstVertex;

    for (size_t i = 0; i < vertices.size(); ++i)
    {
        const auto& [Position, Normal, Tangent, TexCoord, JointIndices, JointWeights] = vertices[i];
        inputVertices[i] = Vertex{ .Position = Position, .Normal = Normal, .Tangent = Tangent, .TexCoord = TexCoord };
        jointIndices[i] = JointIndices;
        jointWeights[i] = JointWeights;
    }

    auto* destinationIndices = static_cast<uint32_t*>(m_SharedIndexBuffer.MappedData) + slice.FirstIndex;
    std::memcpy(destinationIndices, indices.data(), indices.size() * sizeof(uint32_t));

    FlushBuffer(m_InputVerticesBuffer,
                static_cast<vk::DeviceSize>(slice.FirstVertex) * sizeof(Vertex),
                static_cast<vk::DeviceSize>(slice.VertexCount) * sizeof(Vertex));
    FlushBuffer(m_JointIndicesBuffer,
                static_cast<vk::DeviceSize>(slice.FirstVertex) * sizeof(glm::uvec4),
                static_cast<vk::DeviceSize>(slice.VertexCount) * sizeof(glm::uvec4));
    FlushBuffer(m_JointWeightsBuffer,
                static_cast<vk::DeviceSize>(slice.FirstVertex) * sizeof(glm::vec4),
                static_cast<vk::DeviceSize>(slice.VertexCount) * sizeof(glm::vec4));
    FlushBuffer(m_SharedIndexBuffer,
                static_cast<vk::DeviceSize>(slice.FirstIndex) * sizeof(uint32_t),
                static_cast<vk::DeviceSize>(slice.IndexCount) * sizeof(uint32_t));

    m_NextSourceVertex += slice.VertexCount;
    m_NextIndex += slice.IndexCount;

    m_MeshSlices.emplace(handle, slice);
}

uint32_t SkinningSystem::AllocateOutputRegion(const uint32_t vertexCount)
{
    // Align each region to 4 vertices (192 bytes) so byte offsets are 64-byte aligned.
    const uint32_t alignedStart = (m_NextOutputVertex + 3u) & ~3u;
    if (alignedStart + vertexCount > MaxOutputVertices)
    {
        Log::CoreError("Skinning shared vertex buffer is full (requested {} vertices)", vertexCount);
        return std::numeric_limits<uint32_t>::max();
    }

    m_NextOutputVertex = alignedStart + vertexCount;
    return alignedStart;
}

const SkeletonFrame* SkinningSystem::FindSkeletonFrame(const SkinningFrameSnapshot& snapshot,
                                                       const UUID skeletonEntity)
{
    for (const SkeletonFrame& frame : snapshot.Skeletons)
    {
        if (frame.Entity == skeletonEntity)
            return &frame;
    }

    return nullptr;
}

void SkinningSystem::FlushBuffer(const VulkanBuffer& buffer, const vk::DeviceSize offset, const vk::DeviceSize size)
{
    if (buffer.Allocation == nullptr || size == 0)
        return;

    vmaFlushAllocation(VulkanContext::Get().GetAllocator().get(), buffer.Allocation, offset, size);
}

}
