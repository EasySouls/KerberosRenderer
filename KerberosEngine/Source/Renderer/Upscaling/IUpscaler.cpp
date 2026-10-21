#include "IUpscaler.hpp"
#include "Native/NativeUpscaler.hpp"
#include "FSR/FSR3Upscaler.hpp"
#include "VulkanContext.hpp"
#include "Vulkan.hpp"

import Kerberos;

namespace Kerberos {

Owner<IUpscaler> CreateUpscaler(const UpscalerType type)
{
    auto& context = VulkanContext::Get();
    const auto& device = context.GetDevice();
    const auto physicalDevice = context.GetPhysicalDevice();

    switch (type) {
    case UpscalerType::Native:
        return CreateOwner<NativeUpscaler>(*device);
    case UpscalerType::FSR3:
        return CreateOwner<FSR3Upscaler>(*device, *physicalDevice, vkGetDeviceProcAddr);
    default:
        KBRAssert(false, "Unsupported upscaler type!");
        return CreateOwner<NativeUpscaler>(*device);
    }
}

}