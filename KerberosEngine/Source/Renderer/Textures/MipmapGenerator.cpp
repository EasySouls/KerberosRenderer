#include "MipmapGenerator.hpp"
#include "VulkanContext.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace Kerberos::MipmapGenerator {

uint32_t CalculateMipLevelCount(const uint32_t width, const uint32_t height)
{
	const uint32_t largestExtent = std::max(width, height);
	if (largestExtent <= 1)
		return 1;

	return 1u + static_cast<uint32_t>(std::floor(std::log2(static_cast<float>(largestExtent))));
}

bool CanGenerate(const vk::Format format)
{
	if (format == vk::Format::eUndefined)
		return false;

	const auto& context = VulkanContext::Get();
	const vk::FormatProperties properties = context.GetFormatProperties(format).formatProperties;
	constexpr vk::FormatFeatureFlags required =
		vk::FormatFeatureFlagBits::eBlitSrc |
		vk::FormatFeatureFlagBits::eBlitDst |
		vk::FormatFeatureFlagBits::eSampledImageFilterLinear;

	return (properties.optimalTilingFeatures & required) == required;
}

void Generate(const vk::raii::CommandBuffer& cmd,
	const vk::raii::Image& image,
	const uint32_t width,
	const uint32_t height,
	const uint32_t mipLevels,
	const uint32_t arrayLayers)
{
	if (mipLevels <= 1 || arrayLayers == 0)
		return;

	const auto& context = VulkanContext::Get();

	// Every level starts as a copy destination.
	// Level 0 already has the base image. 
	// Each generated level becomes a transfer source before it is used to generate the next one.
	std::vector<vk::ImageLayout> layouts(mipLevels, vk::ImageLayout::eTransferDstOptimal);

	for (uint32_t level = 1; level < mipLevels; ++level)
	{
		const vk::ImageSubresourceRange sourceRange{
			.aspectMask = vk::ImageAspectFlagBits::eColor,
			.baseMipLevel = level - 1,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = arrayLayers
		};
		const vk::ImageSubresourceRange destinationRange{
			.aspectMask = vk::ImageAspectFlagBits::eColor,
			.baseMipLevel = level,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = arrayLayers
		};

		if (layouts[level - 1] != vk::ImageLayout::eTransferSrcOptimal)
		{
			context.TransitionImageLayout(cmd, image, layouts[level - 1],
				vk::ImageLayout::eTransferSrcOptimal, sourceRange,
				vk::PipelineStageFlagBits2::eTransfer, vk::PipelineStageFlagBits2::eTransfer);
			layouts[level - 1] = vk::ImageLayout::eTransferSrcOptimal;
		}

		const uint32_t sourceWidth = std::max(1u, width >> (level - 1));
		const uint32_t sourceHeight = std::max(1u, height >> (level - 1));
		const uint32_t destinationWidth = std::max(1u, width >> level);
		const uint32_t destinationHeight = std::max(1u, height >> level);

		std::vector<vk::ImageBlit> blits;
		blits.reserve(arrayLayers);
		for (uint32_t layer = 0; layer < arrayLayers; ++layer)
		{
			vk::ImageBlit blit{};
			blit.srcSubresource = {
				.aspectMask = vk::ImageAspectFlagBits::eColor,
				.mipLevel = level - 1,
				.baseArrayLayer = layer,
				.layerCount = 1
			};
			blit.srcOffsets[0] = vk::Offset3D{ 0, 0, 0 };
			blit.srcOffsets[1] = vk::Offset3D{
				static_cast<int32_t>(sourceWidth),
				static_cast<int32_t>(sourceHeight),
				1
			};
			blit.dstSubresource = {
				.aspectMask = vk::ImageAspectFlagBits::eColor,
				.mipLevel = level,
				.baseArrayLayer = layer,
				.layerCount = 1
			};
			blit.dstOffsets[0] = vk::Offset3D{ 0, 0, 0 };
			blit.dstOffsets[1] = vk::Offset3D{
				static_cast<int32_t>(destinationWidth),
				static_cast<int32_t>(destinationHeight),
				1
			};
			blits.push_back(blit);
		}

		cmd.blitImage(image, vk::ImageLayout::eTransferSrcOptimal,
			image, vk::ImageLayout::eTransferDstOptimal,
			blits, vk::Filter::eLinear);

		context.TransitionImageLayout(cmd, image,
			vk::ImageLayout::eTransferDstOptimal,
			vk::ImageLayout::eTransferSrcOptimal,
			destinationRange,
			vk::PipelineStageFlagBits2::eTransfer, vk::PipelineStageFlagBits2::eTransfer);
		layouts[level] = vk::ImageLayout::eTransferSrcOptimal;
	}
}

}
