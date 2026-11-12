#include "Texture2D.hpp"

#include <ktx.h>

#include "Utils.hpp"
#include "KTX2Loader.hpp"
#include "KTX2FormatSelector.hpp"
#include "MipmapGenerator.hpp"
#include "VulkanContext.hpp"
#include "Profiling/Profilers.hpp"

import Kerberos;

namespace
{
	vk::Format GetImGuiDisplayFormat(const vk::Format format)
	{
		switch (format)
		{
			case vk::Format::eR8G8B8A8Srgb: return vk::Format::eR8G8B8A8Unorm;
			case vk::Format::eB8G8R8A8Srgb: return vk::Format::eB8G8R8A8Unorm;
			case vk::Format::eR8G8B8Srgb: return vk::Format::eR8G8B8Unorm;
			case vk::Format::eA8B8G8R8SrgbPack32: return vk::Format::eA8B8G8R8UnormPack32;
			case vk::Format::eBc1RgbaSrgbBlock: return vk::Format::eBc1RgbaUnormBlock;
			case vk::Format::eBc3SrgbBlock: return vk::Format::eBc3UnormBlock;
			case vk::Format::eBc7SrgbBlock: return vk::Format::eBc7UnormBlock;
			default: return format;
		}
	}
}

namespace Kerberos {

Texture2D::Texture2D(const TextureSpecification& spec, const Buffer& buffer)
{
	KBR_TRACY_FUNCTION();

	m_Specification = spec;

	auto& context = VulkanContext::Get();
	const auto& device = context.GetDevice();

	width = spec.Width;
	height = spec.Height;
	format = KTX2FormatSelector::Select(spec.Format);

	const uint32_t fullMipLevels = MipmapGenerator::CalculateMipLevelCount(width, height);
	const bool generateMipmaps = fullMipLevels > 1 && MipmapGenerator::CanGenerate(format);
	mipLevels = generateMipmaps ? fullMipLevels : 1;

	imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal; // TODO: Parameterize
	usageFlags = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
	if (generateMipmaps)
		usageFlags |= vk::ImageUsageFlagBits::eTransferSrc;

	// Create a host-visible staging buffer that contains the raw image data
	vk::BufferCreateInfo bufferCreateInfo{
		.size = buffer.Size,
		.usage = vk::BufferUsageFlagBits::eTransferSrc,
		.sharingMode = vk::SharingMode::eExclusive
	};
	vk::raii::Buffer stagingBuffer = device.createBuffer(bufferCreateInfo, nullptr);

	// Get memory requirements for the staging buffer (alignment, memory type bits)
	vk::MemoryRequirements memReqs = stagingBuffer.getMemoryRequirements();

	if (memReqs.size == 0) 
	{
		Log::CoreError("Texture2D::Texture2D - failed to create staging buffer for texture upload, memory requirements size is 0");
		return;
	}

	KBRAssert(memReqs.size >= buffer.Size, "Texture2D::Texture2D - staging buffer memory requirements size ({}) is smaller than buffer size ({}), cannot copy data", memReqs.size, buffer.Size);

	vk::MemoryAllocateInfo memAllocInfo{
		.allocationSize = memReqs.size,
		// Get memory type index for a host visible buffer
		.memoryTypeIndex = FindMemoryType(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent)
	};
	vk::raii::DeviceMemory stagingMemory = device.allocateMemory(memAllocInfo, nullptr);
	stagingBuffer.bindMemory(stagingMemory, 0);

	// Copy texture data into staging buffer
	void* data = stagingMemory.mapMemory(0, memReqs.size);
	std::memcpy(data, buffer.Data, buffer.Size);
	stagingMemory.unmapMemory();

	// The buffer contains the base level only, enerated levels are filled below.
	std::vector<vk::BufferImageCopy> bufferCopyRegions;
	bufferCopyRegions.push_back(vk::BufferImageCopy{
		.bufferOffset = 0,
		.imageSubresource = {
			.aspectMask = vk::ImageAspectFlagBits::eColor,
			.mipLevel = 0,
			.baseArrayLayer = 0,
			.layerCount = 1,
		},
		.imageExtent = {
			.width = width,
			.height = height,
			.depth = 1
		}
	});

	// Create optimal tiled target image
	vk::ImageCreateInfo imageCreateInfo{
		.flags = vk::ImageCreateFlagBits::eMutableFormat,
		.imageType = vk::ImageType::e2D,
		.format = format,
		.extent = {.width = width, .height = height, .depth = 1 },
		.mipLevels = mipLevels,
		.arrayLayers = 1,
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
		.layerCount = 1,
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

		// Copy the base level from the staging buffer
		copyCmd.copyBufferToImage(
			stagingBuffer,
			image,
			vk::ImageLayout::eTransferDstOptimal,
			bufferCopyRegions);

		if (generateMipmaps)
		{
			MipmapGenerator::Generate(copyCmd, image, width, height, mipLevels);
		}

		// Change texture image layout to shader read after all mip levels are ready
		this->imageLayout = imageLayout;
		context.TransitionImageLayout(
			copyCmd,
			image,
			generateMipmaps ? vk::ImageLayout::eTransferSrcOptimal : vk::ImageLayout::eTransferDstOptimal,
			imageLayout,
			subresourceRange);
	});

	CreateSampler(device);

	vk::ImageViewCreateInfo viewCreateInfo{
		.image = image,
		.viewType = vk::ImageViewType::e2D,
		.format = format,
		.subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor, .baseMipLevel = 0, .levelCount = mipLevels, .baseArrayLayer = 0, .layerCount = 1 },
	};
	view = device.createImageView(viewCreateInfo);
	CreateImGuiDisplayView(device, format);

	// Update descriptor image info member that can be used for setting up descriptor sets
	UpdateDescriptor();

	const std::string debugName = "Texture from buffer"; // TODO: Have an option to set debug names for textures from a buffer
	SetDebugName(debugName);
}

Texture2D::Texture2D(const std::filesystem::path& filepath) 
{
	KBR_TRACY_FUNCTION();

	auto extension = filepath.extension().string();
	std::ranges::transform(extension, extension.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	KBRAssert(extension == ".ktx2", "Texture2D::Texture2D - only KTX2 files are supported; KTX1 and raster sources are converted by the import pipeline: {}", filepath.string());

	usageFlags = vk::ImageUsageFlagBits::eSampled;
	imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

	ktxTexture2* ktxTex = nullptr;
	ktxResult result = KTX2Loader::Load(filepath, &ktxTex) ? KTX_SUCCESS : KTX_FILE_DATA_ERROR;
	KBRAssert(result == KTX_SUCCESS, "Texture2D::Texture2D - Failed to load KTX2 file: {}", filepath.string());

	KBRAssert(ktxTex != nullptr, "Texture2D::Texture2D - ktxTexture2 pointer is null after loading KTX2 file: {}", filepath.string());

	auto& context = VulkanContext::Get();
	const auto& device = context.GetDevice();

	width = ktxTex->baseWidth;
	height = ktxTex->baseHeight;
	format = KTX2Loader::ResolveVulkanFormat(ktxTex);

	const uint32_t ktxLevels = std::max(1u, static_cast<uint32_t>(ktxTex->numLevels));
	const uint32_t fullMipLevels = MipmapGenerator::CalculateMipLevelCount(width, height);
	const bool generateMipmaps = ktxLevels <= 1 && fullMipLevels > 1 && MipmapGenerator::CanGenerate(format);
	mipLevels = generateMipmaps ? fullMipLevels : ktxLevels;
	const uint32_t uploadedLevels = std::min(ktxLevels, mipLevels);

	// 16-bit color artifacts are linearized on import; remember that they were
	// authored as sRGB so editor validation does not flag them as data.
	unsigned int sourceSrgbLength = 0;
	void* sourceSrgbValue = nullptr;
	if (ktxHashList_FindValue(&ktxTex->kvDataHead, "KBR_SourceSrgb",
			&sourceSrgbLength, &sourceSrgbValue) == KTX_SUCCESS && sourceSrgbLength > 0)
	{
		m_Specification.SourceSrgb = true;
	}

	usageFlags = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
	if (generateMipmaps)
		usageFlags |= vk::ImageUsageFlagBits::eTransferSrc;

	ktx_uint8_t* ktxTextureData = ktxTex->pData;
	ktx_size_t ktxTextureSize = ktxTex->dataSize;

	// Get device properties for the requested texture format
	//vk::FormatProperties formatProperties = context.GetFormatProperties(format);

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

	for (uint32_t i = 0; i < uploadedLevels; i++) {
		ktx_size_t offset;
		KTX_error_code res = ktxTexture_GetImageOffset(reinterpret_cast<ktxTexture*>(ktxTex), i, 0, 0, &offset);
		KBRAssert(res == KTX_SUCCESS, "Texture2D::Texture2D - Failed to get image offset for mip level {}", i);
		vk::BufferImageCopy bufferCopyRegion{
			.bufferOffset = offset,
			.imageSubresource = {
				.aspectMask = vk::ImageAspectFlagBits::eColor,
				.mipLevel = i,
				.baseArrayLayer = 0,
				.layerCount = 1,
			},
			.imageExtent = {
				.width = std::max(1u, ktxTex->baseWidth >> i),
				.height = std::max(1u, ktxTex->baseHeight >> i),
				.depth = 1
			}
		};
		bufferCopyRegions.push_back(bufferCopyRegion);
	}

	// Create optimal tiled target image
	vk::ImageCreateInfo imageCreateInfo{
		.flags = vk::ImageCreateFlagBits::eMutableFormat,
		.imageType = vk::ImageType::e2D,
		.format = format,
		.extent = {.width = width, .height = height, .depth = 1 },
		.mipLevels = mipLevels,
		.arrayLayers = 1,
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
		.layerCount = 1,
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

		// Copy the authored mip levels from the staging buffer
		copyCmd.copyBufferToImage(
			stagingBuffer,
			image,
			vk::ImageLayout::eTransferDstOptimal,
			bufferCopyRegions);

		if (generateMipmaps)
		{
			MipmapGenerator::Generate(copyCmd, image, width, height, mipLevels);
		}

		// Change texture image layout to shader read after all mip levels are ready
		context.TransitionImageLayout(
			copyCmd,
			image,
			generateMipmaps ? vk::ImageLayout::eTransferSrcOptimal : vk::ImageLayout::eTransferDstOptimal,
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
		.viewType = vk::ImageViewType::e2D,
		.format = format,
		.subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor, .baseMipLevel = 0, .levelCount = mipLevels, .baseArrayLayer = 0, .layerCount = 1 },
	};
	view = device.createImageView(viewCreateInfo);
	CreateImGuiDisplayView(device, format);

	// Update descriptor image info member that can be used for setting up descriptor sets
	UpdateDescriptor();

	const std::string debugName = filepath.stem().string();
	SetDebugName(debugName);
}

void Texture2D::CreateImGuiDisplayView(const vk::raii::Device& device, const vk::Format imageFormat)
{
	const vk::Format displayFormat = GetImGuiDisplayFormat(imageFormat);
	if (displayFormat == imageFormat)
		return;

	const vk::ImageViewCreateInfo viewCreateInfo{
		.image = image,
		.viewType = vk::ImageViewType::e2D,
		.format = displayFormat,
		.subresourceRange = {
			.aspectMask = vk::ImageAspectFlagBits::eColor,
			.baseMipLevel = 0,
			.levelCount = mipLevels,
			.baseArrayLayer = 0,
			.layerCount = 1
		}
	};

	m_ImGuiImageView = device.createImageView(viewCreateInfo);
	m_HasImGuiImageView = true;
}

const vk::raii::ImageView& Texture2D::GetImGuiImageView() const
{
	return m_HasImGuiImageView ? m_ImGuiImageView : view;
}

bool Texture2D::IsSrgb() const
{
	// A 16-bit color source is linearized during import because Vulkan has no 16-bit UNORM+sRGB format.
	// SourceSrgb preserves that authoring intent for editor validation and previews.
	return m_Specification.SourceSrgb || GetImGuiDisplayFormat(format) != format;
}

Texture2D::~Texture2D() 
{
	Log::CoreTrace("Destroying Texture2D: {}", GetHandle());
}

Ref<Texture2D> Texture2D::FromBuffer(const TextureSpecification& spec, const Buffer& buffer) 
{
	return CreateRef<Texture2D>(spec, buffer);
}

Ref<Texture2D> Texture2D::FromFile(const std::filesystem::path& filepath)
{
	return CreateRef<Texture2D>(filepath);
}

}