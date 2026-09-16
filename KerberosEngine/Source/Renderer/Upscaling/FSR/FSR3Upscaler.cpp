#include "FSR3Upscaler.hpp"

#include <ffx_api/ffx_upscale.hpp>
#include <ffx_api/ffx_api_types.h>
#include <ffx_api/vk/ffx_api_vk.hpp>

#include <algorithm>
#include <cstring>
#include <stdexcept>

import Kerberos;

namespace Kerberos {

    namespace {
        constexpr float GetUpscaleRatio(const UpscalerQuality quality) {
            switch (quality) {
                case UpscalerQuality::UltraQuality:     return 1.3f;
                case UpscalerQuality::Quality:          return 1.5f;
                case UpscalerQuality::Balanced:         return 1.7f;
                case UpscalerQuality::Performance:      return 2.0f;
                case UpscalerQuality::UltraPerformance: return 3.0f;
                default:                                return 1.0f;
            }
        }
    }

    PFN_vkVoidFunction VKAPI_CALL FSR3Upscaler::OverrideVkGetDeviceProcAddr(
        const VkDevice device,
        const char* name)
    {
        if (device == VK_NULL_HANDLE || name == nullptr)
            return nullptr;

        struct FunctionEntry
        {
            const char* name;
            PFN_vkVoidFunction function;
        };

#define KBR_VK_FUNCTION_ENTRY(function) \
        { #function, reinterpret_cast<PFN_vkVoidFunction>(function) }
        static const FunctionEntry functions[] = {
            KBR_VK_FUNCTION_ENTRY(vkCreateDescriptorPool),
            KBR_VK_FUNCTION_ENTRY(vkCreateSampler),
            KBR_VK_FUNCTION_ENTRY(vkCreateDescriptorSetLayout),
            KBR_VK_FUNCTION_ENTRY(vkCreateBuffer),
            KBR_VK_FUNCTION_ENTRY(vkCreateBufferView),
            KBR_VK_FUNCTION_ENTRY(vkCreateImage),
            KBR_VK_FUNCTION_ENTRY(vkCreateImageView),
            KBR_VK_FUNCTION_ENTRY(vkCreateShaderModule),
            KBR_VK_FUNCTION_ENTRY(vkCreatePipelineLayout),
            KBR_VK_FUNCTION_ENTRY(vkCreateComputePipelines),
            KBR_VK_FUNCTION_ENTRY(vkDestroyPipelineLayout),
            KBR_VK_FUNCTION_ENTRY(vkDestroyPipeline),
            KBR_VK_FUNCTION_ENTRY(vkDestroyImage),
            KBR_VK_FUNCTION_ENTRY(vkDestroyImageView),
            KBR_VK_FUNCTION_ENTRY(vkDestroyBuffer),
            KBR_VK_FUNCTION_ENTRY(vkDestroyBufferView),
            KBR_VK_FUNCTION_ENTRY(vkDestroyDescriptorSetLayout),
            KBR_VK_FUNCTION_ENTRY(vkDestroyDescriptorPool),
            KBR_VK_FUNCTION_ENTRY(vkDestroySampler),
            KBR_VK_FUNCTION_ENTRY(vkDestroyShaderModule),
            KBR_VK_FUNCTION_ENTRY(vkGetBufferMemoryRequirements),
            KBR_VK_FUNCTION_ENTRY(vkGetImageMemoryRequirements),
            KBR_VK_FUNCTION_ENTRY(vkAllocateDescriptorSets),
            KBR_VK_FUNCTION_ENTRY(vkFreeDescriptorSets),
            KBR_VK_FUNCTION_ENTRY(vkAllocateMemory),
            KBR_VK_FUNCTION_ENTRY(vkFreeMemory),
            KBR_VK_FUNCTION_ENTRY(vkMapMemory),
            KBR_VK_FUNCTION_ENTRY(vkUnmapMemory),
            KBR_VK_FUNCTION_ENTRY(vkBindBufferMemory),
            KBR_VK_FUNCTION_ENTRY(vkBindImageMemory),
            KBR_VK_FUNCTION_ENTRY(vkUpdateDescriptorSets),
            KBR_VK_FUNCTION_ENTRY(vkFlushMappedMemoryRanges),
            KBR_VK_FUNCTION_ENTRY(vkCmdPipelineBarrier),
            KBR_VK_FUNCTION_ENTRY(vkCmdBindPipeline),
            KBR_VK_FUNCTION_ENTRY(vkCmdBindDescriptorSets),
            KBR_VK_FUNCTION_ENTRY(vkCmdDispatch),
            KBR_VK_FUNCTION_ENTRY(vkCmdDispatchIndirect),
            KBR_VK_FUNCTION_ENTRY(vkCmdCopyBuffer),
            KBR_VK_FUNCTION_ENTRY(vkCmdCopyImage),
            KBR_VK_FUNCTION_ENTRY(vkCmdCopyBufferToImage),
            KBR_VK_FUNCTION_ENTRY(vkCmdClearColorImage),
            KBR_VK_FUNCTION_ENTRY(vkCmdFillBuffer),
        };
#undef KBR_VK_FUNCTION_ENTRY

        for (const FunctionEntry& entry : functions)
        {
            if (std::strcmp(name, entry.name) == 0)
            {
                return entry.function;
            }
        }

        // FidelityFX 1.1.4 requests the KHR name even when the core Vulkan 1.1
        // entry point is the one exposed by the loader.
        const PFN_vkVoidFunction function =
            std::strcmp(name, "vkGetBufferMemoryRequirements2KHR") == 0
                ? reinterpret_cast<PFN_vkVoidFunction>(vkGetBufferMemoryRequirements2)
                : ::vkGetDeviceProcAddr(device, name);

        if (function == nullptr)
        {
            if (std::strcmp(name, "vkCmdWriteBufferMarkerAMD") == 0 ||
                std::strcmp(name, "vkCmdWriteBufferMarker2AMD") == 0) {
                // These functions are optional and not required for FSR3 to function.
                // And on any non-AMD device, they would produce warnings every frame.
                return nullptr;
            }
            Log::CoreWarn("FidelityFX FSR3 requested unavailable Vulkan function: {}", name);
        }
        return function;
    }

    FSR3Upscaler::FSR3Upscaler(const vk::Device device,
                               const vk::PhysicalDevice physicalDevice,
                               const PFN_vkGetDeviceProcAddr deviceProcAddr)
        : m_Device(device), m_PhysicalDevice(physicalDevice), m_DeviceProcAddr(deviceProcAddr)
    {
    }

    FSR3Upscaler::~FSR3Upscaler()
    {
        FSR3Upscaler::Release();
    }

    void FSR3Upscaler::Initialize(const UpscalerCreateInfo& createInfo)
    {
        const float upscaleRatio = GetUpscaleRatio(createInfo.quality);
        m_Settings = Settings{ .displayWidth = createInfo.displayWidth,
                               .displayHeight = createInfo.displayHeight,
                               .renderWidth = static_cast<uint32_t>(static_cast<float>(createInfo.displayWidth) / upscaleRatio),
                               .renderHeight = static_cast<uint32_t>(static_cast<float>(createInfo.displayHeight) / upscaleRatio),
                               .quality = createInfo.quality };

        ffx::CreateBackendVKDesc backendDesc{};
        backendDesc.vkDevice = static_cast<VkDevice>(m_Device);
        backendDesc.vkPhysicalDevice = static_cast<VkPhysicalDevice>(m_PhysicalDevice);
        backendDesc.vkDeviceProcAddr = &FSR3Upscaler::OverrideVkGetDeviceProcAddr;

        if (backendDesc.vkDevice == VK_NULL_HANDLE ||
            backendDesc.vkPhysicalDevice == VK_NULL_HANDLE ||
            backendDesc.vkDeviceProcAddr == nullptr ||
            backendDesc.vkDeviceProcAddr(backendDesc.vkDevice, "vkGetDeviceQueue") == nullptr)
        {
            Log::CoreError("FidelityFX FSR3: Invalid Vulkan backend handles or device procedure address");
            return;
        }

        ffx::CreateContextDescUpscale upscaleDesc{};
        upscaleDesc.maxRenderSize = {
            .width = m_Settings.renderWidth,
            .height = m_Settings.renderHeight
        };
        upscaleDesc.maxUpscaleSize = {
            .width = m_Settings.displayWidth,
            .height = m_Settings.displayHeight
        };
        upscaleDesc.flags = /*FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE |*/
                            FFX_UPSCALE_ENABLE_AUTO_EXPOSURE |
                            FFX_UPSCALE_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION | // TODO: use unjittered matrices for motion vectors
                            FFX_UPSCALE_ENABLE_DEBUG_CHECKING;

        upscaleDesc.fpMessage = nullptr;

        if (const ffx::ReturnCode ret
            = ffx::CreateContext(m_Context, nullptr, upscaleDesc, backendDesc);
            !ret)
        {
            switch (ret)
            {
                case ffx::ReturnCode::Error:
                    Log::CoreError("FidelityFX FSR3: Error in CreateContext");
                    break;
                case ffx::ReturnCode::ErrorUnknownDesctype:
                    Log::CoreError("FidelityFX FSR3: Unknown desc type in CreateContext");
                    break;
                case ffx::ReturnCode::ErrorRuntimeError:
                    Log::CoreError("FidelityFX FSR3: Runtime error in CreateContext");
                    break;
                case ffx::ReturnCode::ErrorNoProvider:
                    Log::CoreError("FidelityFX FSR3: No provider in CreateContext");
                    break;
                case ffx::ReturnCode::ErrorMemory:
                    Log::CoreError("FidelityFX FSR3: Memory error in CreateContext");
                    break;
                case ffx::ReturnCode::ErrorParameter:
                    Log::CoreError("FidelityFX FSR3: Invalid parameter in CreateContext");
                    break;
                default:
                    KBRAssert(false, "FidelityFX FSR3: Unknown error in CreateContext");
            }
            m_Context = nullptr;
        }
    }

    void FSR3Upscaler::Release()
    {
        if (m_Context != nullptr)
        {
            ffx::DestroyContext(m_Context);
            m_Context = nullptr;
        }
        m_HasBegunFrame = false;
    }

    void FSR3Upscaler::Resize(const uint32_t displayWidth, const uint32_t displayHeight)
    {
        // FSR requires a full context recreation when the resolution changes
        Release();

        Initialize({ .displayWidth = displayWidth, .displayHeight = displayHeight, .quality = m_Settings.quality });
    }

    void FSR3Upscaler::BeginFrame(const UpscalerFrame &frame)
    {
        KBRAssert(m_Context != nullptr, "FSR3Upscaler::BeginFrame called before Initialize");

        m_Frame = frame;

        ffx::QueryDescUpscaleGetJitterPhaseCount phaseCountDesc{};
        phaseCountDesc.renderWidth = m_Settings.renderWidth;
        phaseCountDesc.displayWidth = m_Settings.displayWidth;
        int32_t phaseCount = 1;
        phaseCountDesc.pOutPhaseCount = &phaseCount;

        ffx::QueryDescUpscaleGetJitterOffset jitterDesc{};
        jitterDesc.phaseCount = std::max(phaseCount, 1);
        jitterDesc.pOutX = &m_JitterX;
        jitterDesc.pOutY = &m_JitterY;
 
        if (const ffx::ReturnCode ret = ffx::Query(m_Context, phaseCountDesc); !ret)
        {
            Log::CoreError("FidelityFX FSR3: Error querying jitter phase count");

            m_JitterX = frame.jitterX;
            m_JitterY = frame.jitterY;
        }
        else
        {
            jitterDesc.index = static_cast<int32_t>(
                m_Frame.frameIndex % static_cast<uint64_t>(std::max(phaseCount, 1)));
            if (const ffx::ReturnCode queryRet = ffx::Query(m_Context, jitterDesc); !queryRet)
            {
                Log::CoreError("FidelityFX FSR3: Error querying jitter offset");

                m_JitterX = frame.jitterX;
                m_JitterY = frame.jitterY;
            }
        }
        
        m_HasBegunFrame = true;
    }

    static FfxApiResourceDescription GetFFXApiResourceDesc(const UpscalerTexture& texture)
    {
        const VkImageCreateInfo createInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = static_cast<VkFormat>(texture.format),
            .extent = { .width = texture.width, .height = texture.height, .depth = 1 },
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = static_cast<VkImageUsageFlags>(texture.usage),
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = static_cast<VkImageLayout>(texture.currentLayout)
        };

        const FfxApiResourceDescription description = ffxApiGetImageResourceDescriptionVK(texture.image, createInfo, 0u);
        return description;
    }

    void FSR3Upscaler::Dispatch(const UpscalerDispatchInfo& dispatchInfo)
    {
        if (m_Context == nullptr || !m_HasBegunFrame)
            return;

        ffx::DispatchDescUpscale dispatchDesc{};

        dispatchDesc.commandList = static_cast<VkCommandBuffer>(dispatchInfo.commandBuffer);

        const FfxApiResourceDescription colorDesc = GetFFXApiResourceDesc(dispatchInfo.inputColor);
        dispatchDesc.color = ffxApiGetResourceVK(dispatchInfo.inputColor.image, colorDesc, FFX_API_RESOURCE_STATE_COMPUTE_READ);

        const FfxApiResourceDescription depthDesc = GetFFXApiResourceDesc(dispatchInfo.inputDepth);
        dispatchDesc.depth = ffxApiGetResourceVK(dispatchInfo.inputDepth.image, depthDesc, FFX_API_RESOURCE_STATE_COMPUTE_READ);

        const FfxApiResourceDescription motionVectorsDesc = GetFFXApiResourceDesc(dispatchInfo.inputMotionVectors);
        dispatchDesc.motionVectors = ffxApiGetResourceVK(dispatchInfo.inputMotionVectors.image, motionVectorsDesc, FFX_API_RESOURCE_STATE_COMPUTE_READ);

        const FfxApiResourceDescription outputDesc = GetFFXApiResourceDesc(dispatchInfo.outputColor);
        dispatchDesc.output = ffxApiGetResourceVK(dispatchInfo.outputColor.image, outputDesc, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);

        dispatchDesc.renderSize = {
            .width = m_Settings.renderWidth,
            .height = m_Settings.renderHeight
        };

        dispatchDesc.upscaleSize = {
            .width = m_Settings.displayWidth,
            .height = m_Settings.displayHeight
        };

        dispatchDesc.jitterOffset = {
            .x = -m_JitterX,
            .y = -m_JitterY
        };

        dispatchDesc.reset = m_Frame.resetHistory;
        dispatchDesc.frameTimeDelta = m_Frame.deltaTime * 1000.0f;

        dispatchDesc.motionVectorScale = {
            .x = 1.0f,
            .y = 1.0f
        };

        // Camera parameters
        dispatchDesc.cameraNear = dispatchInfo.cameraNear;
        dispatchDesc.cameraFar = dispatchInfo.cameraFar;
        dispatchDesc.cameraFovAngleVertical = dispatchInfo.cameraFovAngleVertical;
        dispatchDesc.viewSpaceToMetersFactor = dispatchInfo.viewSpaceToMetersFactor;
        dispatchDesc.preExposure = 1.0f;

        dispatchDesc.enableSharpening = true;
        dispatchDesc.sharpness = 0.8f;

        if (const ffx::ReturnCode ret = ffx::Dispatch(m_Context, dispatchDesc); !ret)
        {
            Log::CoreError("FidelityFX FSR3: Error during Dispatch");
        }
    }

    void FSR3Upscaler::SetQuality(const UpscalerQuality quality)
    {
        m_Settings.quality = quality;

        Resize(m_Settings.displayWidth, m_Settings.displayHeight);
    }

    glm::vec2 FSR3Upscaler::GetJitterOffset() const
    {
        return { m_JitterX, m_JitterY };
    }

    float FSR3Upscaler::GetInverseUpscaleRatio() const
    {
        return 1.0f / GetUpscaleRatio(m_Settings.quality);
    }

}
