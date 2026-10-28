#include "SkinningSystem.hpp"

#include "VulkanContext.hpp"
#include "Renderer/DescriptorManager.hpp"
#include "Renderer/DescriptorWriter.hpp"
#include "Scene/Components/AnimationComponents.hpp"

import Kerberos;

namespace Kerberos {

void SkinningSystem::Initialize(const Owner<DescriptorAllocator>& allocator, const vk::Buffer& skinMatricesBuffer)
{
    m_DescriptorAllocator = allocator.get();

    m_SkinMatricesBuffer = skinMatricesBuffer;

    AllocateBuffers();
    SetupDescriptors();
    AllocateDescriptorBuffer();
    SetupPipeline();
}

void SkinningSystem::Shutdown()
{
    m_InputVerticesBuffer.Reset();
    m_OutputVerticesBuffer.Reset();
    m_JointIndicesBuffer.Reset();
    m_JointWeightsBuffer.Reset();
    m_SharedIndexBuffer.Reset();
}

void SkinningSystem::BeginFrame(const Ref<Scene>& scene, const SkinningFrameSnapshot& skinningSnapshot)
{
    m_SkinMatricesBuffer = skinningSnapshot.SkinMatrixBuffer;

    const auto skinnedMeshView = scene->m_Registry.view<SkinnedMeshComponent, TransformComponent>();
    for (const auto entity : skinnedMeshView)
    {
        const auto& [skinnedMesh, transform] = skinnedMeshView.get<SkinnedMeshComponent, TransformComponent>(entity);

        const SkeletalMesh* mesh = skinnedMesh.Mesh.get();
        if (mesh == nullptr)
        {
            continue;
        }

        const UUID skinnedMeshID = mesh->GetHandle();
        if (m_SkinnedDrawViews.contains(skinnedMeshID))
        {
            continue; // Already processed this mesh
        }

        SkinnedDrawView drawView{
            .Id = ,
            .VertexBuffer = m_OutputVerticesBuffer.Handle,
            .VertexOffsetBytes = ,
            .VertexCount = mesh->GetVertices().size(), 
            .IndexBuffer = m_SharedIndexBuffer.Handle,
            .IndexOffsetBytes = ,
            .IndexCount = ,
            .SkinVersion = ,
        };

        m_SkinnedDrawViews[skinnedMeshID] = drawView;
    }

}

void SkinningSystem::AddPasses(RenderGraph::Graph& graph, uint32_t /*frameIndex*/) const
{
    // TODO: Maybe wrong, we need to declare what state the resources are in when the pass starts

    constexpr RenderGraph::BufferState inputVerticesState{
        .Stages = vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eVertexShader,
        .Access = vk::AccessFlagBits2::eShaderRead, 
    };
    constexpr RenderGraph::BufferState outputVerticesState{
        .Stages = vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eVertexShader,
        .Access = vk::AccessFlagBits2::eShaderWrite,
    };
    constexpr RenderGraph::BufferState jointMatricesState{
        .Stages = vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eVertexShader,
        .Access = vk::AccessFlagBits2::eShaderRead,
    };
    constexpr RenderGraph::BufferState jointIndicesState{
        .Stages = vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eVertexShader,
        .Access = vk::AccessFlagBits2::eShaderRead,
    };
    constexpr RenderGraph::BufferState jointWeightsState{
        .Stages = vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eVertexShader,
        .Access = vk::AccessFlagBits2::eShaderRead,
    };

    const auto hInputVert = graph.ImportBuffer(m_InputVerticesBuffer.Handle, inputVerticesState);
    const auto hOutputVert = graph.ImportBuffer(m_OutputVerticesBuffer.Handle, outputVerticesState);
    const auto hSkinMat = graph.ImportBuffer(m_SkinMatricesBuffer, jointMatricesState);
    const auto hJointIdx = graph.ImportBuffer(m_JointIndicesBuffer.Handle, jointIndicesState);
    const auto hJointWeight = graph.ImportBuffer(m_JointWeightsBuffer.Handle, jointWeightsState);

    graph.AddPass("Skinning", [&](RenderGraph::Graph::PassBuilder& builder) -> RenderGraph::PassExecuteFunction 
    {
        // TODO: Maybe wrong, we need to declare what the pass does with the resources
        builder.Read(hInputVert, inputVerticesState.Stages, inputVerticesState.Access);
        builder.Read(hSkinMat, jointMatricesState.Stages, jointMatricesState.Access);
        builder.Read(hJointIdx, jointIndicesState.Stages, jointIndicesState.Access);
        builder.Read(hJointWeight, jointWeightsState.Stages, jointWeightsState.Access);

        builder.Write(hOutputVert, outputVerticesState.Stages, outputVerticesState.Access);

        return [this](const vk::raii::CommandBuffer& execCmd) 
        {
            m_SkinningPipeline->Bind(execCmd);

            DescriptorManager::BindSets(execCmd, vk::PipelineBindPoint::eCompute, m_SkinningPipelineLayout, 0, { m_SkinningSet });

            // TODO
            SkinningPushConstant pushConstants{};

            execCmd.pushConstants<SkinningPushConstant>(
                m_SkinningPipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, { pushConstants });

            // TODO: real dispatch count based on number of skinned meshes and their vertex counts
            execCmd.dispatch(1, 1, 1);
        };
    });
}

std::optional<SkinnedDrawView> SkinningSystem::FindMesh(const UUID meshEntity) const
{
    const auto it = m_SkinnedDrawViews.find(meshEntity);
    if (it != m_SkinnedDrawViews.end()) {
        return it->second;
    }
    return std::nullopt;
}

void SkinningSystem::RecompileShaders() const
{
    m_SkinningPipeline->Recompile();
}

void SkinningSystem::Reset()
{
    throw std::logic_error("Not implemented");
}

void SkinningSystem::AllocateBuffers()
{
    const auto& context = VulkanContext::Get();

    // Input Vertices Buffer
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = sizeof(Vertex) * MaxInputVertices;
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

        VkBuffer buffer = nullptr;
        if (vmaCreateBuffer(VulkanContext::Get().GetAllocator().get(),
                            &bufferInfo,
                            &allocInfo,
                            &buffer,
                            &m_InputVerticesBuffer.Allocation,
                            nullptr) != VK_SUCCESS)
        {
            KBRAssert(false, "Failed to create input vertices buffer!");
        }

        m_InputVerticesBuffer.Handle = vk::Buffer(buffer);
        m_InputVerticesBuffer.Size = bufferInfo.size;

        vmaMapMemory(VulkanContext::Get().GetAllocator().get(),
                     m_InputVerticesBuffer.Allocation,
                     &m_InputVerticesBuffer.MappedData);

        context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkBuffer>(m_InputVerticesBuffer.Handle)),
                                   vk::ObjectType::eBuffer,
                                   std::format("Input Vertices Buffer"));
        context.SetObjectDebugName(m_InputVerticesBuffer.Allocation,
                                   std::format("Input Vertices Buffer Allocation"));
    }

    // Output Vertices Buffer
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = sizeof(Vertex) * MaxOutputVertices;
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT 
            | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        VkBuffer buffer = nullptr;
        if (vmaCreateBuffer(VulkanContext::Get().GetAllocator().get(),
                            &bufferInfo,
                            &allocInfo,
                            &buffer,
                            &m_OutputVerticesBuffer.Allocation,
                            nullptr) != VK_SUCCESS)
        {
            KBRAssert(false, "Failed to create output vertices buffer!");
        }

        m_OutputVerticesBuffer.Handle = vk::Buffer(buffer);
        m_OutputVerticesBuffer.Size = bufferInfo.size;

        vmaMapMemory(VulkanContext::Get().GetAllocator().get(),
                     m_OutputVerticesBuffer.Allocation,
                     &m_OutputVerticesBuffer.MappedData);

        context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkBuffer>(m_OutputVerticesBuffer.Handle)),
                                   vk::ObjectType::eBuffer,
                                   std::format("Output Vertices Buffer"));
        context.SetObjectDebugName(m_OutputVerticesBuffer.Allocation, std::format("Output Vertices Buffer Allocation"));
    }

    // Joint Indices Buffer
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = sizeof(glm::uvec4) * MaxOutputVertices;
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        VkBuffer buffer = nullptr;
        if (vmaCreateBuffer(VulkanContext::Get().GetAllocator().get(),
                            &bufferInfo,
                            &allocInfo,
                            &buffer,
                            &m_JointIndicesBuffer.Allocation,
                            nullptr) != VK_SUCCESS)
        {
            KBRAssert(false, "Failed to create joint indices buffer!");
        }
        m_JointIndicesBuffer.Handle = vk::Buffer(buffer);
        m_JointWeightsBuffer.Size = bufferInfo.size;

        vmaMapMemory(VulkanContext::Get().GetAllocator().get(),
                     m_JointIndicesBuffer.Allocation,
                     &m_JointIndicesBuffer.MappedData);

        context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkBuffer>(m_JointIndicesBuffer.Handle)),
                                   vk::ObjectType::eBuffer,
                                   std::format("Joint Indices Buffer"));
        context.SetObjectDebugName(m_JointIndicesBuffer.Allocation, std::format("Joint Indices Buffer Allocation"));
    }

    // Joint Weights Buffer
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = sizeof(glm::vec4) * MaxOutputVertices;
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        VkBuffer buffer = nullptr;
        if (vmaCreateBuffer(VulkanContext::Get().GetAllocator().get(),
                            &bufferInfo,
                            &allocInfo,
                            &buffer,
                            &m_JointWeightsBuffer.Allocation,
                            nullptr) != VK_SUCCESS)
        {
            KBRAssert(false, "Failed to create joint weights buffer!");
        }
        m_JointWeightsBuffer.Handle = vk::Buffer(buffer);
        m_JointWeightsBuffer.Size = bufferInfo.size;

        vmaMapMemory(VulkanContext::Get().GetAllocator().get(),
                     m_JointWeightsBuffer.Allocation,
                     &m_JointWeightsBuffer.MappedData);

        context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkBuffer>(m_JointWeightsBuffer.Handle)),
                                   vk::ObjectType::eBuffer,
                                   std::format("Joint Weights Buffer"));
        context.SetObjectDebugName(m_JointWeightsBuffer.Allocation, std::format("Joint Weights Buffer Allocation"));
    }

    // Shared Index Buffer
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = sizeof(uint32_t) * MaxIndices;
        bufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT 
            | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        VkBuffer buffer = nullptr;
        if (vmaCreateBuffer(VulkanContext::Get().GetAllocator().get(),
                            &bufferInfo,
                            &allocInfo,
                            &buffer,
                            &m_SharedIndexBuffer.Allocation,
                            nullptr) != VK_SUCCESS)
        {
            KBRAssert(false, "Failed to create shared index buffer for skinned meshes!");
        }

        m_SharedIndexBuffer.Handle = vk::Buffer(buffer);
        m_SharedIndexBuffer.Size = bufferInfo.size;

        vmaMapMemory(
            VulkanContext::Get().GetAllocator().get(), m_SharedIndexBuffer.Allocation, &m_SharedIndexBuffer.MappedData);

        context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkBuffer>(m_SharedIndexBuffer.Handle)),
                                   vk::ObjectType::eBuffer,
                                   std::format("Shared Index Buffer"));
        context.SetObjectDebugName(m_SharedIndexBuffer.Allocation, std::format("Shared Index Buffer Allocation"));
    }
}

void SkinningSystem::SetupDescriptors()
{
    auto& context = VulkanContext::Get();
    const auto& device = context.GetDevice();

    const std::vector<vk::DescriptorSetLayoutBinding> bindings = {
        { .binding = 0,
          .descriptorType = vk::DescriptorType::eStorageBuffer,
          .descriptorCount = 1,
          .stageFlags = vk::ShaderStageFlagBits::eCompute | vk::ShaderStageFlagBits::eVertex }, // Input vertices
        { .binding = 1,
          .descriptorType = vk::DescriptorType::eStorageBuffer,
          .descriptorCount = 1,
          .stageFlags = vk::ShaderStageFlagBits::eCompute }, // Output skinned vertices
        { .binding = 2,
          .descriptorType = vk::DescriptorType::eStorageBuffer,
          .descriptorCount = 1,
          .stageFlags = vk::ShaderStageFlagBits::eCompute | vk::ShaderStageFlagBits::eVertex }, // Joint matrices
        { .binding = 3,
          .descriptorType = vk::DescriptorType::eStorageBuffer,
          .descriptorCount = 1,
          .stageFlags = vk::ShaderStageFlagBits::eCompute | vk::ShaderStageFlagBits::eVertex }, // Joint indices
        { .binding = 4,
          .descriptorType = vk::DescriptorType::eStorageBuffer,
          .descriptorCount = 1,
          .stageFlags = vk::ShaderStageFlagBits::eCompute | vk::ShaderStageFlagBits::eVertex }, // Joint weights
    };

    m_SkinningDescriptorSetLayout = DescriptorManager::CreateDescriptorSetLayout(bindings);
    context.SetObjectDebugName(m_SkinningDescriptorSetLayout, "Skinning Descriptor Set Layout");

    constexpr vk::PushConstantRange pushConstantRange{ .stageFlags = vk::ShaderStageFlagBits::eCompute,
                                                       .offset = 0,
                                                       .size = sizeof(SkinningPushConstant) };

    const vk::PipelineLayoutCreateInfo pipelineLayoutInfo{ .setLayoutCount = 1,
                                                           .pSetLayouts = &*m_SkinningDescriptorSetLayout,
                                                           .pushConstantRangeCount = 1,
                                                           .pPushConstantRanges = &pushConstantRange };

    m_SkinningPipelineLayout = vk::raii::PipelineLayout(device, pipelineLayoutInfo);
    context.SetObjectDebugName(m_SkinningPipelineLayout, "Skinning Pipeline Layout");
}

void SkinningSystem::AllocateDescriptorBuffer()
{
    KBRAssert(m_SkinningDescriptorSetLayout != nullptr,
              "Descriptor Set Layout must be created before allocating descriptor buffer!");
    KBRAssert(m_SkinMatricesBuffer != nullptr, "Skin Matrices Buffer must be set before allocating descriptor buffer!");

    m_SkinningSet = m_DescriptorAllocator->Allocate(m_SkinningDescriptorSetLayout, "Skinning Descriptor Set");

    DescriptorWriter writer(m_SkinningDescriptorSetLayout, m_SkinningSet);
    writer.WriteStorageBuffer(0, m_InputVerticesBuffer.Handle, sizeof(Vertex) * MaxInputVertices);
    writer.WriteStorageBuffer(1, m_OutputVerticesBuffer.Handle, sizeof(Vertex) * MaxOutputVertices);
    writer.WriteStorageBuffer(2, m_SkinMatricesBuffer, sizeof(glm::mat4) * MaxOutputVertices);
    writer.WriteStorageBuffer(3, m_JointIndicesBuffer.Handle, sizeof(glm::uvec4) * MaxOutputVertices);
    writer.WriteStorageBuffer(4, m_JointWeightsBuffer.Handle, sizeof(glm::vec4) * MaxOutputVertices);
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

}