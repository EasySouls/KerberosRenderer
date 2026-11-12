#pragma once

#include "Vulkan.hpp"

#include <filesystem>

struct ktxTexture2;

namespace Kerberos::KTX2Loader {

[[nodiscard]] bool IsKTX2(const std::filesystem::path& filepath);
[[nodiscard]] bool Load(const std::filesystem::path& filepath, ktxTexture2** texture);

/// Returns the Vulkan format to use for sampling a KTX2 texture. The embedded
/// vkFormat normally already carries sRGB, but files converted from PNG can
/// carry UNORM plus an sRGB transfer function in the data format descriptor.
[[nodiscard]] vk::Format ResolveVulkanFormat(ktxTexture2* texture);

}
