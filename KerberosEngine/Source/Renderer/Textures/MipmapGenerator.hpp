#pragma once

#include "Vulkan.hpp"

#include <cstdint>

namespace Kerberos::MipmapGenerator {

/// Returns the number of levels in a full mip chain for the given extent.
[[nodiscard]] uint32_t CalculateMipLevelCount(uint32_t width, uint32_t height);

/// True when the format can be used as a blit source/destination with linear
/// filtering on the active device.
[[nodiscard]] bool CanGenerate(vk::Format format);

/// Generates levels 1..mipLevels-1 by blitting from the previous level.
/// The whole image must be in eTransferDstOptimal with the base level already
/// uploaded. Each array layer is filtered independently. 
/// On return all levels are in eTransferSrcOptimal. 
/// The caller transitions the whole image to its final layout.
void Generate(const vk::raii::CommandBuffer& cmd,
	const vk::raii::Image& image,
	uint32_t width,
	uint32_t height,
	uint32_t mipLevels,
	uint32_t arrayLayers = 1);

}
