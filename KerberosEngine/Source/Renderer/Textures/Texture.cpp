#include "kbrpch.hpp"
#include "Texture.hpp"

#include "Utils.hpp"

namespace Kerberos
{
	void Texture::UpdateDescriptor()
	{
		descriptor.sampler = sampler;
		descriptor.imageView = view;
		descriptor.imageLayout = imageLayout;
	}

	vk::raii::Sampler& Texture::GetSampler()
	{
		return sampler;
	}

	vk::raii::ImageView& Texture::GetImageView()
	{
		return view;
	}

	vk::DescriptorImageInfo& Texture::GetDescriptorInfo() 
	{
		return descriptor;
	}

	void Texture::CreateSampler(const vk::raii::Device& device, const vk::Filter filter)
	{
		const vk::SamplerCreateInfo samplerCreateInfo{
			.magFilter = filter,
			.minFilter = filter,
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
	}

	void Texture::SetDebugName(const std::string& debugName) const
	{
		const auto& context = VulkanContext::Get();
		context.SetObjectDebugName(image, debugName + "_Image");
		context.SetObjectDebugName(view, debugName + "_ImageView");
		context.SetObjectDebugName(sampler, debugName + "_Sampler");
		context.SetObjectDebugName(deviceMemory, debugName + "_ImageMemory");
	}

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
}
