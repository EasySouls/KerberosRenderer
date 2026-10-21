#pragma once

#include "Core/Scoped.hpp"
#include "Vulkan.hpp"

#include <vma/vk_mem_alloc.h>

namespace Kerberos::VMA
{
    struct Deleter
    {
        void operator()(VmaAllocator allocator) const noexcept;
    };

    using Allocator = Scoped<VmaAllocator, Deleter>;

    [[nodiscard]]
    Allocator CreateAllocator(vk::Instance instance, vk::PhysicalDevice physicalDevice, vk::Device device);
}