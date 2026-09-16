#pragma once

#include "Vulkan.hpp"

#include <cstdint>

namespace Kerberos {

enum class UpscalerType {
    Native,
    FSR3
};

enum class UpscalerQuality {
    UltraQuality,
    Quality,
    Balanced,
    Performance,
    UltraPerformance
};

struct UpscalerCreateInfo {
    uint32_t displayWidth{};
    uint32_t displayHeight{};

    UpscalerQuality quality = UpscalerQuality::Balanced;
};

struct UpscalerTexture {
    vk::Image image;
    vk::ImageView view;
    vk::Format format;
    vk::ImageUsageFlags usage;
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
    float cameraFovAngleVertical; /// Field of view in radians
    float viewSpaceToMetersFactor = 1.0f;
};

struct UpscalerFrame {
    uint64_t frameIndex;

    float jitterX;
    float jitterY;

    float deltaTime;

    bool resetHistory = false;
};

}
