#pragma once

#include "Renderer/Upscaling/IUpscaler.hpp"

namespace Kerberos {

    /**
     * A no-op upscaler which renders the image in its display output.
     */
    class NativeUpscaler : public IUpscaler {
    public:
        explicit NativeUpscaler(vk::Device device);

        void Initialize(const UpscalerCreateInfo& settings) override;
        void Release() override;

        void Resize(uint32_t displayWidth, uint32_t displayHeight) override;

        void BeginFrame(const UpscalerFrame& frame) override;

        void Dispatch(const UpscalerDispatchInfo& dispatchInfo) override;

        void SetQuality(UpscalerQuality quality) override;

        glm::vec2 GetJitterOffset() const override;

        float GetInverseUpscaleRatio() const override;
    };

}