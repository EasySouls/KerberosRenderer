#pragma once

#include "Vulkan.hpp"
#include "Assets/Asset.hpp"

#include <cstdint>

namespace Kerberos
{
	enum class ImageFormat
	{
		None = 0,
		R8,
		RGB8,
		RGBA8,
		RGBA32F
	};

	struct TextureSpecification
	{
		uint32_t Width = 1;
		uint32_t Height = 1;
		ImageFormat Format = ImageFormat::RGBA8;
		bool GenerateMips = false;
	};

	class Texture : public Asset
	{
	public:
		void UpdateDescriptor();

		vk::raii::Sampler& GetSampler();
		vk::raii::ImageView& GetImageView();
		vk::DescriptorImageInfo& GetDescriptorInfo();

	protected:
		Texture() = default;

		void CreateSampler(const vk::raii::Device& device, vk::Filter filter = vk::Filter::eLinear);
		void SetDebugName(const std::string& debugName) const;
	
	protected:
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

		TextureSpecification m_Specification;
	};

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
}
