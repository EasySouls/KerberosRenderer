#pragma once

#include "Vulkan.hpp"

#include <filesystem>

namespace Kerberos
{
	void CreateImage(
		const vk::raii::Device& device,
		uint32_t width, 
		uint32_t height, 
		uint32_t mipLevels,
		vk::SampleCountFlagBits numSamples,
		vk::Format format, 
		vk::ImageTiling tiling, 
		vk::ImageUsageFlags usage, 
		vk::MemoryPropertyFlags properties, 
		vk::raii::Image& image, 
		vk::raii::DeviceMemory& imageMemory
	);
	
	vk::raii::ImageView CreateImageView(
		const vk::raii::Device& device, 
		const vk::raii::Image& image, 
		vk::Format format, 
		vk::ImageAspectFlagBits aspectFlags,
		uint32_t mipLevels
	);

	class Texture
	{
	public:
		vk::raii::Image         image = nullptr;
		vk::ImageLayout         imageLayout{};
		vk::raii::DeviceMemory  deviceMemory = nullptr;
		vk::raii::ImageView     view = nullptr;
		uint32_t                width = 0;
		uint32_t                height = 0;
		uint32_t                mipLevels = 0;
		uint32_t                layerCount = 0;
		vk::DescriptorImageInfo descriptor{};
		vk::raii::Sampler       sampler = nullptr;

		void      UpdateDescriptor();

		vk::raii::Sampler& GetSampler();
		vk::raii::ImageView& GetImageView();

	protected:
		void CreateSampler(const vk::raii::Device& device, vk::Filter filter = vk::Filter::eLinear);
		void SetDebugName(const std::string& debugName) const;
	};

	class Texture2D : public Texture
	{
	public:
		void LoadFromFile(
			const std::filesystem::path& filepath,
			vk::Format           format,
			vk::ImageUsageFlags  imageUsageFlags = vk::ImageUsageFlagBits::eSampled,
			vk::ImageLayout      imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal);

		void FromBuffer(
			void* buffer,
			vk::DeviceSize       bufferSize,
			vk::Format           format,
			uint32_t             texWidth,
			uint32_t             texHeight,
			vk::Filter           filter = vk::Filter::eLinear,
			vk::ImageUsageFlags  imageUsageFlags = vk::ImageUsageFlagBits::eSampled,
			vk::ImageLayout      imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal);
	};

	class Texture2DArray : public Texture
	{
	public:
		void LoadFromFile(
			const std::filesystem::path& filepath,
			vk::Format           format,
			vk::ImageUsageFlags  imageUsageFlags = vk::ImageUsageFlagBits::eSampled,
			vk::ImageLayout      imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal);
	};

	class TextureCube : public Texture
	{
	public:
		void LoadFromFile(
			const std::filesystem::path& filepath,
			vk::Format           format,
			vk::ImageUsageFlags  imageUsageFlags = vk::ImageUsageFlagBits::eSampled,
			vk::ImageLayout      imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal);
	};
}
