#include "NativeUpscaler.hpp"

import Kerberos;

namespace Kerberos {

    NativeUpscaler::NativeUpscaler(const vk::Device /*device*/)
    {
    }


    void NativeUpscaler::Initialize(const UpscalerCreateInfo& /*settings*/)
    {
    }

    void NativeUpscaler::Release()
    {
    }

    void NativeUpscaler::Resize(const uint32_t /*displayWidth*/, const uint32_t /*displayHeight*/)
    {
    }

    void NativeUpscaler::BeginFrame(const UpscalerFrame& /*frame*/)
    {
    }

    void NativeUpscaler::Dispatch(const UpscalerDispatchInfo& /*dispatchInfo*/)
    {
    }

    void NativeUpscaler::SetQuality(const UpscalerQuality /*quality*/)
    {
    }

    glm::vec2 NativeUpscaler::GetJitterOffset() const
    {
        return glm::vec2(0.0f, 0.0f);
    }

    float NativeUpscaler::GetInverseUpscaleRatio() const
    {
        return 1.0f;
    }

}
