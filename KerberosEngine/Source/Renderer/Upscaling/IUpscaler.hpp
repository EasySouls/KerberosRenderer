#pragma once

#include "UpscalerTypes.hpp"
#include "Core/Core.hpp"

#include <glm/vec2.hpp>

namespace  Kerberos {

class IUpscaler {
public:
    IUpscaler() = default;
    virtual ~IUpscaler() = default;

    IUpscaler(const IUpscaler&) = delete;
    IUpscaler& operator=(const IUpscaler&) = delete;
    IUpscaler(IUpscaler&&) = delete;
    IUpscaler& operator=(IUpscaler&&) = delete;

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

Owner<IUpscaler> CreateUpscaler(UpscalerType type);

}
