#pragma once

#include "Vulkan.hpp"

#include <cstdint>

namespace Kerberos {

enum class UpscalerType {
    Native,
    FSR3
};

enum class UpscalerQuality {
    /// Renders at native (display) resolution and uses the upscaler only for
    /// its temporal anti-aliasing.
    NativeAA,
    // Renders at 77% of display resolution.
    UltraQuality,
    // Renders at 66% of display resolution.
    Quality,
    // Renders at 58% of display resolution.
    Balanced,
    // Renders at 50% of display resolution.
    Performance,
    // Renders at 33% of display resolution.
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
    UpscalerTexture inputReactive;      /// Unscaled reactive mask (eR8Unorm)
    UpscalerTexture outputColor;        /// Upscaled display-resolution target

    float cameraNear;
    float cameraFar;
    float cameraFovAngleVertical; /// Field of view in radians
    float viewSpaceToMetersFactor = 1.0f;
    /// Exposure scale applied by the tone-mapping pass. Temporal upscalers use it
    /// to normalize HDR input the same way the presentation path will.
    float preExposure = 1.0f;
};

struct UpscalerFrame {
    uint64_t frameIndex;

    float jitterX;
    float jitterY;

    float deltaTime;

    bool resetHistory = false;
};

}
