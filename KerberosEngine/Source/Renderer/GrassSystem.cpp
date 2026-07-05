#include "kbrpch.hpp"
#include "GrassSystem.hpp"

#include "VulkanContext.hpp"
#include "Utils.hpp"
#include "Shaders/SlangCompiler.hpp"

namespace
{
    template <typename Container>
    constexpr size_t byte_size(const Container& container)
    {
        return container.size() * sizeof(typename Container::value_type);
    }
}

namespace Kerberos
{
	GrassSystem::~GrassSystem()
    {
        Cleanup();
	}

	void GrassSystem::Init()
	{
		InitGrassDataBuffer();
		InitDescriptorLayouts();
		InitShaderObjects();
		InitDescriptorBuffer();
	}

	void GrassSystem::RecordDraw(const vk::raii::CommandBuffer& cmd, uint32_t frameIndex, const GrassConstants& constants)
	{
		SetDefaultGraphicsState(cmd);

		constexpr std::array unusedStages = {
			vk::ShaderStageFlagBits::eVertex,
			vk::ShaderStageFlagBits::eGeometry
		};

        constexpr std::array<vk::ShaderEXT, 2> unusedShaders = { nullptr, nullptr };

        cmd.bindShadersEXT(unusedStages, unusedShaders);

		cmd.bindShadersEXT(
			{ vk::ShaderStageFlagBits::eTaskEXT, vk::ShaderStageFlagBits::eMeshEXT, vk::ShaderStageFlagBits::eFragment },
			{ m_TaskShader, m_MeshShader, m_FragShader }
		);

		const vk::DescriptorBufferBindingInfoEXT bindingInfo{
			.address = m_DescriptorBufferAddress,
			.usage = vk::BufferUsageFlagBits::eResourceDescriptorBufferEXT
		};
		cmd.bindDescriptorBuffersEXT(bindingInfo);

		uint32_t bufferIndex = 0;
		vk::DeviceSize offset = 0;
		cmd.setDescriptorBufferOffsetsEXT(
			vk::PipelineBindPoint::eGraphics,
			*m_PipelineLayout,
			0,
            { bufferIndex },
			{ offset }
		);

		cmd.pushConstants<GrassConstants>(
			*m_PipelineLayout,
			vk::ShaderStageFlagBits::eTaskEXT | vk::ShaderStageFlagBits::eMeshEXT,
			0,
            { constants }
		);

		constexpr uint32_t totalGrassChunks = 100;
		cmd.drawMeshTasksEXT(totalGrassChunks, 1, 1);
	}

    void GrassSystem::InitGrassDataBuffer()
    {
		auto& context = VulkanContext::Get();
        const VmaAllocator allocator = context.GetAllocator().get();
        const auto& device = context.GetDevice();

		std::vector<GrassChunk> initialChunks;
		initialChunks.push_back({ glm::vec4(0.0f, 0.0f, 0.0f, 10.0f), glm::vec4(1.0f, 0.0f, 0.0f, 0.0f) });
		initialChunks.push_back({ glm::vec4(20.0f, 0.0f, 20.0f, 10.0f), glm::vec4(1.0f, 1.0f, 0.0f, 0.0f) });
		initialChunks.push_back({ glm::vec4(-20.0f, 0.0f, -20.0f, 10.0f), glm::vec4(1.0f, 2.0f, 0.0f, 0.0f) });

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = sizeof(GrassChunk) * MaxGrassCount;

        // Storage Buffer for the shaders to read, Device Address for descriptor buffers
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
            VK_BUFFER_USAGE_TRANSFER_DST_BIT |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        // If terrain is entirely static, VMA_MEMORY_USAGE_GPU_ONLY + staging buffer is better.
        // Using CPU_TO_GPU here for simplicity and dynamic terrain updates.
        allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

        VkBuffer buffer = nullptr;
        if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &buffer, &m_ChunkBuffer.allocation, nullptr) != VK_SUCCESS)
            KBR_CORE_ASSERT(false, "Failed to create Grass Chunk SSBO!");

        m_ChunkBuffer.Handle = vk::Buffer(buffer);
        vmaMapMemory(allocator, m_ChunkBuffer.allocation, &m_ChunkBuffer.MappedData);

        std::memcpy(m_ChunkBuffer.MappedData, initialChunks.data(), byte_size(initialChunks));

        const vk::BufferDeviceAddressInfo addressInfo{ .buffer = m_ChunkBuffer.Handle };
		m_ChunkBuffer.DeviceAddress = device.getBufferAddress(addressInfo);

		context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkBuffer>(m_ChunkBuffer.Handle)), vk::ObjectType::eBuffer, "Grass Chunk Buffer");
    }

    void GrassSystem::InitDescriptorLayouts()
    {
        auto& context = VulkanContext::Get();
        const auto& device = context.GetDevice();

        static constexpr vk::DescriptorSetLayoutBinding chunkBufferBinding{
            .binding = 0,
            .descriptorType = vk::DescriptorType::eStorageBuffer,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eTaskEXT | vk::ShaderStageFlagBits::eMeshEXT
        };

        constexpr vk::DescriptorSetLayoutCreateInfo layoutInfo{
            .flags = vk::DescriptorSetLayoutCreateFlagBits::eDescriptorBufferEXT,
            .bindingCount = 1,
            .pBindings = &chunkBufferBinding
        };
        m_SetLayout = vk::raii::DescriptorSetLayout(device, layoutInfo);
		context.SetObjectDebugName(m_SetLayout, "Grass Descriptor Set Layout");

        m_PushConstantRange = vk::PushConstantRange{
			.stageFlags = vk::ShaderStageFlagBits::eTaskEXT | vk::ShaderStageFlagBits::eMeshEXT,
			.offset = 0,
			.size = sizeof(GrassConstants)
		};

        const vk::PipelineLayoutCreateInfo pipelineLayoutInfo{
            .flags = vk::PipelineLayoutCreateFlags{},
            .setLayoutCount = 1,
            .pSetLayouts = &*m_SetLayout,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &m_PushConstantRange
        };
        m_PipelineLayout = vk::raii::PipelineLayout(device, pipelineLayoutInfo);
		context.SetObjectDebugName(m_PipelineLayout, "Grass Pipeline Layout");
    }

    void GrassSystem::InitShaderObjects()
    {
        auto& context = VulkanContext::Get();
        const auto& device = context.GetDevice();

        const auto taskSpv = SlangCompiler::CompileToSpirv("grass.slang", {ShaderEntryPoint::Task});
        const auto meshSpv = SlangCompiler::CompileToSpirv("grass.slang", {ShaderEntryPoint::Mesh});
        const auto fragSpv = SlangCompiler::CompileToSpirv("grass.slang", {ShaderEntryPoint::Fragment});

        const vk::DescriptorSetLayout setLayouts[] = { *m_SetLayout };

        const std::vector<vk::ShaderCreateInfoEXT> shaderInfos = {
            // Task Shader
            vk::ShaderCreateInfoEXT{
                .flags = vk::ShaderCreateFlagBitsEXT::eLinkStage,
                .stage = vk::ShaderStageFlagBits::eTaskEXT,
                .nextStage = vk::ShaderStageFlagBits::eMeshEXT,
                .codeType = vk::ShaderCodeTypeEXT::eSpirv,
                .codeSize = taskSpv.size() * sizeof(uint32_t),
                .pCode = taskSpv.data(),
                .pName = "taskMain",
                .setLayoutCount = 1,
                .pSetLayouts = setLayouts,
                .pushConstantRangeCount = 1,
                .pPushConstantRanges = &m_PushConstantRange
            },
            // Mesh Shader
            vk::ShaderCreateInfoEXT{
                .flags = vk::ShaderCreateFlagBitsEXT::eLinkStage,
                .stage = vk::ShaderStageFlagBits::eMeshEXT,
                .nextStage = vk::ShaderStageFlagBits::eFragment,
                .codeType = vk::ShaderCodeTypeEXT::eSpirv,
                .codeSize = meshSpv.size() * sizeof(uint32_t),
                .pCode = meshSpv.data(),
                .pName = "meshMain",
                .setLayoutCount = 1,
                .pSetLayouts = setLayouts,
                .pushConstantRangeCount = 1,
                .pPushConstantRanges = &m_PushConstantRange
            },
            // Fragment Shader
            vk::ShaderCreateInfoEXT{
                .flags = vk::ShaderCreateFlagBitsEXT::eLinkStage,
                .stage = vk::ShaderStageFlagBits::eFragment,
                .nextStage = {},
                .codeType = vk::ShaderCodeTypeEXT::eSpirv,
                .codeSize = fragSpv.size() * sizeof(uint32_t),
                .pCode = fragSpv.data(),
                .pName = "fragmentMain",
                .setLayoutCount = 1,
                .pSetLayouts = setLayouts,
                .pushConstantRangeCount = 1,
                .pPushConstantRanges = &m_PushConstantRange
            }
        };

        std::vector<vk::raii::ShaderEXT> shaders = device.createShadersEXT(shaderInfos);

        m_TaskShader = std::move(shaders[0]);
		context.SetObjectDebugName(m_TaskShader, "Grass Task Shader");

        m_MeshShader = std::move(shaders[1]);
        context.SetObjectDebugName(m_MeshShader, "Grass Mesh Shader");

        m_FragShader = std::move(shaders[2]);
        context.SetObjectDebugName(m_FragShader, "Grass Fragment Shader");
    }

    void GrassSystem::InitDescriptorBuffer()
    {
		auto& context = VulkanContext::Get();
		const auto& device = context.GetDevice();
		const auto& physicalDevice = context.GetPhysicalDevice();

        const auto props = physicalDevice.getProperties2<
            vk::PhysicalDeviceProperties2,
            vk::PhysicalDeviceDescriptorBufferPropertiesEXT>();
        const vk::PhysicalDeviceDescriptorBufferPropertiesEXT descProps = props.get<vk::PhysicalDeviceDescriptorBufferPropertiesEXT>();

        const size_t descriptorSize = descProps.storageBufferDescriptorSize;

        const size_t bufferSize = (descriptorSize + descProps.descriptorBufferOffsetAlignment - 1)
            & ~(descProps.descriptorBufferOffsetAlignment - 1);

        const vk::BufferCreateInfo bufferInfo{
            .size = bufferSize,
            .usage = vk::BufferUsageFlagBits::eResourceDescriptorBufferEXT | vk::BufferUsageFlagBits::eShaderDeviceAddress
        };
        m_DescriptorBuffer = vk::raii::Buffer(device, bufferInfo);
        context.SetObjectDebugName(m_DescriptorBuffer, "Grass Descriptor Buffer");

        const vk::MemoryRequirements memReq = m_DescriptorBuffer.getMemoryRequirements();
		constexpr vk::MemoryAllocateFlagsInfo flagsInfo{ .flags = vk::MemoryAllocateFlagBits::eDeviceAddress };
        const vk::MemoryAllocateInfo allocInfo{
            .pNext = &flagsInfo,
            .allocationSize = memReq.size,
            .memoryTypeIndex = FindMemoryType(memReq.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent)
        };

        m_DescriptorMemory = vk::raii::DeviceMemory(device, allocInfo);
        context.SetObjectDebugName(m_DescriptorMemory, "Grass Descriptor Memory");
        m_DescriptorBuffer.bindMemory(*m_DescriptorMemory, 0);

        m_DescriptorBufferAddress = device.getBufferAddress({ .buffer = *m_DescriptorBuffer });

        void* mappedData = m_DescriptorMemory.mapMemory(0, bufferSize);

        const vk::DescriptorAddressInfoEXT addrInfo{
            .address = m_ChunkBuffer.DeviceAddress,
            .range = MaxGrassCount * sizeof(GrassChunk),
            .format = vk::Format::eUndefined
        };
        const vk::DescriptorGetInfoEXT getInfo{
            .type = vk::DescriptorType::eStorageBuffer,
            .data = vk::DescriptorDataEXT(&addrInfo)
        };

        device.getDescriptorEXT(getInfo, descriptorSize, mappedData);

        m_DescriptorMemory.unmapMemory();
    }

    void GrassSystem::SetDefaultGraphicsState(const vk::raii::CommandBuffer& cmd)
    {
        cmd.setRasterizerDiscardEnable(false);
        cmd.setCullMode(vk::CullModeFlagBits::eNone);
        cmd.setFrontFace(vk::FrontFace::eCounterClockwise);
        cmd.setDepthBiasEnable(false);

        cmd.setRasterizationSamplesEXT(vk::SampleCountFlagBits::e1);

        vk::SampleMask sampleMask = 0xFFFFFFFF;
        cmd.setSampleMaskEXT(vk::SampleCountFlagBits::e1, { sampleMask });

        cmd.setAlphaToCoverageEnableEXT(false);
        cmd.setAlphaToOneEnableEXT(false);

        cmd.setDepthTestEnable(true);
        cmd.setDepthWriteEnable(true);
        cmd.setDepthCompareOp(vk::CompareOp::eLessOrEqual);
        cmd.setDepthBoundsTestEnable(false);
        cmd.setStencilTestEnable(false);
        cmd.setDepthClampEnableEXT(false);

		cmd.setPolygonModeEXT(vk::PolygonMode::eFill);

        cmd.setColorBlendEnableEXT(0, { false });
        cmd.setColorWriteMaskEXT(0, {
            vk::ColorComponentFlagBits::eR |
            vk::ColorComponentFlagBits::eG |
            vk::ColorComponentFlagBits::eB |
            vk::ColorComponentFlagBits::eA
                                 });
        cmd.setLogicOpEnableEXT(false);
    }

    void GrassSystem::Cleanup() const
    {
        const auto allocator = VulkanContext::Get().GetAllocator().get();

        vmaUnmapMemory(allocator, m_ChunkBuffer.allocation);
        vmaDestroyBuffer(allocator, m_ChunkBuffer.Handle, m_ChunkBuffer.allocation);
    }
}
