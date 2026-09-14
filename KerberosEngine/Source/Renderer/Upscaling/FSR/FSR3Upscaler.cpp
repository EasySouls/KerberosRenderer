#include "FSR3Upscaler.hpp"

#include <ffx_api/ffx_upscale.hpp>
#include <ffx_api/ffx_api_types.h>
#include <ffx_api/vk/ffx_api_vk.hpp>
#include "FidelityFX-SDK/sdk/include/FidelityFX/host/ffx_fsr3upscaler.h"

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

    FSR3Upscaler::FSR3Upscaler(const vk::Device device)
        : m_Device(device)
    {
    }

    FSR3Upscaler::~FSR3Upscaler()
    {
        FSR3Upscaler::Release();
    }

    void FSR3Upscaler::Initialize(const UpscalerCreateInfo& settings)
    {
        m_Settings = settings;
        m_Quality = settings.initialQuality;

        // TODO set render size based on quality

        ffx::CreateBackendVKDesc backendDesc{};
        backendDesc.vkDevice = m_Device;

        ffx::CreateContextDescUpscale upscaleDesc{};
        upscaleDesc.maxRenderSize = {
            .width = static_cast<uint32_t>(settings.renderWidth),
            .height = static_cast<uint32_t>(settings.renderHeight)
        };
        upscaleDesc.maxUpscaleSize = {
            .width = static_cast<uint32_t>(settings.displayWidth),
            .height = static_cast<uint32_t>(settings.displayHeight)
        };
        upscaleDesc.flags = FFX_FSR3UPSCALER_ENABLE_DEBUG_CHECKING;

        if (const ffx::ReturnCode ret
            = ffx::CreateContext(m_Context, nullptr, backendDesc, upscaleDesc);
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
        }
    }

    void FSR3Upscaler::Release()
    {
        ffx::DestroyContext(m_Context);
    }

    void FSR3Upscaler::Resize(const uint32_t displayWidth, const uint32_t displayHeight)
    {
        // FSR requires a full context recreation when the resolution changes
        Release();

        m_Settings.displayWidth = displayWidth;
        m_Settings.displayHeight = displayHeight;
        Initialize(m_Settings);
    }

    static float CalculateHalton(const int index, const int base)
    {
        float result = 0.0f;
        float f = 1.0f / static_cast<float>(base);
        int i = index;
        while (i > 0)
        {
            result += f * static_cast<float>(i % base);
            i /= base;
            f /= static_cast<float>(base);
        }
        return result;
    }

    void FSR3Upscaler::BeginFrame(const UpscalerFrame &frame)
    {
        m_Frame = frame;

        const float upscaleRatio = static_cast<float>(m_Settings.displayWidth) / m_Settings.renderWidth;
        const int phaseCount = static_cast<int>(8.0f * (upscaleRatio * upscaleRatio));

        const int frameIndex = m_Frame.frameIndex % phaseCount;
        m_JitterX = CalculateHalton(frameIndex + 1, 2) - 0.5f;
        m_JitterY = CalculateHalton(frameIndex + 1, 3) - 0.5f;
    }

    static FfxApiResourceDescription GetFFXApiResourceDesc(const UpscalerTexture& texture)
    {
        const vk::ImageCreateInfo createInfo {
            .imageType = vk::ImageType::e2D,
            .format = texture.format,
            .extent = {
                .width = texture.width,
                .height = texture.height,
                .depth = 1
            }, // TODO: This is not customizable right now
            .mipLevels = 1,
            .arrayLayers = 1,
            .initialLayout = texture.currentLayout
        };

        const FfxApiResourceDescription description = ffxApiGetImageResourceDescriptionVK(texture.image, createInfo, 0u);
        return description;
    }

    void FSR3Upscaler::Dispatch(const UpscalerDispatchInfo& dispatchInfo)
    {
        ffx::DispatchDescUpscale dispatchDesc{};

        dispatchDesc.commandList = dispatchInfo.commandBuffer;

        const FfxApiResourceDescription colorDesc = GetFFXApiResourceDesc(dispatchInfo.inputColor);
        dispatchDesc.color = ffxApiGetResourceVK(dispatchInfo.inputColor.image, colorDesc, 0u);

        const FfxApiResourceDescription depthDesc = GetFFXApiResourceDesc(dispatchInfo.inputDepth);
        dispatchDesc.depth = ffxApiGetResourceVK(dispatchInfo.inputDepth.image, depthDesc, 0u);

        const FfxApiResourceDescription motionVectorsDesc = GetFFXApiResourceDesc(dispatchInfo.inputMotionVectors);
        dispatchDesc.motionVectors = ffxApiGetResourceVK(dispatchInfo.inputMotionVectors.image, motionVectorsDesc, 0u);

        const FfxApiResourceDescription outputDesc = GetFFXApiResourceDesc(dispatchInfo.outputColor);
        dispatchDesc.output = ffxApiGetResourceVK(dispatchInfo.outputColor.image, outputDesc, 0u);

        dispatchDesc.renderSize = {
            .width = m_Settings.renderWidth,
            .height = m_Settings.renderHeight
        };

        dispatchDesc.upscaleSize = {
            .width = m_Settings.displayWidth,
            .height = m_Settings.displayHeight
        };

        dispatchDesc.jitterOffset = {
            .x = m_JitterX,
            .y = m_JitterY
        };

        dispatchDesc.reset = m_Frame.resetHistory;
        dispatchDesc.frameTimeDelta = m_Frame.deltaTime;

        // TODO: If outputting motion vectors in NDC space [-1, 1], scale them to UV space
        dispatchDesc.motionVectorScale = {
            .x = static_cast<float>(dispatchInfo.inputColor.width),
            .y = static_cast<float>(dispatchInfo.inputColor.height)
        };

        // Camera parameters
        dispatchDesc.cameraNear = dispatchInfo.cameraNear;
        dispatchDesc.cameraFar = dispatchInfo.cameraFar;
        dispatchDesc.cameraFovAngleVertical = dispatchInfo.cameraFovAngleVertical;

        dispatchDesc.enableSharpening = true;
        dispatchDesc.sharpness = 0.8f;

        if (const ffx::ReturnCode ret = ffx::Dispatch(m_Context, dispatchDesc); !ret)
        {
            Log::CoreError("FidelityFX FSR3: Error during Dispatch");
        }
    }

    void FSR3Upscaler::SetQuality(const UpscalerQuality quality)
    {
        const float scaleRatio = GetUpscaleRatio(quality);

        m_Settings.renderWidth = static_cast<uint32_t>(static_cast<float>(m_Settings.displayWidth) / scaleRatio);
        m_Settings.renderHeight = static_cast<uint32_t>(static_cast<float>(m_Settings.displayHeight) / scaleRatio);

        m_Quality = quality;

        Resize(m_Settings.displayWidth, m_Settings.displayHeight);
    }

    glm::vec2 FSR3Upscaler::GetJitterOffset() const
    {
        return { m_JitterX, m_JitterY };
    }

    float FSR3Upscaler::GetInverseUpscaleRatio() const
    {
        return 1.0f / GetUpscaleRatio(m_Quality);
    }

}
