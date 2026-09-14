#pragma once

#include "Renderer/Upscaling/IUpscaler.hpp"

#include "Vulkan.hpp"

#include <ffx_api/ffx_api.hpp>

namespace Kerberos {

class FSR3Upscaler : public IUpscaler {
public:
    explicit FSR3Upscaler(vk::Device device);
    ~FSR3Upscaler() override;

    void Initialize(const UpscalerCreateInfo& settings) override;
    void Release() override;

    void Resize(uint32_t displayWidth, uint32_t displayHeight) override;

    void BeginFrame(const UpscalerFrame& frame) override;

    void Dispatch(const UpscalerDispatchInfo& dispatchInfo) override;

    void SetQuality(UpscalerQuality quality) override;

    glm::vec2 GetJitterOffset() const override;

    float GetInverseUpscaleRatio() const override;

private:
    vk::Device m_Device{nullptr};

    ffx::Context m_Context{nullptr};

    UpscalerCreateInfo m_Settings{};
    UpscalerFrame m_Frame{};

    UpscalerQuality m_Quality{};

    float m_JitterX{0.0f};
    float m_JitterY{0.0f};
};

}