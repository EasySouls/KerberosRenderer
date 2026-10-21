#pragma once

#include "Texture.hpp"

namespace Kerberos::KTX2FormatSelector {

[[nodiscard]] vk::Format Select(ImageFormat format);
[[nodiscard]] bool IsSupported(vk::Format format);

// Reinterprets the given format as its sRGB variant when one exists. Formats
// without an sRGB equivalent (single/dual channel, float) are returned
// unchanged.
[[nodiscard]] ImageFormat ToSrgb(ImageFormat format);
[[nodiscard]] vk::Format ToSrgb(vk::Format format);

}
