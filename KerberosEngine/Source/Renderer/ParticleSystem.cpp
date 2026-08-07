#include "kbrpch.hpp"
#include "ParticleSystem.hpp"

#include "Scene/Scene.hpp"
#include "Scene/Components/ParticleComponents.hpp"
#include "VulkanContext.hpp"
#include "DescriptorManager.hpp"
#include "DescriptorWriter.hpp"

#include <glm/gtc/random.hpp>

#include <array>
#include <format>

namespace
{
	struct alignas(16) GPUParticle
	{
		// Must stay in lockstep with particles.slang::Particle.
		glm::vec3 Position;
		float     Size;

		glm::vec3 Velocity;
		float     Life;

		glm::vec4 Color;

		glm::vec3 Acceleration;
		float     MaxLife;

		glm::vec4 StartColor;
		glm::vec4 EndColor;

		float     StartSize;
		float     EndSize;
		float     _Padding[2]{ 0.0f, 0.0f };
	};

	struct alignas(16) SpawnRequest
	{
		glm::vec3 emitterPosition;
		uint32_t  spawnCount;

		float     minLife;
		float     maxLife;
		glm::vec3 minVelocity;
		[[maybe_unused]] float     _pad0;
		glm::vec3 maxVelocity;
		[[maybe_unused]] float     _pad1;

		glm::vec3 minAcceleration;
		[[maybe_unused]] float     _pad2;
		glm::vec3 maxAcceleration;
		[[maybe_unused]] float     _pad3;

		glm::vec4 startColor;
		glm::vec4 endColor;

		float     startSize;
		float     endSize;
		[[maybe_unused]] glm::vec2 _pad4;
	};

	struct Counters
	{
		uint32_t DeadCount;
		uint32_t AliveCount;
		uint32_t SpawnRequestCount;
		uint32_t IndirectDrawCount;
	};
}

namespace Kerberos
{
#define USING_MANUAL_DESCRIPTOR_ALLOCATION 0

	ParticleSystem::ParticleSystem()
		: m_ParticlePoolBuffer(sizeof(GPUParticle) * MaxParticles)
		, m_DeadListBuffer(sizeof(uint32_t) * MaxParticles)
		, m_AliveListBuffer(sizeof(uint32_t) * MaxParticles)
		, m_CountersBuffer(sizeof(Counters))
		, m_ParticleFrameBuffers(VulkanContext::Get().GetMaxFramesInFlight())
#if USING_MANUAL_DESCRIPTOR_ALLOCATION
		, m_DescriptorBuffers(VulkanContext::Get().GetMaxFramesInFlight())
#endif
		, m_IndirectDrawBuffers(VulkanContext::Get().GetMaxFramesInFlight())
	{
		const auto& context = VulkanContext::Get();

		AllocateParticleFrameBuffers();
		AllocateIndirectDrawBuffers();

		for (uint32_t i = 0; i < VulkanContext::Get().GetMaxFramesInFlight(); ++i)
		{
			m_SpawnRequestBuffers.emplace_back(sizeof(SpawnRequest) * MaxParticles);

			const auto& buffer = m_SpawnRequestBuffers[i];
			context.SetObjectDebugName(buffer.GetBufferMemory(), std::format("Particle Spawn Request Buffer Memory {}", i));
			context.SetObjectDebugName(buffer.GetBuffer(), std::format("Particle Spawn Request Buffer {}", i));
		}

		context.SetObjectDebugName(m_ParticlePoolBuffer.GetBufferMemory(), "Particle Pool Buffer Memory");
		context.SetObjectDebugName(m_ParticlePoolBuffer.GetBuffer(), "Particle Pool Buffer");
		context.SetObjectDebugName(m_DeadListBuffer.GetBufferMemory(), "Particle Dead List Buffer Memory");
		context.SetObjectDebugName(m_DeadListBuffer.GetBuffer(), "Particle Dead List Buffer");
		context.SetObjectDebugName(m_AliveListBuffer.GetBufferMemory(), "Particle Alive List Buffer Memory");
		context.SetObjectDebugName(m_AliveListBuffer.GetBuffer(), "Particle Alive List Buffer");
		context.SetObjectDebugName(m_CountersBuffer.GetBufferMemory(), "Particle Counters Buffer Memory");
		context.SetObjectDebugName(m_CountersBuffer.GetBuffer(), "Particle Counters Buffer");

		constexpr Counters counters = {
			.DeadCount = static_cast<uint32_t>(MaxParticles), 
			.AliveCount = 0, 
			.SpawnRequestCount = 0,
			.IndirectDrawCount = 0 
		};
		std::memcpy(m_CountersBuffer.GetMappedData(), &counters, sizeof(Counters));

		std::memset(m_ParticlePoolBuffer.GetMappedData(), 0, m_ParticlePoolBuffer.GetBufferSize());

		std::vector<uint32_t> deadList(MaxParticles);
		for (uint32_t i = 0; i < MaxParticles; ++i)
			deadList[i] = i;
		std::memcpy(m_DeadListBuffer.GetMappedData(), deadList.data(), sizeof(uint32_t) * MaxParticles);
	}

	ParticleSystem::~ParticleSystem() 
	{
		const auto allocator = VulkanContext::Get().GetAllocator().get();

		for (uint32_t i = 0; i < VulkanContext::Get().GetMaxFramesInFlight(); ++i)
		{
			vmaUnmapMemory(allocator, m_IndirectDrawBuffers[i].allocation);
			vmaDestroyBuffer(allocator, m_IndirectDrawBuffers[i].Handle, m_IndirectDrawBuffers[i].allocation);

			vmaUnmapMemory(allocator, m_ParticleFrameBuffers[i].allocation);
			vmaDestroyBuffer(allocator, m_ParticleFrameBuffers[i].Handle, m_ParticleFrameBuffers[i].allocation);

#if USING_MANUAL_DESCRIPTOR_ALLOCATION
			vmaUnmapMemory(allocator, m_DescriptorBuffers[i].allocation);
			vmaDestroyBuffer(allocator, m_DescriptorBuffers[i].Handle, m_DescriptorBuffers[i].allocation);
#endif
		}
	}

	void ParticleSystem::Initialize(const vk::Format colorFormat, const vk::Format depthFormat, const Owner<DescriptorAllocator>& allocator)
	{
		m_DescriptorAllocator = allocator.get();

		SetupDescriptors();
		CreateDefaultParticleTexture();
		AllocateDescriptorBuffers();
		
		SetupPipelines(colorFormat, depthFormat);
	}

	void ParticleSystem::Update(const Ref<Scene>& scene, 
								const float dt, 
								const vk::raii::CommandBuffer& cmd, 
								const uint32_t frameIndex, 
								const ParticleFrameData& frameData, 
								DescriptorAllocator& frameAllocator)
	{
		KBR_PROFILE_FUNCTION();

		BeginRenderPassDebugLabel(cmd, "Particle System Compute Update Passes");

		// Copy the frame data to the GPU buffer
		std::memcpy(m_ParticleFrameBuffers[frameIndex].MappedData, &frameData, sizeof(ParticleFrameData));

		const auto emitterView = scene->m_Registry.view<ParticleEmitterComponent, TransformComponent>();

		std::vector<SpawnRequest> activeRequests;
		activeRequests.reserve(emitterView.size_hint());

		uint32_t totalParticlesToSpawn = 0;
		
		for (const auto entity : emitterView)
		{
			auto& emitter = emitterView.get<ParticleEmitterComponent>(entity);
			const auto& transform = emitterView.get<TransformComponent>(entity);
			if (!emitter.IsActive)
				continue;

			emitter.spawnAccumulator += emitter.SpawnRate * dt;
			const uint32_t spawnCount = static_cast<uint32_t>(std::floor(emitter.spawnAccumulator));

			if (spawnCount > 0)
			{
				emitter.spawnAccumulator -= static_cast<float>(spawnCount);

				SpawnRequest req{};

				req.emitterPosition = transform.WorldTransform[3];
				req.spawnCount = spawnCount;

				req.minLife = emitter.MinLifetime;
				req.maxLife = emitter.MaxLifetime;
				req.minVelocity = emitter.MinVelocity;
				req.maxVelocity = emitter.MaxVelocity;
				req.minAcceleration = emitter.MinAcceleration;
				req.maxAcceleration = emitter.MaxAcceleration;
				req.startColor = emitter.StartColor;
				req.endColor = emitter.EndColor;
				req.startSize = emitter.StartSize;
				req.endSize = emitter.EndSize;

				activeRequests.push_back(req);

				totalParticlesToSpawn += spawnCount;
			}
		}

		const uint32_t requestCount = static_cast<uint32_t>(activeRequests.size());

		if (requestCount > 0)
		{
			const size_t dataSize = sizeof(SpawnRequest) * requestCount;

			std::memcpy(m_SpawnRequestBuffers[frameIndex].GetMappedData(), activeRequests.data(), dataSize);

			KBR_CORE_INFO("ParticleSystem: Spawning {} particles in {} requests", totalParticlesToSpawn, requestCount);
		}

		// Emit pass
		{
#if not USING_MANUAL_DESCRIPTOR_ALLOCATION

			DescriptorManager::BindSets(
				cmd,
				vk::PipelineBindPoint::eCompute,
				m_ComputePipelineLayout,
				0,
				{ m_ParticleSet, m_SpawnSets[frameIndex] }
			);

#else

			const vk::DescriptorBufferBindingInfoEXT bindingInfo{
				.address = m_DescriptorBuffers[frameIndex].DeviceAddress,
				.usage = vk::BufferUsageFlagBits::eResourceDescriptorBufferEXT
			};
			cmd.bindDescriptorBuffersEXT({ bindingInfo });

			constexpr uint32_t bufferIndices[2] = { 0, 0 }; // Both sets live in buffer index 0
			const vk::DeviceSize offsets[2] = { m_ParticleBufferOffset, m_SpawnBufferOffset };

			cmd.setDescriptorBufferOffsetsEXT(
				vk::PipelineBindPoint::eCompute,
				*m_ComputePipelineLayout,
				0, // First Set
				bufferIndices,
				offsets
			);

#endif
			BeginRenderPassDebugLabel(cmd, "Particle Emit Pass");

			if (requestCount > 0)
			{
				cmd.bindPipeline(vk::PipelineBindPoint::eCompute, m_SpawnPipeline->GetVulkanPipeline());
				cmd.pushConstants<uint32_t>(
					m_ComputePipelineLayout,
					vk::ShaderStageFlagBits::eCompute,
					0, 
					{requestCount}
				);

				constexpr uint32_t workGroupSize = 64;
				cmd.dispatch((requestCount + (workGroupSize - 1)) / workGroupSize, 1, 1);

				// Ensure Emit writes to Particle Pool and Counters finish before Prepare
				vk::MemoryBarrier2 barrier{
					.srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
					.srcAccessMask = vk::AccessFlagBits2::eShaderWrite | vk::AccessFlagBits2::eShaderRead,
					.dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
					.dstAccessMask = vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite,
				};
				cmd.pipelineBarrier2({.memoryBarrierCount = 1, .pMemoryBarriers = &barrier});
			}

			EndRenderPassDebugLabel(cmd);
		}

		// Prepare simulation pass
		{
			BeginRenderPassDebugLabel(cmd, "Particle Prepare Simulation Pass");

			cmd.bindPipeline(vk::PipelineBindPoint::eCompute, m_PrepareSimulatePipeline->GetVulkanPipeline());
			cmd.dispatch(1, 1, 1);

			// Ensure Prepare writes to Indirect Buffer and Counters finish before Simulate
			vk::MemoryBarrier2 barrier{
				.srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
				.srcAccessMask = vk::AccessFlagBits2::eShaderWrite | vk::AccessFlagBits2::eShaderRead,
				.dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
				.dstAccessMask = vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite,
			};
			cmd.pipelineBarrier2({.memoryBarrierCount = 1, .pMemoryBarriers = &barrier});

			EndRenderPassDebugLabel(cmd);
		}

		// Simulate pass
		{
			BeginRenderPassDebugLabel(cmd, "Particle Simulate Pass");

			cmd.bindPipeline(vk::PipelineBindPoint::eCompute, m_UpdatePipeline->GetVulkanPipeline());
			constexpr uint32_t workGroupSize = 256;
			cmd.dispatch((MaxParticles + (workGroupSize - 1)) / workGroupSize, 1, 1);

			// Ensure Simulate writes finish before the Graphics pipeline reads them.
			// We are waiting on writes to the AliveList, ParticlePool, and IndirectCommand buffers.
			vk::MemoryBarrier2 barrier{
				.srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
				.srcAccessMask = vk::AccessFlagBits2::eShaderWrite,
				.dstStageMask = vk::PipelineStageFlagBits2::eDrawIndirect | vk::PipelineStageFlagBits2::eVertexShader,
				.dstAccessMask = vk::AccessFlagBits2::eIndirectCommandRead | vk::AccessFlagBits2::eShaderRead,
			};
			cmd.pipelineBarrier2({ .memoryBarrierCount = 1, .pMemoryBarriers = &barrier });

			EndRenderPassDebugLabel(cmd);
		}

		EndRenderPassDebugLabel(cmd);
	}

	void ParticleSystem::RecordDraw(const vk::raii::CommandBuffer& cmd, const uint32_t frameIndex) const 
	{
		BeginRenderPassDebugLabel(cmd, "Particle Render Pass");

		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_RenderPipeline->GetVulkanPipeline());

#if not USING_MANUAL_DESCRIPTOR_ALLOCATION

		DescriptorManager::BindSets(
			cmd,
			vk::PipelineBindPoint::eGraphics,
			m_GraphicsPipelineLayout,
			0, // start at set 0
			{ m_ParticleSet, m_SpawnSets[frameIndex], m_TextureSet }
		);

#else

		const vk::DescriptorBufferBindingInfoEXT bindingInfo{
				.address = m_DescriptorBuffers[frameIndex].DeviceAddress,
				.usage = vk::BufferUsageFlagBits::eResourceDescriptorBufferEXT
		};
		cmd.bindDescriptorBuffersEXT({ bindingInfo });

		constexpr uint32_t bufferIndices[3] = { 0, 0, 0 }; // Both sets live in buffer index 0
		const vk::DeviceSize offsets[3] = { m_ParticleBufferOffset, m_SpawnBufferOffset, m_TextureBufferOffset };

		cmd.setDescriptorBufferOffsetsEXT(
			vk::PipelineBindPoint::eGraphics,
			*m_GraphicsPipelineLayout,
			0,
			bufferIndices,
			offsets
		);

#endif

		cmd.drawIndirect(
			m_IndirectDrawBuffers[frameIndex].Handle,
			0,
			1,
			sizeof(vk::DrawIndirectCommand)
		);

		EndRenderPassDebugLabel(cmd);
	}

	void ParticleSystem::SetupDescriptors() 
	{
		auto& context = VulkanContext::Get();
		const auto& device = context.GetDevice();

		// SET 0: Particle Buffers (5 Storage Buffers)
		const std::vector<vk::DescriptorSetLayoutBinding> particleBindings = {
			{.binding = 0, .descriptorType = vk::DescriptorType::eStorageBuffer, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eCompute | vk::ShaderStageFlagBits::eVertex}, // Pool
			{.binding = 1, .descriptorType = vk::DescriptorType::eStorageBuffer, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eCompute}, // DeadList
			{.binding = 2, .descriptorType = vk::DescriptorType::eStorageBuffer, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eCompute | vk::ShaderStageFlagBits::eVertex}, // AliveList
			{.binding = 3, .descriptorType = vk::DescriptorType::eStorageBuffer, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eCompute}, // Counters
			{.binding = 4, .descriptorType = vk::DescriptorType::eStorageBuffer, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eCompute}  // Indirect Buffer 
		};

		m_ParticleBuffersLayout = DescriptorManager::CreateDescriptorSetLayout(particleBindings);
		context.SetObjectDebugName(m_ParticleBuffersLayout, "Particle Buffers Descriptor Set Layout");

		// SET 1: Spawn Requests and Particle Frame Data (Storage and uniform buffer updated per frame by CPU)
		const std::vector<vk::DescriptorSetLayoutBinding> spawnReqBinding{
			{
				.binding = 0,
				.descriptorType = vk::DescriptorType::eStorageBuffer, // Spawn requests
				.descriptorCount = 1,
				.stageFlags = vk::ShaderStageFlagBits::eCompute
			},
			{
				.binding = 1,
				.descriptorType = vk::DescriptorType::eUniformBuffer, // Particle frame data
				.descriptorCount = 1,
				.stageFlags = vk::ShaderStageFlagBits::eCompute | vk::ShaderStageFlagBits::eVertex
			}
		};

		m_SpawnRequestsLayout = DescriptorManager::CreateDescriptorSetLayout(spawnReqBinding);
		context.SetObjectDebugName(m_SpawnRequestsLayout, "Particle Spawn Requests Descriptor Set Layout");

		// SET 2: Texture and Sampler
		const std::vector<vk::DescriptorSetLayoutBinding> textureBindings = {
			{ .binding = 0, . descriptorType = vk::DescriptorType::eSampledImage, .descriptorCount =  1, . stageFlags = vk::ShaderStageFlagBits::eFragment },
			{ .binding = 1, . descriptorType = vk::DescriptorType::eSampler,      .descriptorCount = 1, . stageFlags = vk::ShaderStageFlagBits::eFragment }
		};

		m_TextureLayout = DescriptorManager::CreateDescriptorSetLayout(textureBindings);
		context.SetObjectDebugName(m_TextureLayout, "Particle Texture Descriptor Set Layout");

		// Compute needs Sets 0, 1, and 2. Plus a push constant for requestCount.
		const std::array<vk::DescriptorSetLayout, 2> computeLayouts = {
			*m_ParticleBuffersLayout, *m_SpawnRequestsLayout
		};
		constexpr vk::PushConstantRange computePushConstant{
			.stageFlags = vk::ShaderStageFlagBits::eCompute,
			.offset = 0,
			.size = sizeof(uint32_t)
		};
		const vk::PipelineLayoutCreateInfo computePipelineLayoutInfo{
			.setLayoutCount = computeLayouts.size(),
			.pSetLayouts = computeLayouts.data(),
			.pushConstantRangeCount = 1,
			.pPushConstantRanges = &computePushConstant
		};
		m_ComputePipelineLayout = vk::raii::PipelineLayout(device, computePipelineLayoutInfo);
		context.SetObjectDebugName(m_ComputePipelineLayout, "Particle Compute Pipeline Layout");

		// Graphics needs Sets 0, 1, and 3.
		const std::array<vk::DescriptorSetLayout, 3> graphicsLayouts = {
			*m_ParticleBuffersLayout, *m_SpawnRequestsLayout, *m_TextureLayout
		};
		const vk::PipelineLayoutCreateInfo graphicsPipelineLayoutInfo{
			.setLayoutCount = graphicsLayouts.size(),
			.pSetLayouts = graphicsLayouts.data(),
			.pushConstantRangeCount = 0,
			.pPushConstantRanges = nullptr
		};
		m_GraphicsPipelineLayout = vk::raii::PipelineLayout(device, graphicsPipelineLayoutInfo);
		context.SetObjectDebugName(m_GraphicsPipelineLayout, "Particle Graphics Pipeline Layout");

		// Create sampler for particle texture
		constexpr vk::SamplerCreateInfo samplerInfo{
			.magFilter = vk::Filter::eLinear,
			.minFilter = vk::Filter::eLinear,
			.mipmapMode = vk::SamplerMipmapMode::eLinear,
			.addressModeU = vk::SamplerAddressMode::eRepeat,
			.addressModeV = vk::SamplerAddressMode::eRepeat,
			.addressModeW = vk::SamplerAddressMode::eRepeat,
			.mipLodBias = 0.0f,
			.anisotropyEnable = vk::True,
			.maxAnisotropy = 8.0f,
			.compareEnable = vk::False,
			.compareOp = vk::CompareOp::eAlways,
			.minLod = 0.0f,
			.maxLod = 1.0f,
			.borderColor = vk::BorderColor::eIntOpaqueBlack,
			.unnormalizedCoordinates = vk::False
		};
		m_ParticleSampler = vk::raii::Sampler(device, samplerInfo);
	}

	void ParticleSystem::SetupPipelines(vk::Format colorFormat, vk::Format depthFormat)
	{
		ComputePipelineSpecification spawnPipelineSpec{};
		spawnPipelineSpec.Name = "Particle Spawn Pipeline";
		spawnPipelineSpec.Shader = CreateRef<Shader>("particle_emit", "Particle Emit");
		spawnPipelineSpec.PipelineLayout = m_ComputePipelineLayout;
		spawnPipelineSpec.UsingDescriptorBuffers = true;

		m_SpawnPipeline = CreateRef<ComputePipeline>(spawnPipelineSpec);

		ComputePipelineSpecification prepareSimulatePipelineSpec{};
		prepareSimulatePipelineSpec.Name = "Particle Prepare Simulate Pipeline";
		prepareSimulatePipelineSpec.Shader = CreateRef<Shader>("particle_prepare_simulation", "Particle Prepare Simulation");
		prepareSimulatePipelineSpec.PipelineLayout = m_ComputePipelineLayout;
		prepareSimulatePipelineSpec.UsingDescriptorBuffers = true;

		m_PrepareSimulatePipeline = CreateRef<ComputePipeline>(prepareSimulatePipelineSpec);

		ComputePipelineSpecification updatePipelineSpec{};
		updatePipelineSpec.Name = "Particle Update Pipeline";
		updatePipelineSpec.Shader = CreateRef<Shader>("particle_simulate", "Particle Simulate");
		updatePipelineSpec.PipelineLayout = m_ComputePipelineLayout;
		updatePipelineSpec.UsingDescriptorBuffers = true;

		m_UpdatePipeline = CreateRef<ComputePipeline>(updatePipelineSpec);

		std::vector dynamicStates = {
			vk::DynamicState::eViewport,
			vk::DynamicState::eScissor
		};

		GraphicsPipelineSpecification renderPipelineSpec{};
		renderPipelineSpec.Name = "Particle Render Pipeline";
		renderPipelineSpec.Shader = CreateRef<Shader>("particle_draw", "Particle Draw");
		renderPipelineSpec.PipelineLayout = m_GraphicsPipelineLayout;
		renderPipelineSpec.BindingDescription = {};
		renderPipelineSpec.InputAttributeDescriptions = {};
		renderPipelineSpec.SampleCount = vk::SampleCountFlagBits::e1;
		renderPipelineSpec.CullMode = CullMode::None;
		renderPipelineSpec.EnableDepthClamp = false;
		renderPipelineSpec.EnableDepthBias = false;
		renderPipelineSpec.EnableDepthTest = false;
		renderPipelineSpec.EnableDepthWrite = false;
		renderPipelineSpec.FrontFace = FrontFace::CounterClockwise;
		renderPipelineSpec.BlendModes = { BlendMode::Additive };
		renderPipelineSpec.ColorAttachmentFormats = { colorFormat };
		renderPipelineSpec.DepthAttachmentFormat = depthFormat;
		renderPipelineSpec.DynamicStates = dynamicStates;
		renderPipelineSpec.UsingDescriptorBuffers = true;

		m_RenderPipeline = CreateRef<GraphicsPipeline>(renderPipelineSpec);
	}

	void ParticleSystem::AllocateParticleFrameBuffers() 
	{
		const auto& context = VulkanContext::Get();

		for (uint32_t i = 0; i < context.GetMaxFramesInFlight(); ++i)
		{
			VkBufferCreateInfo bufferInfo{};
			bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			bufferInfo.size = sizeof(ParticleFrameData);
			bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
			bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			VmaAllocationCreateInfo allocInfo{};
			allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

			VkBuffer buffer = nullptr;
			if (vmaCreateBuffer(VulkanContext::Get().GetAllocator().get(), &bufferInfo, &allocInfo, &buffer, &m_ParticleFrameBuffers[i].allocation, nullptr) != VK_SUCCESS)
				KBR_CORE_ASSERT(false, "Failed to create indirect draw buffer!");

			m_ParticleFrameBuffers[i].Handle = vk::Buffer(buffer);
			vmaMapMemory(VulkanContext::Get().GetAllocator().get(), m_ParticleFrameBuffers[i].allocation, &m_ParticleFrameBuffers[i].MappedData);

			context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkBuffer>(m_ParticleFrameBuffers[i].Handle)), vk::ObjectType::eBuffer, std::format("Particle Frame Buffer {}", i));
			context.SetObjectDebugName(m_ParticleFrameBuffers[i].allocation, std::format("Particle Frame Buffer Allocation {}", i));
		}
	}

	void ParticleSystem::AllocateDescriptorBuffers()
	{
		KBR_CORE_ASSERT(*m_ParticleBuffersLayout != nullptr && *m_SpawnRequestsLayout != nullptr, "Descriptor Set Layouts must be created before allocating descriptor buffers!");

		auto& context = VulkanContext::Get();
		const auto& device = context.GetDevice();
		const auto& physicalDevice = context.GetPhysicalDevice();

#if not USING_MANUAL_DESCRIPTOR_ALLOCATION

		m_ParticleSet = m_DescriptorAllocator->Allocate(m_ParticleBuffersLayout, "Particle Static Set");
		{
			DescriptorWriter writer(m_ParticleBuffersLayout, m_ParticleSet);
			writer.WriteStorageBuffer(0, m_ParticlePoolBuffer.GetBuffer(), m_ParticlePoolBuffer.GetBufferSize());
			writer.WriteStorageBuffer(1, m_DeadListBuffer.GetBuffer(), m_DeadListBuffer.GetBufferSize());
			writer.WriteStorageBuffer(2, m_AliveListBuffer.GetBuffer(), m_AliveListBuffer.GetBufferSize());
			writer.WriteStorageBuffer(3, m_CountersBuffer.GetBuffer(), m_CountersBuffer.GetBufferSize());
			writer.WriteStorageBuffer(4, m_IndirectDrawBuffers[0].Handle, sizeof(VkDrawIndirectCommand));
			writer.Flush();
		}

		const uint32_t framesInFlight = VulkanContext::Get().GetMaxFramesInFlight();
		m_SpawnSets.resize(framesInFlight);

		for (uint32_t i = 0; i < framesInFlight; ++i)
		{
			m_SpawnSets[i] = m_DescriptorAllocator->Allocate(m_SpawnRequestsLayout, std::format("Particle Spawn Set {}", i));

			DescriptorWriter writer(m_SpawnRequestsLayout, m_SpawnSets[i]);
			writer.WriteStorageBuffer(0, m_SpawnRequestBuffers[i].GetBuffer(), m_SpawnRequestBuffers[i].GetBufferSize());
			writer.WriteUniformBuffer(1, m_ParticleFrameBuffers[i].Handle, sizeof(ParticleFrameData));
			writer.Flush();
		}

		m_TextureSet = m_DescriptorAllocator->Allocate(m_TextureLayout, "Particle Texture Set");
		{
			DescriptorWriter writer(m_TextureLayout, m_TextureSet);

			writer.WriteSampledImage(0, m_DefaultParticleTexture->GetImageView());
			writer.WriteSampler(1, *m_ParticleSampler);
			writer.Flush();
		}

#else

		const vk::DeviceSize particleLayoutSize = m_ParticleBuffersLayout.getSizeEXT();
		const vk::DeviceSize spawnLayoutSize = m_SpawnRequestsLayout.getSizeEXT();
		const vk::DeviceSize textureLayoutSize = m_TextureLayout.getSizeEXT();

		auto alignOffset = [](const vk::DeviceSize offset, const vk::DeviceSize alignment)
		{
			return (offset + alignment - 1) & ~(alignment - 1);
		};

		const auto result = physicalDevice.getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceDescriptorBufferPropertiesEXT>();
		vk::PhysicalDeviceDescriptorBufferPropertiesEXT descBufferProps = result.get<vk::PhysicalDeviceDescriptorBufferPropertiesEXT>();

		const vk::DeviceSize alignment = descBufferProps.descriptorBufferOffsetAlignment;

		m_ParticleBufferOffset = 0;
		m_SpawnBufferOffset = alignOffset(m_ParticleBufferOffset + particleLayoutSize, alignment);
		m_TextureBufferOffset = alignOffset(m_SpawnBufferOffset + spawnLayoutSize, alignment);

		const vk::DeviceSize totalBufferSize = alignOffset(m_TextureBufferOffset + textureLayoutSize, alignment);

		for (uint32_t i = 0; i < context.GetMaxFramesInFlight(); ++i)
		{
			VkBufferCreateInfo bufferInfo{};
			bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			bufferInfo.size = totalBufferSize;
			bufferInfo.usage = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
			bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			VmaAllocationCreateInfo allocInfo{};
			allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

			VkBuffer buffer = nullptr;
			if (vmaCreateBuffer(VulkanContext::Get().GetAllocator().get(), &bufferInfo, &allocInfo, &buffer, &m_DescriptorBuffers[i].allocation, nullptr) != VK_SUCCESS)
				KBR_CORE_ASSERT(false, "Failed to create indirect draw buffer!");

			m_DescriptorBuffers[i].Handle = vk::Buffer(buffer);
			vmaMapMemory(VulkanContext::Get().GetAllocator().get(), m_DescriptorBuffers[i].allocation, &m_DescriptorBuffers[i].MappedData);

			context.SetObjectDebugName(m_DescriptorBuffers[i].Handle, std::format("Particle Descriptor Buffer {}", i));
			context.SetObjectDebugName(m_DescriptorBuffers[i].allocation, std::format("Particle Descriptor Buffer Allocation {}", i));

			vk::BufferDeviceAddressInfo addressInfo{ .buffer = m_DescriptorBuffers[i].Handle };
			m_DescriptorBuffers[i].DeviceAddress = device.getBufferAddress(addressInfo);

			char* mappedPtr = static_cast<char*>(m_DescriptorBuffers[i].MappedData);

			auto writeStorageBuffer = [&](const vk::raii::DescriptorSetLayout& layout, const uint32_t binding,
			                              const vk::DeviceSize setOffset, const vk::Buffer bufferHandle, const vk::DeviceSize bufferSize)
			{
				const vk::DeviceSize bindingOffset = layout.getBindingOffsetEXT(binding);

				const vk::DescriptorAddressInfoEXT addrInfo{
					.address = device.getBufferAddress({.buffer = bufferHandle }),
					.range = bufferSize,
					.format = vk::Format::eUndefined
				};
				const vk::DescriptorGetInfoEXT getInfo{
					.type = vk::DescriptorType::eStorageBuffer,
					.data = vk::DescriptorDataEXT(&addrInfo)
				};
				device.getDescriptorEXT(getInfo, descBufferProps.storageBufferDescriptorSize, mappedPtr + setOffset + bindingOffset);
			};

			writeStorageBuffer(m_ParticleBuffersLayout, 0, m_ParticleBufferOffset, m_ParticlePoolBuffer.GetBuffer(), m_ParticlePoolBuffer.GetBufferSize());
			writeStorageBuffer(m_ParticleBuffersLayout, 1, m_ParticleBufferOffset, m_DeadListBuffer.GetBuffer(), m_DeadListBuffer.GetBufferSize());
			writeStorageBuffer(m_ParticleBuffersLayout, 2, m_ParticleBufferOffset, m_AliveListBuffer.GetBuffer(), m_AliveListBuffer.GetBufferSize());
			writeStorageBuffer(m_ParticleBuffersLayout, 3, m_ParticleBufferOffset, m_CountersBuffer.GetBuffer(), m_CountersBuffer.GetBufferSize());
			writeStorageBuffer(m_ParticleBuffersLayout, 4, m_ParticleBufferOffset, m_IndirectDrawBuffers[i].Handle, sizeof(VkDrawIndirectCommand));

			writeStorageBuffer(m_SpawnRequestsLayout, 0, m_SpawnBufferOffset, m_SpawnRequestBuffers[i].GetBuffer(), m_SpawnRequestBuffers[i].GetBufferSize());

			const vk::DeviceSize frameBindingOffset = m_SpawnRequestsLayout.getBindingOffsetEXT(1);

			const vk::DescriptorAddressInfoEXT frameAddrInfo{
				.address = device.getBufferAddress({.buffer = m_ParticleFrameBuffers[i].Handle }),
				.range = sizeof(ParticleFrameData),
				.format = vk::Format::eUndefined
			};
			const vk::DescriptorGetInfoEXT frameGetInfo{
				.type = vk::DescriptorType::eUniformBuffer,
				.data = vk::DescriptorDataEXT(&frameAddrInfo)
			};
			device.getDescriptorEXT(frameGetInfo, descBufferProps.uniformBufferDescriptorSize, mappedPtr + m_SpawnBufferOffset + frameBindingOffset);

			const vk::DeviceSize textureBindingOffset = m_TextureLayout.getBindingOffsetEXT(0);

			const vk::ImageView particleView = m_DefaultParticleTexture->GetImageView();

			vk::DescriptorImageInfo imageInfo{
				.sampler = nullptr,
				.imageView = particleView,
				.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
			};
			vk::DescriptorGetInfoEXT imageGetInfo{
				.type = vk::DescriptorType::eSampledImage,
				.data = vk::DescriptorDataEXT(&imageInfo)
			};
			device.getDescriptorEXT(imageGetInfo, descBufferProps.sampledImageDescriptorSize, mappedPtr + m_TextureBufferOffset + textureBindingOffset);

			const vk::DeviceSize samplerBindingOffset = m_TextureLayout.getBindingOffsetEXT(1);

			vk::DescriptorImageInfo samplerInfo{
				.sampler = *m_ParticleSampler,
				.imageView = nullptr,
				.imageLayout = vk::ImageLayout::eUndefined
			};
			vk::DescriptorGetInfoEXT samplerGetInfo{
				.type = vk::DescriptorType::eSampler,
				.data = vk::DescriptorDataEXT(&samplerInfo)
			};
			device.getDescriptorEXT(samplerGetInfo, descBufferProps.samplerDescriptorSize, mappedPtr + m_TextureBufferOffset + samplerBindingOffset);
		}

#endif
	}

	void ParticleSystem::CreateDefaultParticleTexture() 
	{
		constexpr std::array<uint8_t, 4> buffer = { 255, 255, 255, 255 };
		TextureSpecification spec{};
		spec.Width = 1;
		spec.Height = 1;
		spec.Format = ImageFormat::RGBA8;
		const Buffer bufferStruct{ sizeof(uint8_t) * buffer.size() };
		std::memcpy(bufferStruct.Data, buffer.data(), bufferStruct.Size);
		m_DefaultParticleTexture = Texture2D::FromBuffer(spec, bufferStruct);
	}

	void ParticleSystem::AllocateIndirectDrawBuffers() 
	{
		const auto& context = VulkanContext::Get();

		for (uint32_t i = 0; i < context.GetMaxFramesInFlight(); ++i)
		{
			VkBufferCreateInfo bufferInfo{};
			bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			bufferInfo.size = sizeof(VkDrawIndirectCommand);
			bufferInfo.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT 
				| VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
			bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			VmaAllocationCreateInfo allocInfo{};
			allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

			VkBuffer buffer = nullptr;
			if (vmaCreateBuffer(VulkanContext::Get().GetAllocator().get(), &bufferInfo, &allocInfo, &buffer, &m_IndirectDrawBuffers[i].allocation, nullptr) != VK_SUCCESS)
				KBR_CORE_ASSERT(false, "Failed to create indirect draw buffer!");

			m_IndirectDrawBuffers[i].Handle = vk::Buffer(buffer);
			vmaMapMemory(VulkanContext::Get().GetAllocator().get(), m_IndirectDrawBuffers[i].allocation, &m_IndirectDrawBuffers[i].MappedData);

			context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkBuffer>(m_IndirectDrawBuffers[i].Handle)), vk::ObjectType::eBuffer, std::format("Particle Indirect Draw Buffer {}", i));
			context.SetObjectDebugName(m_IndirectDrawBuffers[i].allocation, std::format("Particle Indirect Draw Buffer Allocation {}", i));

			constexpr VkDrawIndirectCommand drawCommand{
				.vertexCount = 6,
				.instanceCount = 0,
				.firstVertex = 0,
				.firstInstance = 0
			};
			std::memcpy(m_IndirectDrawBuffers[i].MappedData, &drawCommand, sizeof(drawCommand));
		}
	}
}
