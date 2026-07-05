#pragma once

#include "Vulkan.hpp"

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
#include <vma/vk_mem_alloc.h>

namespace Kerberos
{
	struct GrassChunk
	{
		glm::vec4 centerAndRadius; // xyz = world space center, w = bounding radius (for culling)
		glm::vec4 parameters;      // x = density, y = wind phase offset, z = type, w = unused
	};

	struct GrassConstants
	{
		glm::mat4 viewProjMatrix;
		float time;
	};

	class GrassSystem
	{
	public:
		~GrassSystem();

		void Init();
		
		void RecordDraw(const vk::raii::CommandBuffer& cmd, uint32_t frameIndex, const GrassConstants& constants);

	private:
		void InitGrassDataBuffer();
		void InitDescriptorLayouts();
		void InitShaderObjects();
		void InitDescriptorBuffer();

		static void SetDefaultGraphicsState(const vk::raii::CommandBuffer& cmd);

		void Cleanup() const;

	private:
		vk::raii::DescriptorSetLayout m_SetLayout = nullptr;
		vk::raii::PipelineLayout m_PipelineLayout = nullptr;

		vk::raii::ShaderEXT m_TaskShader = nullptr;
		vk::raii::ShaderEXT m_MeshShader = nullptr;
		vk::raii::ShaderEXT m_FragShader = nullptr;

		vk::raii::Buffer m_DescriptorBuffer = nullptr;
		vk::raii::DeviceMemory m_DescriptorMemory = nullptr;
		vk::DeviceAddress m_DescriptorBufferAddress = 0;

		vk::PushConstantRange m_PushConstantRange{};

		struct VulkanBuffer
		{
			vk::Buffer Handle;
			VmaAllocation allocation;
			void* MappedData = nullptr;
			vk::DeviceAddress DeviceAddress = 0;
		};
		static constexpr size_t MaxGrassCount = 100000;
		VulkanBuffer m_ChunkBuffer{};
	};
}