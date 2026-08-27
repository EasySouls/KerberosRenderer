#pragma once

#include "Texture.hpp"

namespace Kerberos::KTX2FormatSelector {

[[nodiscard]] vk::Format Select(ImageFormat format);
[[nodiscard]] bool IsSupported(vk::Format format);

}
