#pragma once

#include "Vulkan.hpp"

#include <cstdint>

namespace Kerberos {

enum class UpscalerQuality {
    UltraQuality = 1,
    Quality,
    Balanced,
    Performance,
    UltraPerformance
};

struct UpscalerCreateInfo {
    uint32_t displayWidth{};
    uint32_t displayHeight{};

    uint32_t renderWidth{};
    uint32_t renderHeight{};

    float maxUpscaleRatio = 2.0f;

    UpscalerQuality initialQuality = UpscalerQuality::Balanced;
};

struct UpscalerTexture {
    vk::Image image;
    vk::ImageView view;
    vk::Format format;
    uint32_t width;
    uint32_t height;
    vk::ImageLayout currentLayout;
};

struct UpscalerDispatchInfo {
    vk::CommandBuffer commandBuffer;

    UpscalerTexture inputColor;         /// Unscaled, aliased render output
    UpscalerTexture inputDepth;         /// Unscaled depth buffer
    UpscalerTexture inputMotionVectors; /// Unscaled velocity buffer (eR16G16Sfloat)
    UpscalerTexture outputColor;        /// Upscaled display-resolution target

    float cameraNear;
    float cameraFar;
    float cameraFovAngleVertical; /// Filed of view in radians
};

struct UpscalerFrame {
    uint64_t frameIndex;

    float jitterX;
    float jitterY;

    float deltaTime;

    bool resetHistory = false;
};

}
