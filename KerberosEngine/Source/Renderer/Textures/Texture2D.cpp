#include "kbrpch.hpp"
#include "Texture2D.hpp"

#include <ktx.h>

#include "Utils.hpp"
#include "VulkanContext.hpp"

namespace Kerberos
{
	static ktxResult LoadKTXFile(const std::filesystem::path& filepath, ktxTexture2** target)
	{
		const ktxResult result = ktxTexture2_CreateFromNamedFile(filepath.string().c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, target);
		return result;
	}

	Texture2D::Texture2D(const TextureSpecification& spec, const Buffer& buffer)
	{
		m_Specification = spec;

		auto& context = VulkanContext::Get();
		const auto& device = context.GetDevice();

		width = spec.Width;
		height = spec.Height;
		mipLevels = 1; // TODO: Parameterize
		constexpr vk::ImageUsageFlags imageUsageFlags = vk::ImageUsageFlagBits::eSampled; // TODO: Parameterize
		constexpr vk::Format format = vk::Format::eR8G8B8A8Unorm; // TODO: Parameterize

		// Use a separate command buffer for texture loading
		vk::raii::CommandBuffer copyCmd = context.BeginSingleTimeCommands();

		// Create a host-visible staging buffer that contains the raw image data
		vk::BufferCreateInfo bufferCreateInfo{
			.size = buffer.Size,
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
		std::memcpy(data, buffer.Data, buffer.Size);
		stagingMemory.unmapMemory();

		// Setup buffer copy regions for each mip level
		std::vector<vk::BufferImageCopy> bufferCopyRegions;

		for (uint32_t i = 0; i < mipLevels; i++) {
			VkDeviceSize offset = 0; // TODO: Calculate offset when mipmapping is enabled
			vk::BufferImageCopy bufferCopyRegion{
				.bufferOffset = offset,
				.imageSubresource = {
					.aspectMask = vk::ImageAspectFlagBits::eColor,
					.mipLevel = i,
					.baseArrayLayer = 0,
					.layerCount = 1,
				},
				.imageExtent = {
					.width = std::max(1u, width >> i),
					.height = std::max(1u, height >> i),
					.depth = 1
				}
			};
			bufferCopyRegions.push_back(bufferCopyRegion);
		}

		// Create optimal tiled target image
		vk::ImageCreateInfo imageCreateInfo{
			.imageType = vk::ImageType::e2D,
			.format = format,
			.extent = {.width = width, .height = height, .depth = 1 },
			.mipLevels = mipLevels,
			.arrayLayers = 1,
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
			.layerCount = 1,
		};

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

		context.EndSingleTimeCommands(copyCmd);

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

		// Update descriptor image info member that can be used for setting up descriptor sets
		UpdateDescriptor();

		const std::string debugName = "Texture from buffer"; // TODO: Have an option to set debug names for textures from a buffer
		SetDebugName(debugName);
	}

	Texture2D::Texture2D(const std::filesystem::path& filepath) 
	{
		const auto extension = filepath.extension();
		KBR_CORE_ASSERT(extension == ".ktx" || extension == ".ktx2", "Texture2D::Texture2D - only KTX files are supported in this constructor");
		
		vk::ImageUsageFlags  imageUsageFlags = vk::ImageUsageFlagBits::eSampled;
		vk::ImageLayout      imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

		ktxTexture2* ktxTex;
		ktxResult result = LoadKTXFile(filepath, &ktxTex);
		assert(result == KTX_SUCCESS);

		auto& context = VulkanContext::Get();
		const auto& device = context.GetDevice();

		width = ktxTex->baseWidth;
		height = ktxTex->baseHeight;
		mipLevels = ktxTex->numLevels;

		const vk::Format format = static_cast<vk::Format>(ktxTex->vkFormat);

		ktx_uint8_t* ktxTextureData = ktxTex->pData;
		ktx_size_t ktxTextureSize = ktxTex->dataSize;

		// Get device properties for the requested texture format
		vk::FormatProperties formatProperties = context.GetFormatProperties(format);

		// Use a separate command buffer for texture loading
		vk::raii::CommandBuffer copyCmd = context.BeginSingleTimeCommands();

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

		for (uint32_t i = 0; i < mipLevels; i++) {
			ktx_size_t offset;
			KTX_error_code res = ktxTexture_GetImageOffset(reinterpret_cast<ktxTexture*>(ktxTex), i, 0, 0, &offset);
			KBR_CORE_ASSERT(res == KTX_SUCCESS);
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
			.imageType = vk::ImageType::e2D,
			.format = format,
			.extent = {.width = width, .height = height, .depth = 1 },
			.mipLevels = mipLevels,
			.arrayLayers = 1,
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
			.layerCount = 1,
		};

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

		context.EndSingleTimeCommands(copyCmd);

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

		// Update descriptor image info member that can be used for setting up descriptor sets
		UpdateDescriptor();

		const std::string debugName = filepath.stem().string();
		SetDebugName(debugName);
	}

	Texture2D::~Texture2D() 
	{
		KBR_CORE_TRACE("Destroying Texture2D: {}", GetHandle());
	}
}
