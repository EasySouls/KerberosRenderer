#pragma once

#include "UpscalerTypes.hpp"

#include <glm/vec2.hpp>

namespace  Kerberos {

class IUpscaler {
public:
    virtual ~IUpscaler() = default;

    virtual void Initialize(const UpscalerCreateInfo& settings) = 0;
    virtual void Release() = 0;

    virtual void Resize(
       uint32_t displayWidth,
       uint32_t displayHeight
   ) = 0;

    virtual void BeginFrame(const UpscalerFrame& frame) = 0;

    virtual void Dispatch(const UpscalerDispatchInfo& dispatchInfo) = 0;

    virtual void SetQuality(UpscalerQuality quality) = 0;

    [[nodiscard]]
    virtual glm::vec2 GetJitterOffset() const = 0;

    [[nodiscard]]
    virtual float GetInverseUpscaleRatio() const = 0;
};

}
