#pragma once

#include "Core/Core.hpp"
#include "Buffer.hpp"
#include "Vulkan.hpp"
#include "ComputePipeline.hpp"
#include "GraphicsPipeline.hpp"
#include "Textures/Texture2D.hpp"

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <vma/vk_mem_alloc.h>

#include <vector>

namespace Kerberos
{

class Scene;

struct alignas(16) ParticleFrameData
{
	glm::mat4 ViewProj{ 0.0f };
	glm::vec3 CameraUp{ 0.0f, 1.0f, 0.0f };
	float     DeltaTime{ 0.0f };
	glm::vec3 CameraRight{ 1.0f, 0.0f, 0.0f };
	float     Time{ 0.0f };
};

class ParticleSystem
{
public:
	ParticleSystem();
	~ParticleSystem();

	void Initialize(vk::Format colorFormat, vk::Format depthFormat);

	void Update(const Ref<Scene>& scene, float dt, const vk::raii::CommandBuffer& cmd, uint32_t frameIndex, const ParticleFrameData& frameData) const;

	void RecordDraw(const vk::raii::CommandBuffer& cmd, uint32_t frameIndex) const;

private:
	void SetupDescriptors();
	void SetupPipelines(vk::Format colorFormat, vk::Format depthFormat);

	void AllocateParticleFrameBuffers();
	void AllocateIndirectDrawBuffers();
	void AllocateDescriptorBuffers();

	void CreateDefaultParticleTexture();

private:
	static constexpr size_t MaxParticles = 1'000'000;

	StorageBuffer m_ParticlePoolBuffer;
	StorageBuffer m_DeadListBuffer;
	StorageBuffer m_AliveListBuffer;
	StorageBuffer m_CountersBuffer;

	vk::DeviceSize m_ParticleBufferOffset = 0;
	vk::DeviceSize m_SpawnBufferOffset = 0;
	vk::DeviceSize m_TextureBufferOffset = 0;

	struct VulkanBuffer
	{
		vk::Buffer Handle;
		VmaAllocation allocation;
		void* MappedData = nullptr;
		vk::DeviceAddress DeviceAddress = 0;
	};
	std::vector<VulkanBuffer> m_ParticleFrameBuffers;
	std::vector<VulkanBuffer> m_DescriptorBuffers;
	std::vector<VulkanBuffer> m_IndirectDrawBuffers;
	std::vector<StorageBuffer> m_SpawnRequestBuffers;

	vk::raii::DescriptorSetLayout m_ParticleBuffersLayout = nullptr;
	vk::raii::DescriptorSetLayout m_SpawnRequestsLayout = nullptr;
	vk::raii::DescriptorSetLayout m_TextureLayout = nullptr;

	vk::raii::PipelineLayout m_ComputePipelineLayout = nullptr;
	vk::raii::PipelineLayout m_GraphicsPipelineLayout = nullptr;

	vk::raii::Sampler m_ParticleSampler = nullptr;

	Ref<Texture2D> m_DefaultParticleTexture = nullptr;

	Ref<ComputePipeline>	m_SpawnPipeline;
	Ref<ComputePipeline>	m_PrepareSimulatePipeline;
	Ref<ComputePipeline>	m_UpdatePipeline;
	Ref<GraphicsPipeline>	m_RenderPipeline;
};

}