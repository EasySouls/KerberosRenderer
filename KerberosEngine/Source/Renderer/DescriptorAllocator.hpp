#pragma once

#include "Vulkan.hpp"
#include "ShaderResourceSet.hpp"

#include <vma/vk_mem_alloc.h>

namespace Kerberos
{
	class DescriptorAllocator
	{
	public:
		explicit DescriptorAllocator(uint32_t maxSets);
		~DescriptorAllocator();

		ShaderResourceSet Allocate(const vk::raii::DescriptorSetLayout& layout);

	private:
		bool m_UseDescriptorBuffers = false;

		vk::raii::DescriptorPool m_DescriptorPool = nullptr;

		vk::Buffer m_Handle = nullptr;
		VmaAllocation m_Allocation = nullptr;
		vk::DeviceAddress m_DeviceAddress = 0;
		void* m_MappedData = nullptr;
		vk::DeviceSize m_OffsetAlignment = 0;
		uint32_t m_CurrentOffset = 0;
	};
}