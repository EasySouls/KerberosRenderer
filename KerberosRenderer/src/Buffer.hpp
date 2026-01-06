#pragma once

#include "Vulkan.hpp"

void CreateBuffer(
	const vk::raii::Device& device,
	const vk::PhysicalDevice& physicalDevice,
	vk::DeviceSize size,
	vk::BufferUsageFlags usage,
	vk::MemoryPropertyFlags properties,
	vk::raii::Buffer& buffer,
	vk::raii::DeviceMemory& bufferMemory
);

void CopyBuffer(
	const vk::raii::Device& device,
	const vk::raii::CommandPool& commandPool,
	const vk::raii::Queue& graphicsQueue,
	const vk::raii::Buffer& srcBuffer,
	const vk::raii::Buffer& dstBuffer,
	vk::DeviceSize size,
	const vk::raii::Semaphore* waitSemaphore = nullptr,
	const vk::raii::Semaphore* signalSemaphore = nullptr
);