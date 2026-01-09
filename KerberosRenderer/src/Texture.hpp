#pragma once

#include "Vulkan.hpp"

void CreateImage(
	const vk::raii::Device& device,
	const vk::PhysicalDevice& physicalDevice,
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