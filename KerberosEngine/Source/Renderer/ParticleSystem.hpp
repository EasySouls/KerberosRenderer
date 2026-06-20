#pragma once

#include "Core/Core.hpp"
#include "Buffer.hpp"
#include "Vulkan.hpp"
#include "ComputePipeline.hpp"
#include "GraphicsPipeline.hpp"

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <vma/vk_mem_alloc.h>

#include <vector>

namespace Kerberos
{

class Scene;

class ParticleSystem
{
public:
	ParticleSystem();
	~ParticleSystem();

	void Initialize(vk::Format colorFormat, vk::Format depthFormat, const vk::raii::DescriptorSetLayout& sceneDescriptorLayout);

	void Update(const Ref<Scene>& scene, float dt, const vk::raii::CommandBuffer& cmd, uint32_t frameIndex, const vk::raii::DescriptorSet& sceneDescriptorSet);

	void RecordDraw(const vk::raii::CommandBuffer& cmd, uint32_t frameIndex, const vk::raii::DescriptorSet& sceneDescriptorSet);

private:
	void SetupDescriptors(const vk::raii::DescriptorSetLayout& sceneLayout);
	void SetupPipelineLayouts(const vk::raii::DescriptorSetLayout& sceneLayout);
	void SetupPipelines(vk::Format colorFormat, vk::Format depthFormat);

private:
	static constexpr size_t MaxParticles = 1'000'000;

	StorageBuffer m_ParticlePoolBuffer;
	StorageBuffer m_DeadListBuffer;
	StorageBuffer m_AliveListBuffer;
	StorageBuffer m_CountersBuffer;

	struct IndirectDrawBuffer
	{
		vk::Buffer Handle;
		VmaAllocation allocation;
		void* MappedData = nullptr;
	};
	std::vector<IndirectDrawBuffer> m_IndirectDrawBuffers;
	std::vector<StorageBuffer> m_SpawnRequestBuffers;

	vk::raii::DescriptorPool m_DescriptorPool = nullptr;

	vk::raii::DescriptorSetLayout m_ParticleBuffersLayout = nullptr;
	vk::raii::DescriptorSetLayout m_SpawnRequestsLayout = nullptr;
	vk::raii::DescriptorSetLayout m_TextureLayout = nullptr;

	std::vector<vk::raii::DescriptorSet> m_ParticleBufferDescriptorSets;
	std::vector<vk::raii::DescriptorSet> m_SpawnRequestDescriptorSets;
	vk::raii::DescriptorSet m_TextureDescriptorSet = nullptr;

	vk::raii::PipelineLayout m_ComputePipelineLayout = nullptr;
	vk::raii::PipelineLayout m_GraphicsPipelineLayout = nullptr;

	vk::raii::Sampler m_ParticleSampler = nullptr;

	Ref<ComputePipeline>	m_SpawnPipeline;
	Ref<ComputePipeline>	m_PrepareSimulatePipeline;
	Ref<ComputePipeline>	m_UpdatePipeline;
	Ref<GraphicsPipeline>	m_RenderPipeline;
};

}