#include "TextureCube.hpp"
#include "VulkanContext.hpp"

#include "ktx.h"
#include "Utils.hpp"
#include "KTX2Loader.hpp"
#include "KTX2FormatSelector.hpp"
#include "Profiling/Profilers.hpp"

import Kerberos;

namespace Kerberos {

TextureCube::TextureCube(const CubemapData& data)
{
	KBR_TRACY_FUNCTION();

	const auto& firstFace = data.Faces[0];
	KBRAssert(firstFace.Buffer.Data != nullptr && firstFace.Buffer.Size > 0,
		"TextureCube::TextureCube - first cubemap face has no data");
	for (size_t face = 1; face < data.Faces.size(); ++face)
	{
		const auto& other = data.Faces[face];
		KBRAssert(other.Buffer.Data != nullptr && other.Buffer.Size == firstFace.Buffer.Size
			&& other.Specification.Width == firstFace.Specification.Width
			&& other.Specification.Height == firstFace.Specification.Height
			&& other.Specification.Format == firstFace.Specification.Format,
			"TextureCube::TextureCube - cubemap faces must share width, height, format, and buffer size");
	}

	m_Specification = firstFace.Specification;
	width = firstFace.Specification.Width;
	height = firstFace.Specification.Height;
	mipLevels = 1;
	layerCount = 6;
	usageFlags = vk::ImageUsageFlagBits::eSampled;
	imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
	format = KTX2FormatSelector::Select(m_Specification.Format);
	if (data.IsSRGB)
		format = KTX2FormatSelector::ToSrgb(format);

	auto& context = VulkanContext::Get();
	const auto& device = context.GetDevice();

	// Assemble a staging buffer holding all six faces back to back.
	const uint64_t faceSize = firstFace.Buffer.Size;
	vk::BufferCreateInfo bufferCreateInfo{
		.size = faceSize * data.Faces.size(),
		.usage = vk::BufferUsageFlagBits::eTransferSrc,
		.sharingMode = vk::SharingMode::eExclusive
	};
	vk::raii::Buffer stagingBuffer = device.createBuffer(bufferCreateInfo, nullptr);

	vk::MemoryRequirements memReqs = stagingBuffer.getMemoryRequirements();
	vk::MemoryAllocateInfo memAllocInfo{
		.allocationSize = memReqs.size,
		.memoryTypeIndex = FindMemoryType(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent)
	};
	vk::raii::DeviceMemory stagingMemory = device.allocateMemory(memAllocInfo, nullptr);
	stagingBuffer.bindMemory(stagingMemory, 0);

	uint8_t* mapped = static_cast<uint8_t*>(stagingMemory.mapMemory(0, memReqs.size));
	for (size_t face = 0; face < data.Faces.size(); ++face)
		std::memcpy(mapped + face * faceSize, data.Faces[face].Buffer.Data, faceSize);
	stagingMemory.unmapMemory();

	std::vector<vk::BufferImageCopy> bufferCopyRegions;
	for (uint32_t face = 0; face < 6; ++face)
	{
		vk::BufferImageCopy bufferCopyRegion{
			.bufferOffset = static_cast<vk::DeviceSize>(face) * faceSize,
			.imageSubresource = {
				.aspectMask = vk::ImageAspectFlagBits::eColor,
				.mipLevel = 0,
				.baseArrayLayer = face,
				.layerCount = 1,
			},
			.imageExtent = { .width = width, .height = height, .depth = 1 }
		};
		bufferCopyRegions.push_back(bufferCopyRegion);
	}

	vk::ImageCreateInfo imageCreateInfo{
		.flags = vk::ImageCreateFlagBits::eCubeCompatible,
		.imageType = vk::ImageType::e2D,
		.format = format,
		.extent = { .width = width, .height = height, .depth = 1 },
		.mipLevels = mipLevels,
		.arrayLayers = 6,
		.samples = vk::SampleCountFlagBits::e1,
		.tiling = vk::ImageTiling::eOptimal,
		.usage = usageFlags,
		.sharingMode = vk::SharingMode::eExclusive,
		.initialLayout = vk::ImageLayout::eUndefined,
	};
	imageCreateInfo.usage |= vk::ImageUsageFlagBits::eTransferDst;

	image = device.createImage(imageCreateInfo, nullptr);
	memReqs = image.getMemoryRequirements();
	memAllocInfo.allocationSize = memReqs.size;
	memAllocInfo.memoryTypeIndex = FindMemoryType(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
	deviceMemory = device.allocateMemory(memAllocInfo, nullptr);
	image.bindMemory(deviceMemory, 0);

	vk::ImageSubresourceRange subresourceRange{
		.aspectMask = vk::ImageAspectFlagBits::eColor,
		.baseMipLevel = 0,
		.levelCount = mipLevels,
		.layerCount = 6,
	};

	context.Submit(VulkanContext::OperationType::Graphics, [&](const vk::raii::CommandBuffer& copyCmd)
	{
		context.TransitionImageLayout(copyCmd, image, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, subresourceRange);
		copyCmd.copyBufferToImage(stagingBuffer, image, vk::ImageLayout::eTransferDstOptimal, bufferCopyRegions);
		context.TransitionImageLayout(copyCmd, image, vk::ImageLayout::eTransferDstOptimal, imageLayout, subresourceRange);
	});

	CreateSampler(device);

	vk::ImageViewCreateInfo viewCreateInfo{
		.image = image,
		.viewType = vk::ImageViewType::eCube,
		.format = format,
		.subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eColor, .baseMipLevel = 0, .levelCount = mipLevels, .baseArrayLayer = 0, .layerCount = 6 },
	};
	view = device.createImageView(viewCreateInfo);

	UpdateDescriptor();

	const std::string debugName = data.Name.empty() ? "Cubemap" : data.Name;
	SetDebugName(debugName);
}

TextureCube::TextureCube(const std::filesystem::path& filepath)
{
	KBR_TRACY_FUNCTION();

	auto extension = filepath.extension().string();
	std::ranges::transform(extension, extension.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	KBRAssert(extension == ".ktx2", "TextureCube::TextureCube - only KTX2 files are supported; KTX1 and raster sources are converted by the import pipeline: {}", filepath.string());

	ktxTexture2* ktxTex = nullptr;
	ktxResult result = KTX2Loader::Load(filepath, &ktxTex) ? KTX_SUCCESS : KTX_FILE_DATA_ERROR;
	KBRAssert(result == KTX_SUCCESS, "TextureCube::TextureCube - Failed to load KTX2 file: {}", filepath.string());

	KBRAssert(ktxTex != nullptr, "TextureCube::TextureCube - ktxTexture2 is null after loading KTX2 file: {}", filepath.string());

	usageFlags = vk::ImageUsageFlagBits::eSampled;
	imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

	auto& context = VulkanContext::Get();
	const auto& device = context.GetDevice();

	width = ktxTex->baseWidth;
	height = ktxTex->baseHeight;
	mipLevels = ktxTex->numLevels;
	format = static_cast<vk::Format>(ktxTex->vkFormat);

	ktx_uint8_t* ktxTextureData = ktxTex->pData;
	ktx_size_t ktxTextureSize = ktxTex->dataSize;

	// Create a host-visible staging buffer that contains the raw image data

	vk::BufferCreateInfo bufferCreateInfo{
		.size = ktxTextureSize,
		.usage = vk::BufferUsageFlagBits::eTransferSrc,
		.sharingMode = vk::SharingMode::eExclusive
	};
	vk::raii::Buffer stagingBuffer = device.createBuffer(bufferCreateInfo, nullptr);

	// Get memory requirements for the staging buffer (alignment, memory type bits)
	vk::MemoryRequirements memReqs = stagingBuffer.getMemoryRequirements();
	vk::MemoryAllocateInfo memAllocInfo{
		.allocationSize = memReqs.size,
		// Get memory type index for a host visible buffer
		.memoryTypeIndex = FindMemoryType(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent)
	};
	vk::raii::DeviceMemory stagingMemory = device.allocateMemory(memAllocInfo, nullptr);
	stagingBuffer.bindMemory(stagingMemory, 0);

	// Copy texture data into staging buffer
	uint8_t* data{ nullptr };
	data = static_cast<uint8_t*>(stagingMemory.mapMemory(0, memReqs.size));
	std::memcpy(data, ktxTextureData, ktxTextureSize);
	stagingMemory.unmapMemory();

	// Setup buffer copy regions for each mip level
	std::vector<vk::BufferImageCopy> bufferCopyRegions;

	for (uint32_t face = 0; face < 6; ++face)
	{
		for (uint32_t level = 0; level < mipLevels; level++)
		{
			ktx_size_t offset;
			KTX_error_code res = ktxTexture_GetImageOffset((ktxTexture*)ktxTex, level, 0, face, &offset);
			KBRAssert(res == KTX_SUCCESS, "TextureCube::TextureCube - Failed to get image offset for level {} face {}", level, face);
			vk::BufferImageCopy bufferCopyRegion{
				.bufferOffset = offset,
				.imageSubresource = {
					.aspectMask = vk::ImageAspectFlagBits::eColor,
					.mipLevel = level,
					.baseArrayLayer = face,
					.layerCount = 1,
				},
				.imageExtent = {
					.width = ktxTex->baseWidth >> level,
					.height = ktxTex->baseHeight >> level,
					.depth = 1
				}
			};
			bufferCopyRegions.push_back(bufferCopyRegion);
		}
	}

	// Create optimal tiled target image
	vk::ImageCreateInfo imageCreateInfo{
		.flags = vk::ImageCreateFlagBits::eCubeCompatible,
		.imageType = vk::ImageType::e2D,
		.format = format,
		.extent = {.width = width, .height = height, .depth = 1 },
		.mipLevels = mipLevels,
		.arrayLayers = 6,
		.samples = vk::SampleCountFlagBits::e1,
		.tiling = vk::ImageTiling::eOptimal,
		.usage = usageFlags,
		.sharingMode = vk::SharingMode::eExclusive,
		.initialLayout = vk::ImageLayout::eUndefined,
	};

	// Ensure that the TRANSFER_DST bit is set for staging
	if (!(imageCreateInfo.usage & vk::ImageUsageFlagBits::eTransferDst)) {
		imageCreateInfo.usage |= vk::ImageUsageFlagBits::eTransferDst;
	}

	image = device.createImage(imageCreateInfo, nullptr);
	memReqs = image.getMemoryRequirements();
	memAllocInfo.allocationSize = memReqs.size;
	memAllocInfo.memoryTypeIndex = FindMemoryType(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
	deviceMemory = device.allocateMemory(memAllocInfo, nullptr);
	image.bindMemory(deviceMemory, 0);

	vk::ImageSubresourceRange subresourceRange{
		.aspectMask = vk::ImageAspectFlagBits::eColor,
		.baseMipLevel = 0,
		.levelCount = mipLevels,
		.layerCount = 6,
	};

	context.Submit(VulkanContext::OperationType::Graphics, [&](const vk::raii::CommandBuffer& copyCmd)
	{
		// Image barrier for optimal image (target)
		// Optimal image will be used as destination for the copy
		context.TransitionImageLayout(
			copyCmd,
			image,
			vk::ImageLayout::eUndefined,
			vk::ImageLayout::eTransferDstOptimal,
			subresourceRange);

		// Copy mip levels from staging buffer
		copyCmd.copyBufferToImage(
			stagingBuffer,
			image,
			vk::ImageLayout::eTransferDstOptimal,
			bufferCopyRegions);

		// Change texture image layout to shader read after all mip levels have been copied
		context.TransitionImageLayout(
			copyCmd,
			image,
			vk::ImageLayout::eTransferDstOptimal,
			imageLayout,
			subresourceRange);
	});

	ktxTexture2_Destroy(ktxTex);

	// Create a default sampler
	CreateSampler(device);

	// Create image view
	// Textures are not directly accessed by the shaders and
	// are abstracted by image views containing additional
	// information and sub resource ranges
	vk::ImageViewCreateInfo viewCreateInfo{
		.image = image,
		.viewType = vk::ImageViewType::eCube,
		.format = format,
		.subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor, .baseMipLevel = 0, .levelCount = mipLevels, .baseArrayLayer = 0, .layerCount = 6 },
	};
	view = device.createImageView(viewCreateInfo);

	// Update descriptor image info member that can be used for setting up descriptor sets
	UpdateDescriptor();

	const std::string debugName = filepath.stem().string();
	SetDebugName(debugName);
}

TextureCube::~TextureCube() 
{
	Log::CoreTrace("Destroying TextureCube: {}", GetHandle());
}

Ref<TextureCube> TextureCube::FromData(const CubemapData& data)
{
	return CreateRef<TextureCube>(data);
}

Ref<TextureCube> TextureCube::FromFile(const std::filesystem::path& filepath)
{
	return CreateRef<TextureCube>(filepath);
}

}