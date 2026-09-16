#pragma once

#include "Renderer/Upscaling/IUpscaler.hpp"

#include "Vulkan.hpp"

#include <ffx_api/ffx_api.hpp>

namespace Kerberos {

class FSR3Upscaler : public IUpscaler {
public:
    FSR3Upscaler(vk::Device device, vk::PhysicalDevice physicalDevice, PFN_vkGetDeviceProcAddr deviceProcAddr);
    ~FSR3Upscaler() override;

    void Initialize(const UpscalerCreateInfo& createInfo) override;
    void Release() override;

    void Resize(uint32_t displayWidth, uint32_t displayHeight) override;

    void BeginFrame(const UpscalerFrame& frame) override;

    void Dispatch(const UpscalerDispatchInfo& dispatchInfo) override;

    void SetQuality(UpscalerQuality quality) override;

    glm::vec2 GetJitterOffset() const override;

    float GetInverseUpscaleRatio() const override;

    [[nodiscard]] bool IsInitialized() const { return m_Context != nullptr; }

private:
    static PFN_vkVoidFunction VKAPI_CALL OverrideVkGetDeviceProcAddr(
        VkDevice device,
        const char* name);

private:

    vk::Device m_Device{nullptr};
    vk::PhysicalDevice m_PhysicalDevice{nullptr};
    PFN_vkGetDeviceProcAddr m_DeviceProcAddr = nullptr;

    ffx::Context m_Context{nullptr};

    struct Settings
    {
        uint32_t displayWidth{};
        uint32_t displayHeight{};

        uint32_t renderWidth{};
        uint32_t renderHeight{};

        UpscalerQuality quality = UpscalerQuality::Balanced;
    };
    Settings m_Settings{};
    UpscalerFrame m_Frame{};

    float m_JitterX{0.0f};
    float m_JitterY{0.0f};
    bool m_HasBegunFrame = false;
};

}