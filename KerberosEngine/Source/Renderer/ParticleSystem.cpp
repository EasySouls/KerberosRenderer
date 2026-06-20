#include "kbrpch.hpp"
#include "ParticleSystem.hpp"

#include "Scene/Scene.hpp"
#include "Scene/Components/ParticleComponents.hpp"
#include "VulkanContext.hpp"

#include <glm/gtc/random.hpp>

#include <array>

namespace
{
	struct GPUParticle
	{
		glm::vec4 PositionAndSize;	// xyz = position, w = size
		glm::vec4 VelocityAndAge;	// xyz = velocity, w = age
		glm::vec4 Color;			// rgba
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

	ParticleSystem::ParticleSystem()
		: m_ParticlePoolBuffer(sizeof(GPUParticle) * MaxParticles)
		, m_DeadListBuffer(sizeof(uint32_t) * MaxParticles)
		, m_AliveListBuffer(sizeof(uint32_t) * MaxParticles)
		, m_CountersBuffer(sizeof(Counters))
		, m_IndirectDrawBuffers(VulkanContext::Get().GetMaxFramesInFlight())
	{
		for (uint32_t i = 0; i < VulkanContext::Get().GetMaxFramesInFlight(); ++i)
		{
			VkBufferCreateInfo bufferInfo{};
			bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			bufferInfo.size = sizeof(VkDrawIndirectCommand);
			bufferInfo.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
			bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			VmaAllocationCreateInfo allocInfo{};
			allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

			VkBuffer buffer = nullptr;
			if (vmaCreateBuffer(VulkanContext::Get().GetAllocator().get(), &bufferInfo, &allocInfo, &buffer, &m_IndirectDrawBuffers[i].allocation, nullptr) != VK_SUCCESS)
				KBR_CORE_ASSERT(false, "Failed to create indirect draw buffer!");

			m_IndirectDrawBuffers[i].Handle = vk::Buffer(buffer);
			vmaMapMemory(VulkanContext::Get().GetAllocator().get(), m_IndirectDrawBuffers[i].allocation, &m_IndirectDrawBuffers[i].MappedData);
		}

		for (uint32_t i = 0; i < VulkanContext::Get().GetMaxFramesInFlight(); ++i)
		{
			m_SpawnRequestBuffers.emplace_back(sizeof(SpawnRequest) * MaxParticles);
		}

		constexpr Counters counters = {
			.DeadCount = static_cast<uint32_t>(MaxParticles), 
			.AliveCount = 0, 
			.SpawnRequestCount = 0,
			.IndirectDrawCount = 0 
		};
		std::memcpy(m_CountersBuffer.GetMappedData(), &counters, sizeof(Counters));

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
		}
	}

	void ParticleSystem::Initialize(const vk::Format colorFormat, const vk::Format depthFormat, const vk::raii::DescriptorSetLayout& sceneDescriptorLayout)
	{
		SetupDescriptors(sceneDescriptorLayout);
		SetupPipelineLayouts(sceneDescriptorLayout);
		SetupPipelines(colorFormat, depthFormat);
	}

	void ParticleSystem::Update(const Ref<Scene>& scene, const float dt, const vk::raii::CommandBuffer& cmd, const uint32_t frameIndex, const vk::raii::DescriptorSet& sceneDescriptorSet)
	{
		KBR_PROFILE_FUNCTION();

		const auto emitterView = scene->m_Registry.view<ParticleEmitterComponent, TransformComponent>();

		std::vector<SpawnRequest> activeRequests;
		activeRequests.reserve(emitterView.size_hint());
		
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

				req.emitterPosition = transform.Translation;
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
			}
		}

		const uint32_t requestCount = static_cast<uint32_t>(activeRequests.size());

		if (requestCount > 0)
		{
			const size_t dataSize = sizeof(SpawnRequest) * requestCount;

			std::memcpy(m_SpawnRequestBuffers[frameIndex].GetMappedData(), activeRequests.data(), dataSize);
		}

		// Emit pass
		{
			cmd.bindDescriptorSets(
				vk::PipelineBindPoint::eCompute,
				m_ComputePipelineLayout,
				0,  { sceneDescriptorSet, m_ParticleBufferDescriptorSets[frameIndex], m_SpawnRequestDescriptorSets[frameIndex] },
				{ 0 }
			);

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
		}

		// Prepare simulation pass
		{
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
		}

		// Simulate pass
		{
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
		}
	}

	void ParticleSystem::RecordDraw(const vk::raii::CommandBuffer& cmd, const uint32_t frameIndex, const vk::raii::DescriptorSet& sceneDescriptorSet)
	{
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_RenderPipeline->GetVulkanPipeline());

		cmd.bindDescriptorSets(
			vk::PipelineBindPoint::eGraphics,
			m_GraphicsPipelineLayout,
			0, { sceneDescriptorSet, m_ParticleBufferDescriptorSets[frameIndex], m_TextureDescriptorSet },
			{ 0 }
		);

		cmd.drawIndirect(
			m_IndirectDrawBuffers[frameIndex].Handle,
			0,
			1,
			sizeof(vk::DrawIndirectCommand)
		);
	}

	void ParticleSystem::SetupDescriptors(const vk::raii::DescriptorSetLayout& sceneLayout) 
	{
		auto& context = VulkanContext::Get();
		const auto& device = context.GetDevice();

		// SET 1: Particle Buffers (5 Storage Buffers)
		const std::vector<vk::DescriptorSetLayoutBinding> particleBindings = {
			{0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute | vk::ShaderStageFlagBits::eVertex}, // Pool
			{1, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute}, // DeadList
			{2, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute | vk::ShaderStageFlagBits::eVertex}, // AliveList
			{3, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute}, // Counters
			{4, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute}  // Indirect Buffer 
		};
		const vk::DescriptorSetLayoutCreateInfo particleLayoutInfo{
			.bindingCount = static_cast<uint32_t>(particleBindings.size()),
			.pBindings = particleBindings.data()
		};
		m_ParticleBuffersLayout = vk::raii::DescriptorSetLayout(device, particleLayoutInfo);
		context.SetObjectDebugName(m_ParticleBuffersLayout, "Particle Buffers Descriptor Set Layout");

		// SET 2: Spawn Requests (Storage Buffer updated per frame by CPU)
		constexpr vk::DescriptorSetLayoutBinding spawnReqBinding{
			.binding = 0,
			.descriptorType = vk::DescriptorType::eStorageBuffer,
			.descriptorCount = 1,
			.stageFlags = vk::ShaderStageFlagBits::eCompute
		};
		const vk::DescriptorSetLayoutCreateInfo spawnReqLayoutInfo{
			.bindingCount = 1,
			.pBindings = &spawnReqBinding
		};
		m_SpawnRequestsLayout = vk::raii::DescriptorSetLayout(device, spawnReqLayoutInfo);
		context.SetObjectDebugName(m_SpawnRequestsLayout, "Particle Spawn Requests Descriptor Set Layout");

		// SET 3: Texture and Sampler
		const std::vector<vk::DescriptorSetLayoutBinding> textureBindings = {
			{0, vk::DescriptorType::eSampledImage, 1, vk::ShaderStageFlagBits::eFragment},
			{1, vk::DescriptorType::eSampler,      1, vk::ShaderStageFlagBits::eFragment}
		};
		const vk::DescriptorSetLayoutCreateInfo texLayoutInfo{
			.bindingCount = static_cast<uint32_t>(textureBindings.size()),
			.pBindings = textureBindings.data()
		};
		m_TextureLayout = vk::raii::DescriptorSetLayout(device, texLayoutInfo);
		context.SetObjectDebugName(m_TextureLayout, "Particle Texture Descriptor Set Layout");

		// Compute needs Sets 0, 1, and 2. Plus a push constant for requestCount.
		const std::array<vk::DescriptorSetLayout, 3> computeLayouts = {
			*sceneLayout, *m_ParticleBuffersLayout, *m_SpawnRequestsLayout
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
			*sceneLayout,* m_ParticleBuffersLayout,* m_TextureLayout
		};
		const vk::PipelineLayoutCreateInfo graphicsPipelineLayoutInfo{
			.setLayoutCount = graphicsLayouts.size(),
			.pSetLayouts = graphicsLayouts.data(),
			.pushConstantRangeCount = 0,
			.pPushConstantRanges = nullptr
		};
		m_GraphicsPipelineLayout = vk::raii::PipelineLayout(device, graphicsPipelineLayoutInfo);
		context.SetObjectDebugName(m_GraphicsPipelineLayout, "Particle Graphics Pipeline Layout");

		const uint32_t maxFramesInFlight = VulkanContext::Get().GetMaxFramesInFlight();

		const std::vector<vk::DescriptorPoolSize> poolSizes = {
			{vk::DescriptorType::eUniformBuffer, maxFramesInFlight},
			{vk::DescriptorType::eStorageBuffer, maxFramesInFlight * 7}, // 5 for particles, 1 for spawn per frame
			{vk::DescriptorType::eSampledImage, 1},
			{vk::DescriptorType::eSampler, 1}
		};

		const vk::DescriptorPoolCreateInfo descriptorPoolInfo{
			.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
			.maxSets = (maxFramesInFlight * 3) + 1, // Sets 0, 1, 2 per frame + 1 Set 3 global
			.poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
			.pPoolSizes = poolSizes.data()
		};
		m_DescriptorPool = vk::raii::DescriptorPool(device, descriptorPoolInfo);
		context.SetObjectDebugName(m_DescriptorPool, "Particle Descriptor Pool");

		// Setup arrays of layouts for allocation
		std::vector<vk::DescriptorSetLayout> particleLayouts(maxFramesInFlight, *m_ParticleBuffersLayout);
		std::vector<vk::DescriptorSetLayout> spawnLayouts(maxFramesInFlight, *m_SpawnRequestsLayout);

		// Allocate Sets
		vk::DescriptorSetAllocateInfo allocInfo;
		allocInfo.descriptorPool = *m_DescriptorPool;

		allocInfo.descriptorSetCount = maxFramesInFlight;

		allocInfo.pSetLayouts = particleLayouts.data();
		m_ParticleBufferDescriptorSets = vk::raii::DescriptorSets(device, allocInfo);
		for (uint32_t i = 0; i < m_ParticleBufferDescriptorSets.size(); ++i)
		{
			context.SetObjectDebugName(m_ParticleBufferDescriptorSets[i], "Particle Buffer Descriptor Set[" + std::to_string(i) + "]");
		}

		allocInfo.pSetLayouts = spawnLayouts.data();
		m_SpawnRequestDescriptorSets = vk::raii::DescriptorSets(device, allocInfo);
		for (uint32_t i = 0; i < m_SpawnRequestDescriptorSets.size(); ++i)
		{
			context.SetObjectDebugName(m_SpawnRequestDescriptorSets[i], "Particle Spawn Request Descriptor Set[" + std::to_string(i) + "]");
		}

		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &(*m_TextureLayout);
		m_TextureDescriptorSet = std::move(vk::raii::DescriptorSets(device, allocInfo)[0]);
		context.SetObjectDebugName(m_TextureDescriptorSet, "Particle Texture Descriptor Set");

		// Create sampler for particle texture
		vk::SamplerCreateInfo samplerInfo{
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


		std::vector<vk::WriteDescriptorSet> descriptorWrites;

		for (uint32_t i = 0; i < maxFramesInFlight; i++)
		{

			// Spawn Requests
			vk::DescriptorBufferInfo spawnReqInfo{ *m_SpawnRequestBuffers[i].GetBuffer(), 0, VK_WHOLE_SIZE};
			descriptorWrites.push_back({
				.dstSet = *m_SpawnRequestDescriptorSets[i], .dstBinding = 0, .dstArrayElement = 0, .descriptorCount = 1, .descriptorType = vk::DescriptorType::eStorageBuffer, .pImageInfo = nullptr, .pBufferInfo = &spawnReqInfo, .pTexelBufferView = nullptr
									   });

			// 3. Particle Buffers (Shared buffers, but we bind them to the per-frame descriptor sets)
			// Assuming global buffers: particlePoolBuffer, deadListBuffer, etc.
			vk::DescriptorBufferInfo poolInfo{ *m_ParticlePoolBuffer.GetBuffer(), 0, VK_WHOLE_SIZE };
			descriptorWrites.push_back({ .dstSet = *m_ParticleBufferDescriptorSets[i], .dstBinding = 0, .dstArrayElement = 0, .descriptorCount = 1, .descriptorType = vk::DescriptorType::eStorageBuffer, .pImageInfo = nullptr, .pBufferInfo = &poolInfo, .pTexelBufferView = nullptr });

			vk::DescriptorBufferInfo deadInfo{ *m_DeadListBuffer.GetBuffer(), 0, VK_WHOLE_SIZE };
			descriptorWrites.push_back({ .dstSet = *m_ParticleBufferDescriptorSets[i], .dstBinding = 1, .dstArrayElement = 0, .descriptorCount = 1, .descriptorType = vk::DescriptorType::eStorageBuffer, .pImageInfo = nullptr, .pBufferInfo = &deadInfo, .pTexelBufferView = nullptr });

			vk::DescriptorBufferInfo aliveInfo{ *m_AliveListBuffer.GetBuffer(), 0, VK_WHOLE_SIZE };
			descriptorWrites.push_back({ .dstSet = *m_ParticleBufferDescriptorSets[i], .dstBinding = 2, .dstArrayElement = 0, .descriptorCount = 1, .descriptorType = vk::DescriptorType::eStorageBuffer, .pImageInfo = nullptr, .pBufferInfo = &aliveInfo, .pTexelBufferView = nullptr });

			vk::DescriptorBufferInfo counterInfo{ *m_CountersBuffer.GetBuffer(), 0, VK_WHOLE_SIZE };
			descriptorWrites.push_back({ .dstSet = *m_ParticleBufferDescriptorSets[i], .dstBinding = 3, .dstArrayElement = 0, .descriptorCount = 1, .descriptorType = vk::DescriptorType::eStorageBuffer, .pImageInfo = nullptr, .pBufferInfo = &counterInfo, .pTexelBufferView = nullptr });

			vk::DescriptorBufferInfo indirectInfo{ m_IndirectDrawBuffers[i].Handle, 0, VK_WHOLE_SIZE};
			descriptorWrites.push_back({ .dstSet = *m_ParticleBufferDescriptorSets[i], .dstBinding = 4, .dstArrayElement = 0, .descriptorCount = 1, .descriptorType = vk::DescriptorType::eStorageBuffer, .pImageInfo = nullptr, .pBufferInfo = &indirectInfo, .pTexelBufferView = nullptr });
		}

		/*vk::DescriptorImageInfo imageInfo{
			nullptr,
			*particleImageView,
			vk::ImageLayout::eShaderReadOnlyOptimal
		};
		descriptorWrites.push_back({
			*textureSet, 0, 0, 1, vk::DescriptorType::eSampledImage, &imageInfo, nullptr, nullptr
								   });*/

		vk::DescriptorImageInfo samplerDescriptorInfo{
			.sampler = *m_ParticleSampler,
			.imageView = nullptr,
			.imageLayout = vk::ImageLayout::eUndefined
		};

		descriptorWrites.push_back(vk::WriteDescriptorSet{
					.dstSet = m_TextureDescriptorSet,
					.dstBinding = 1,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = vk::DescriptorType::eSampler,
					.pImageInfo = &samplerDescriptorInfo
		});

		device.updateDescriptorSets(descriptorWrites, nullptr);
	}

	void ParticleSystem::SetupPipelineLayouts(const vk::raii::DescriptorSetLayout& sceneLayout)
	{
		auto& context = VulkanContext::Get();
		const auto& device = context.GetDevice();

		// Compute pipeline layout
		{
			const std::array setLayouts = {
				*sceneLayout, *m_ParticleBuffersLayout, *m_SpawnRequestsLayout
			};
			constexpr vk::PushConstantRange pushConstantRange{
				.stageFlags = vk::ShaderStageFlagBits::eCompute,
				.offset = 0,
				.size = sizeof(uint32_t)
			};

			const vk::PipelineLayoutCreateInfo computePipelineLayoutInfo{
				.setLayoutCount = static_cast<uint32_t>(setLayouts.size()),
				.pSetLayouts = setLayouts.data(),
				.pushConstantRangeCount = 1,
				.pPushConstantRanges = &pushConstantRange
			};
			m_ComputePipelineLayout = vk::raii::PipelineLayout{ device, computePipelineLayoutInfo };
			context.SetObjectDebugName(m_ComputePipelineLayout, "Particle Compute Pipeline Layout");
		}
		
		// Graphics pipeline layout
		{
			const std::array setLayouts = {
				*sceneLayout, *m_ParticleBuffersLayout, *m_TextureLayout
			};
			const vk::PipelineLayoutCreateInfo graphicsPipelineLayoutInfo{
				.setLayoutCount = static_cast<uint32_t>(setLayouts.size()),
				.pSetLayouts = setLayouts.data()
			};
			m_GraphicsPipelineLayout = vk::raii::PipelineLayout{ device, graphicsPipelineLayoutInfo };
			context.SetObjectDebugName(m_GraphicsPipelineLayout, "Particle Graphics Pipeline Layout");
		}
	}

	void ParticleSystem::SetupPipelines(vk::Format colorFormat, vk::Format depthFormat)
	{
		ComputePipelineSpecification spawnPipelineSpec{};
		spawnPipelineSpec.Name = "Particle Spawn Pipeline";
		spawnPipelineSpec.Shader = CreateRef<Shader>("particle_emit", "Particle Emit");
		spawnPipelineSpec.PipelineLayout = m_ComputePipelineLayout;

		m_SpawnPipeline = CreateRef<ComputePipeline>(spawnPipelineSpec);

		ComputePipelineSpecification prepareSimulatePipelineSpec{};
		prepareSimulatePipelineSpec.Name = "Particle Prepare Simulate Pipeline";
		prepareSimulatePipelineSpec.Shader = CreateRef<Shader>("particle_prepare_simulation", "Particle Prepare Simulation");
		prepareSimulatePipelineSpec.PipelineLayout = m_ComputePipelineLayout;

		m_PrepareSimulatePipeline = CreateRef<ComputePipeline>(prepareSimulatePipelineSpec);

		ComputePipelineSpecification updatePipelineSpec{};
		updatePipelineSpec.Name = "Particle Update Pipeline";
		updatePipelineSpec.Shader = CreateRef<Shader>("particle_simulate", "Particle Simulate");
		updatePipelineSpec.PipelineLayout = m_ComputePipelineLayout;

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
		renderPipelineSpec.EnableDepthTest = true;
		renderPipelineSpec.EnableDepthWrite = false;
		renderPipelineSpec.BlendModes = { BlendMode::Additive };
		renderPipelineSpec.ColorAttachmentFormats = { colorFormat };
		renderPipelineSpec.DepthAttachmentFormat = depthFormat;
		renderPipelineSpec.DynamicStates = dynamicStates;

		m_RenderPipeline = CreateRef<GraphicsPipeline>(renderPipelineSpec);
	}
}
