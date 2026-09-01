#include "TextureCube.hpp"
#include "VulkanContext.hpp"

#include "ktx.h"
#include "Utils.hpp"
#include "Utils/KtxConversion.hpp"
#include "KTX2Loader.hpp"

import Kerberos;

namespace Kerberos {

static ktxResult LoadKTXFile(const std::filesystem::path& filepath, ktxTexture2** target)
{
	const ktxResult result = ktxTexture2_CreateFromNamedFile(filepath.string().c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, target);
	return result;
}

TextureCube::TextureCube(const CubemapData& data)
{
	KBRAssert(false, "TextureCube::TextureCube - CubemapData constructor is not implemented yet");
}

TextureCube::TextureCube(const std::filesystem::path& filepath)
{
	auto extension = filepath.extension().string();
	std::ranges::transform(extension, extension.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	KBRAssert(extension == ".ktx" || extension == ".ktx2", "TextureCube::TextureCube - only KTX files are supported in this constructor");

	ktxTexture2* ktxTex = nullptr;

	// TODO: Remove the conversion from here, it should happen in an importer
	if (extension == ".ktx")
	{
		auto newFilePath = filepath;
		newFilePath.replace_extension(".ktx2");

		if (!std::filesystem::exists(newFilePath))
		{
			const Process::ProcessResult result = KtxConversion::ConvertKtxToKtx2(filepath);

			KBRAssert(
				result.Succeeded,
				"TextureCube::TextureCube - failed to convert KTX to KTX2 for file: {} (started: {}, exit code: {}, error code: {}, error: {})",
				filepath.string(),
				result.Started,
				result.ExitCode,
				result.ErrorCode,
				result.ErrorMessage);
		}
		
		ktxResult result = LoadKTXFile(newFilePath, &ktxTex);
		KBRAssert(result == KTX_SUCCESS, "TextureCube::TextureCube - Failed to load KTX file after converting it: {}", newFilePath.string());
	}
	else
	{
		ktxResult result = KTX2Loader::Load(filepath, &ktxTex) ? KTX_SUCCESS : KTX_FILE_DATA_ERROR;
		KBRAssert(result == KTX_SUCCESS, "TextureCube::TextureCube - Failed to load KTX file: {}", filepath.string());
	}

	KBRAssert(ktxTex != nullptr, "TextureCube::TextureCube - ktxTexture2 is null after loading KTX file: {}", filepath.string());

	vk::ImageUsageFlags  imageUsageFlags = vk::ImageUsageFlagBits::eSampled;
	vk::ImageLayout      imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

	auto& context = VulkanContext::Get();
	const auto& device = context.GetDevice();

	width = ktxTex->baseWidth;
	height = ktxTex->baseHeight;
	mipLevels = ktxTex->numLevels;
	const vk::Format format = static_cast<vk::Format>(ktxTex->vkFormat);

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
		.usage = imageUsageFlags,
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
		this->imageLayout = imageLayout;
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