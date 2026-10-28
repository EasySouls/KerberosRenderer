#include "VulkanBuffer.hpp"

#include "VulkanContext.hpp"

import Kerberos;

namespace Kerberos {

void VulkanBuffer::Reset()
{
    KBRAssert(Handle != nullptr && Allocation != nullptr, "VulkanBuffer is not initialized");

    const auto allocator = VulkanContext::Get().GetAllocator().get();

    vmaUnmapMemory(allocator, Allocation);
    vmaDestroyBuffer(allocator, Handle, Allocation);

    Handle = nullptr;
    Allocation = nullptr;
    MappedData = nullptr;
    DeviceAddress = 0;
    Size = 0;
}

}