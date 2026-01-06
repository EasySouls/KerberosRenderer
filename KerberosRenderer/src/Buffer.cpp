#include "Buffer.hpp"

#include "Utils.hpp"

void CreateBuffer(
	const vk::raii::Device& device,
	const vk::PhysicalDevice& physicalDevice,
	const vk::DeviceSize size,
	const vk::BufferUsageFlags usage,
	const vk::MemoryPropertyFlags properties,
	vk::raii::Buffer& buffer,
	vk::raii::DeviceMemory& bufferMemory
) 
{
	const vk::BufferCreateInfo bufferInfo{
		.size = size,
		.usage = usage,
		.sharingMode = vk::SharingMode::eExclusive
	};
	buffer = vk::raii::Buffer(device, bufferInfo);

	const vk::MemoryRequirements memRequirements = buffer.getMemoryRequirements();
	const vk::MemoryAllocateInfo allocInfo{
		.allocationSize = memRequirements.size,
		.memoryTypeIndex = FindMemoryType(physicalDevice, memRequirements.memoryTypeBits, properties)
	};
	bufferMemory = vk::raii::DeviceMemory(device, allocInfo);
	buffer.bindMemory(*bufferMemory, 0);
}

void CopyBuffer(
	const vk::raii::Device& device, 
	const vk::raii::CommandPool& commandPool,
	const vk::raii::Queue& graphicsQueue, 
	const vk::raii::Buffer& srcBuffer, 
	const vk::raii::Buffer& dstBuffer,
	const vk::DeviceSize size,
	const vk::raii::Semaphore* waitSemaphore,
	const vk::raii::Semaphore* signalSemaphore
) 
{
	const vk::CommandBufferAllocateInfo allocInfo{
		.commandPool = *commandPool,
		.level = vk::CommandBufferLevel::ePrimary,
		.commandBufferCount = 1
	};
	const vk::raii::CommandBuffer copyCmdBuffer = std::move(device.allocateCommandBuffers(allocInfo).front());

	constexpr vk::CommandBufferBeginInfo beginInfo{
		.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit
	};
	copyCmdBuffer.begin(beginInfo);
	copyCmdBuffer.copyBuffer(*srcBuffer, *dstBuffer, vk::BufferCopy{ .size = size });
	copyCmdBuffer.end();

	const vk::SubmitInfo submitInfo{
		.waitSemaphoreCount = waitSemaphore ? 1u : 0u,
		.pWaitSemaphores = waitSemaphore ? reinterpret_cast<const vk::Semaphore*>(waitSemaphore) : nullptr,
		.commandBufferCount = 1u,
		.pCommandBuffers = &*copyCmdBuffer,
		.signalSemaphoreCount = signalSemaphore ? 1u : 0u,
		.pSignalSemaphores = signalSemaphore ? reinterpret_cast<const vk::Semaphore*>(signalSemaphore) : nullptr,
	};
	graphicsQueue.submit(submitInfo, nullptr);
	graphicsQueue.waitIdle();
}
