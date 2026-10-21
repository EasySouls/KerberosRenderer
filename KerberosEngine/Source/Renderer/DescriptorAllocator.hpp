#pragma once

#include "Vulkan.hpp"
#include "ShaderResourceSet.hpp"

#include <vma/vk_mem_alloc.h>


#include <string>
#ifdef KBR_DEBUG
#include <map>
#endif

namespace Kerberos
{
	class DescriptorAllocator
	{
	public:
		explicit DescriptorAllocator(uint32_t maxSets, vk::DeviceSize bufferHeapSize = 1024 * 1024);
		~DescriptorAllocator();

		ShaderResourceSet Allocate(const vk::raii::DescriptorSetLayout& layout, const std::string& debugName = "");

		void Reset();

	private:
		bool m_UseDescriptorBuffers = false;

		vk::raii::DescriptorPool m_DescriptorPool = nullptr;

		vk::Buffer m_Handle = nullptr;
		VmaAllocation m_Allocation = nullptr;
		vk::DeviceAddress m_DeviceAddress = 0;
		void* m_MappedData = nullptr;
		vk::DeviceSize m_OffsetAlignment = 0;
		uint64_t m_CurrentOffset = 0;

#ifdef KBR_DEBUG
		std::map<vk::DeviceSize, std::string> m_AllocationDebugNames;
#endif
	};
}