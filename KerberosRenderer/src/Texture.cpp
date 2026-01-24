#include "Texture.hpp"

#include "Utils.hpp"

void CreateImage(
	const vk::raii::Device& device,
	const uint32_t width, 
	const uint32_t height,
	const uint32_t mipLevels,
	const vk::SampleCountFlagBits numSamples,
	const vk::Format format,
	const vk::ImageTiling tiling,
	const vk::ImageUsageFlags usage,
	const vk::MemoryPropertyFlags properties, 
	vk::raii::Image& image, 
	vk::raii::DeviceMemory& imageMemory) 
{
	const vk::ImageCreateInfo imageInfo{ 
		.imageType = vk::ImageType::e2D, 
		.format = format,
		.extent = {
			.width = width, 
			.height = height,
			.depth = 1
	    }, 
		.mipLevels = mipLevels, 
		.arrayLayers = 1,
		.samples = numSamples, 
		.tiling = tiling,
		.usage = usage, 
		.sharingMode = vk::SharingMode::eExclusive 
	};

	image = vk::raii::Image(device, imageInfo);

	const vk::MemoryRequirements memRequirements = image.getMemoryRequirements();
	const vk::MemoryAllocateInfo allocInfo{ 
		.allocationSize = memRequirements.size,
		.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, properties) 
	};

	imageMemory = vk::raii::DeviceMemory(device, allocInfo);
	image.bindMemory(imageMemory, 0);
}

vk::raii::ImageView CreateImageView(
	const vk::raii::Device& device, 
	const vk::raii::Image& image, 
	const vk::Format format,
	const vk::ImageAspectFlagBits aspectFlags,
	const uint32_t mipLevels
) 
{
	const vk::ImageViewCreateInfo viewInfo{
		.flags = {},
		.image = image, 
		.viewType = vk::ImageViewType::e2D,
		.format = format, 
		.subresourceRange = {
			.aspectMask = aspectFlags,
			.baseMipLevel = 0,
			.levelCount = mipLevels,
			.baseArrayLayer = 0,
			.layerCount = 1 
	    }
	};

	return { device, viewInfo };
}

namespace kbr
{
	void Texture::UpdateDescriptor() 
	{
		descriptor.sampler = sampler;
		descriptor.imageView = view;
		descriptor.imageLayout = imageLayout;
	}

	ktxResult Texture::LoadKTXFile(const std::filesystem::path& filepath, ktxTexture** target) 
	{
		ktxResult result = KTX_SUCCESS;
		result = ktxTexture_CreateFromNamedFile(filepath.string().c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, target);
		return result;
	}

	void Texture2D::LoadFromFile(const std::filesystem::path& filepath, vk::Format format,
		vk::ImageUsageFlags imageUsageFlags, vk::ImageLayout imageLayout) 
	{
		ktxTexture* ktxTexture;
		ktxResult result = LoadKTXFile(filepath, &ktxTexture);
		assert(result == KTX_SUCCESS);

		auto& context = VulkanContext::Get();
		const auto& device = context.GetDevice();

		width = ktxTexture->baseWidth;
		height = ktxTexture->baseHeight;
		mipLevels = ktxTexture->numLevels;

		ktx_uint8_t* ktxTextureData = ktxTexture_GetData(ktxTexture);
		ktx_size_t ktxTextureSize = ktxTexture_GetSize(ktxTexture);

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
			KTX_error_code result = ktxTexture_GetImageOffset(ktxTexture, i, 0, 0, &offset);
			assert(result == KTX_SUCCESS);
			vk::BufferImageCopy bufferCopyRegion{
				.bufferOffset = offset,
				.imageSubresource = {
					.aspectMask = vk::ImageAspectFlagBits::eColor,
					.mipLevel = i,
					.baseArrayLayer = 0,
					.layerCount = 1,
				},
				.imageExtent = {
					.width = std::max(1u, ktxTexture->baseWidth >> i),
					.height = std::max(1u, ktxTexture->baseHeight >> i),
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

		ktxTexture_Destroy(ktxTexture);

		// Create a default sampler
		vk::SamplerCreateInfo samplerCreateInfo{
			.magFilter = vk::Filter::eLinear,
			.minFilter = vk::Filter::eLinear,
			.mipmapMode = vk::SamplerMipmapMode::eLinear,
			.addressModeU = vk::SamplerAddressMode::eRepeat,
			.addressModeV = vk::SamplerAddressMode::eRepeat,
			.addressModeW = vk::SamplerAddressMode::eRepeat,
			.mipLodBias = 0.0f,
			.anisotropyEnable = vk::True,
			.maxAnisotropy = 16.0f,
			.compareOp = vk::CompareOp::eNever,
			.minLod = 0.0f,
			.maxLod = static_cast<float>(mipLevels),
			.borderColor = vk::BorderColor::eFloatOpaqueWhite
		};
		sampler = device.createSampler(samplerCreateInfo);

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
	}

	void Texture2D::FromBuffer(void* buffer, vk::DeviceSize bufferSize, vk::Format format, uint32_t texWidth,
		uint32_t texHeight, vk::Filter filter, vk::ImageUsageFlags imageUsageFlags, vk::ImageLayout imageLayout) 
	{
		throw std::runtime_error("Texture2D::FromBuffer not implemented yet");
	}

	void Texture2DArray::LoadFromFile(const std::filesystem::path& filepath, vk::Format format,
		vk::ImageUsageFlags imageUsageFlags, vk::ImageLayout imageLayout) 
	{
		throw std::runtime_error("Texture2DArray::LoadFromFile not implemented yet");
	}

	void TextureCubeMap::LoadFromFile(const std::filesystem::path& filepath, vk::Format format,
		vk::ImageUsageFlags imageUsageFlags, vk::ImageLayout imageLayout) 
	{
		ktxTexture* ktxTexture;
		ktxResult result = LoadKTXFile(filepath, &ktxTexture);
		assert(result == KTX_SUCCESS);

		auto& context = VulkanContext::Get();
		const auto& device = context.GetDevice();

		width = ktxTexture->baseWidth;
		height = ktxTexture->baseHeight;
		mipLevels = ktxTexture->numLevels;

		ktx_uint8_t* ktxTextureData = ktxTexture_GetData(ktxTexture);
		ktx_size_t ktxTextureSize = ktxTexture_GetSize(ktxTexture);

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

		for (uint32_t face = 0; face < 6; ++face) 
		{
			for (uint32_t level = 0; level < mipLevels; level++) 
			{
				ktx_size_t offset;
				KTX_error_code result = ktxTexture_GetImageOffset(ktxTexture, level, 0, face, &offset);
				assert(result == KTX_SUCCESS);
				vk::BufferImageCopy bufferCopyRegion{
					.bufferOffset = offset,
					.imageSubresource = {
						.aspectMask = vk::ImageAspectFlagBits::eColor,
						.mipLevel = level,
						.baseArrayLayer = face,
						.layerCount = 1,
					},
					.imageExtent = {
						.width = ktxTexture->baseWidth >> level,
						.height = ktxTexture->baseHeight >> level,
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

		ktxTexture_Destroy(ktxTexture);

		// Create a default sampler
		vk::SamplerCreateInfo samplerCreateInfo{
			.magFilter = vk::Filter::eLinear,
			.minFilter = vk::Filter::eLinear,
			.mipmapMode = vk::SamplerMipmapMode::eLinear,
			.addressModeU = vk::SamplerAddressMode::eClampToEdge,
			.addressModeV = vk::SamplerAddressMode::eClampToEdge,
			.addressModeW = vk::SamplerAddressMode::eClampToEdge,
			.mipLodBias = 0.0f,
			.anisotropyEnable = vk::True,
			.maxAnisotropy = 16.0f,
			.compareOp = vk::CompareOp::eNever,
			.minLod = 0.0f,
			.maxLod = static_cast<float>(mipLevels),
			.borderColor = vk::BorderColor::eFloatOpaqueWhite
		};
		sampler = device.createSampler(samplerCreateInfo);

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
	}
}
