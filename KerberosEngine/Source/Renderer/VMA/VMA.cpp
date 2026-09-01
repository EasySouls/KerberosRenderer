#include "VMA.hpp"
#include "Utils/VulkanHelpers.hpp"


#define VMA_IMPLEMENTATION
#include "vma/vk_mem_alloc.h"

import Kerberos;

namespace Kerberos::VMA
{
	void Deleter::operator()(const VmaAllocator allocator) const noexcept
	{
		vmaDestroyAllocator(allocator);
	}

	Allocator CreateAllocator(const vk::Instance instance, const vk::PhysicalDevice physicalDevice, const vk::Device device)
	{
		VmaAllocatorCreateInfo allocatorCi = VmaAllocatorCreateInfo{};
		allocatorCi.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
		allocatorCi.physicalDevice = physicalDevice;
		allocatorCi.device = device;
		allocatorCi.instance = instance;

		VmaAllocator allocator;
		if (const VkResult result = vmaCreateAllocator(&allocatorCi, &allocator); result != VK_SUCCESS)
		{
			Log::CoreError("Failed to create vma allocator: {}", VulkanHelpers::VkResultToString(static_cast<vk::Result>(result)));
			KBRAssert(false, "Failed to create Vulkan Memory Allocator: {}", VulkanHelpers::VkResultToString(static_cast<vk::Result>(result)));
			return {};
		}

		return Allocator{ allocator };
	}
}