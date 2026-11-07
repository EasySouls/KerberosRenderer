#pragma once

#include "Vulkan.hpp"
#include "DescriptorAllocator.hpp"
#include "Core/Core.hpp"

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

		// Grass rendering needs mesh shaders, shader objects and descriptor buffers. When the device does not
		// provide them, Init() logs and leaves the system uninitialised instead of creating invalid Vulkan
		// objects, and RecordDraw() becomes a no-op.
		static bool IsSupportedByDevice();
		bool IsSupported() const { return m_Supported; }

		void Init(const Owner<DescriptorAllocator>& allocator);
		
		void RecordDraw(const vk::raii::CommandBuffer& cmd, uint32_t frameIndex, const GrassConstants& constants);

	private:
		void InitGrassDataBuffer();
		void InitDescriptorLayouts();
		void InitShaderObjects();
		void InitDescriptorBuffer();

		static void SetDefaultGraphicsState(const vk::raii::CommandBuffer& cmd);

		void Cleanup() const;

	private:
		DescriptorAllocator* m_DescriptorAllocator = nullptr;

		bool m_Supported = false;

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