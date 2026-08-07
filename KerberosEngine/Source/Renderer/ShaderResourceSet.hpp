#pragma once

#include "Vulkan.hpp"

namespace Kerberos
{
	struct ShaderResourceSet
	{
		// For traditional descriptor sets
		vk::DescriptorSet DescriptorSet = nullptr;

		// For descriptor buffers
		vk::Buffer BackingBuffer = nullptr;
		vk::DeviceAddress BufferAddress = 0;
		vk::DeviceSize BufferOffset = 0;
		void* MappedData = nullptr;
	};
}