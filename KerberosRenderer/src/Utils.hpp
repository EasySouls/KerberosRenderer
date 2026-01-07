#pragma once

#include "Vulkan.hpp"

inline uint32_t FindMemoryType(const vk::PhysicalDevice& physicalDevice, const uint32_t typeFilter, const vk::MemoryPropertyFlags properties)
{
	const vk::PhysicalDeviceMemoryProperties memProperties = physicalDevice.getMemoryProperties();

	for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i)
	{
		if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
		{
			return i;
		}
	}

	throw std::runtime_error("Failed to find suitable memory type!");
}

inline vk::Format FindSupportedFormat(
	const vk::PhysicalDevice& physicalDevice,
	const std::vector<vk::Format>& candidates,
	const vk::ImageTiling tiling,
	const vk::FormatFeatureFlags features)
{
	for (const vk::Format format : candidates)
	{
		const vk::FormatProperties props = physicalDevice.getFormatProperties(format);
		if (tiling == vk::ImageTiling::eLinear && (props.linearTilingFeatures & features) == features)
		{
			return format;
		}
		if (tiling == vk::ImageTiling::eOptimal && (props.optimalTilingFeatures & features) == features)
		{
			return format;
		}
	}
	throw std::runtime_error("Failed to find supported format!");
}