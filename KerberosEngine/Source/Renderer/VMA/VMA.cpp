#include "VMA.hpp"
#include "Utils/VulkanHelpers.hpp"

#ifdef _MSC_VER
	#pragma warning(push, 0)
#endif

#if defined(__clang__) || defined(__GNUC__)
	#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Wall"
	#pragma GCC diagnostic ignored "-Wextra"
	#pragma GCC diagnostic ignored "-Wpedantic"
#endif

#define VMA_IMPLEMENTATION
#include "vma/vk_mem_alloc.h"

#ifdef _MSC_VER
	#pragma warning(pop)
#endif

#if defined(__clang__) || defined(__GNUC__)
	#pragma GCC diagnostic pop
#endif

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