#include "VulkanBuffer.hpp"

#include "VulkanContext.hpp"

import Kerberos;

namespace Kerberos {

void VulkanBuffer::Reset()
{
    if (Handle == nullptr && Allocation == nullptr)
        return;

    const auto allocator = VulkanContext::Get().GetAllocator().get();

    if (MappedData != nullptr)
        vmaUnmapMemory(allocator, Allocation);

    vmaDestroyBuffer(allocator, Handle, Allocation);

    Handle = nullptr;
    Allocation = nullptr;
    MappedData = nullptr;
    DeviceAddress = 0;
    Size = 0;
}

void CreateBuffer(VulkanBuffer& buffer,
    const vk::DeviceSize size,
    const vk::BufferUsageFlags usage,
    const bool hostVisible,
    const char* name)
{
    auto& context = VulkanContext::Get();

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = static_cast<VkBufferUsageFlags>(usage);
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    if (hostVisible)
        allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    else
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    VkBuffer handle = nullptr;
    if (vmaCreateBuffer(context.GetAllocator().get(), &bufferInfo, &allocInfo, &handle, &buffer.Allocation, nullptr) !=
        VK_SUCCESS)
    {
        KBRAssert("Failed to create {}", name);
        return;
    }

    buffer.Handle = vk::Buffer(handle);
    buffer.Size = size;
    buffer.DeviceAddress = context.GetDevice().getBufferAddress({ .buffer = buffer.Handle });

    if (hostVisible)
    {
        vmaMapMemory(context.GetAllocator().get(), buffer.Allocation, &buffer.MappedData);
        std::memset(buffer.MappedData, 0, static_cast<size_t>(size));
    }

    context.SetObjectDebugName(
        reinterpret_cast<uint64_t>(static_cast<VkBuffer>(buffer.Handle)), vk::ObjectType::eBuffer, name);
    context.SetObjectDebugName(buffer.Allocation, std::string(name) + " Allocation");
}

}