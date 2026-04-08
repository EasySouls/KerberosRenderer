#include "kbrpch.hpp"
#include "Buffer.hpp"

#include "Utils.hpp"
#include "VulkanContext.hpp"

// TODO: Use VMA

void CreateBuffer(
	const vk::raii::Device& device,
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
	constexpr vk::MemoryAllocateFlagsInfo flagsInfo{ .flags = vk::MemoryAllocateFlagBits::eDeviceAddress };
	const vk::MemoryAllocateInfo allocInfo{
		.pNext = usage & vk::BufferUsageFlagBits::eShaderDeviceAddress ? &flagsInfo : nullptr,
		.allocationSize = memRequirements.size,
		.memoryTypeIndex = Kerberos::FindMemoryType(memRequirements.memoryTypeBits, properties)
	};
	bufferMemory = vk::raii::DeviceMemory(device, allocInfo);
	buffer.bindMemory(*bufferMemory, 0);
}

namespace Kerberos {

VertexBuffer::VertexBuffer(const std::vector<Vertex>& vertices) 
{
	const vk::raii::Device& device = VulkanContext::Get().GetDevice();

	const vk::DeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();
	const vk::BufferCreateInfo stagingInfo{ 
		.size = bufferSize, 
		.usage = vk::BufferUsageFlagBits::eTransferSrc, 
		.sharingMode = vk::SharingMode::eExclusive 
	};
	const vk::raii::Buffer stagingBuffer(device, stagingInfo);
	const vk::MemoryRequirements memRequirementsStaging = stagingBuffer.getMemoryRequirements();
	const vk::MemoryAllocateInfo memoryAllocateInfoStaging{
		.allocationSize = memRequirementsStaging.size,
		.memoryTypeIndex = FindMemoryType(memRequirementsStaging.memoryTypeBits,
										  vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent)
	};
	const vk::raii::DeviceMemory stagingBufferMemory(device, memoryAllocateInfoStaging);

	stagingBuffer.bindMemory(stagingBufferMemory, 0);
	void* dataStaging = stagingBufferMemory.mapMemory(0, stagingInfo.size);
	std::memcpy(dataStaging, vertices.data(), stagingInfo.size);
	stagingBufferMemory.unmapMemory();

	const vk::BufferCreateInfo bufferInfo{
		.size = bufferSize,
		.usage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR,
		.sharingMode = vk::SharingMode::eExclusive
	};
	m_Buffer = vk::raii::Buffer(device, bufferInfo);

	const vk::MemoryRequirements memRequirements = m_Buffer.getMemoryRequirements();
	constexpr vk::MemoryAllocateFlagsInfo flagsInfo{ .flags = vk::MemoryAllocateFlagBits::eDeviceAddress };
	const vk::MemoryAllocateInfo memoryAllocateInfo{
		.pNext = &flagsInfo,
		.allocationSize = memRequirements.size,
		.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits,
										  vk::MemoryPropertyFlagBits::eDeviceLocal),
	};
	m_BufferMemory = vk::raii::DeviceMemory(device, memoryAllocateInfo);

	m_Buffer.bindMemory(*m_BufferMemory, 0);

	VulkanContext::Get().CopyBuffer(stagingBuffer, m_Buffer, stagingInfo.size);

	const vk::BufferDeviceAddressInfo bufferDeviceAddressInfo{
		.buffer = *m_Buffer
	};
	m_DeviceAddress = device.getBufferAddress(bufferDeviceAddressInfo);
}

IndexBuffer::IndexBuffer(const std::vector<uint32_t>& indices) 
{
	const vk::raii::Device& device = VulkanContext::Get().GetDevice();

	const vk::DeviceSize bufferSize = sizeof(indices[0]) * indices.size();

	vk::raii::Buffer stagingBuffer({});
	vk::raii::DeviceMemory stagingBufferMemory({});
	CreateBuffer(device,
				 bufferSize,
				 vk::BufferUsageFlagBits::eTransferSrc,
				 vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
				 stagingBuffer, stagingBufferMemory);

	void* data = stagingBufferMemory.mapMemory(0, bufferSize);
	std::memcpy(data, indices.data(), bufferSize);
	stagingBufferMemory.unmapMemory();

	CreateBuffer(device, bufferSize,
				 vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR,
				 vk::MemoryPropertyFlagBits::eDeviceLocal,
				 m_Buffer, m_BufferMemory);

	VulkanContext::Get().CopyBuffer(stagingBuffer, m_Buffer, bufferSize);

	const vk::BufferDeviceAddressInfo bufferDeviceAddressInfo{
		.buffer = *m_Buffer
	};
	m_DeviceAddress = device.getBufferAddress(bufferDeviceAddressInfo);
}

UniformBuffer::UniformBuffer(const vk::DeviceSize bufferSize) 
{
	const vk::raii::Device& device = VulkanContext::Get().GetDevice();

	CreateBuffer(device,
				 bufferSize,
				 vk::BufferUsageFlagBits::eUniformBuffer,
				 vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
				 m_Buffer, m_BufferMemory);

	m_MappedData = m_BufferMemory.mapMemory(0, bufferSize);
}

StorageBuffer::StorageBuffer(const vk::DeviceSize bufferSize)
{
	const vk::raii::Device& device = VulkanContext::Get().GetDevice();

	CreateBuffer(device,
				 bufferSize,
				 vk::BufferUsageFlagBits::eStorageBuffer,
				 vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
				 m_Buffer, m_BufferMemory);

	m_MappedData = m_BufferMemory.mapMemory(0, bufferSize);
}

}
