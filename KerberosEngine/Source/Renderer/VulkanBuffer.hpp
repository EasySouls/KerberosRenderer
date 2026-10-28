#pragma once

#include "Vulkan.hpp"
#include "VMA/VMA.hpp"

namespace Kerberos {

struct VulkanBuffer
{
    vk::Buffer Handle;
    VmaAllocation Allocation;
    void* MappedData = nullptr;
    vk::DeviceAddress DeviceAddress = 0;
    vk::DeviceSize Size = 0;

    void Reset();
};

void CreateBuffer(VulkanBuffer& buffer, vk::DeviceSize size, vk::BufferUsageFlags usage, bool hostVisible, const char* name);

}