#pragma once

#include "Vulkan.hpp"
#include "Vertex.hpp"

void CreateBuffer(
	const vk::raii::Device& device,
	vk::DeviceSize size,
	vk::BufferUsageFlags usage,
	vk::MemoryPropertyFlags properties,
	vk::raii::Buffer& buffer,
	vk::raii::DeviceMemory& bufferMemory
);

namespace Kerberos
{
	class VertexBuffer
	{
	public:
		explicit VertexBuffer(const std::vector<Vertex>& vertices);

		const vk::raii::Buffer& GetBuffer() const { return m_Buffer; }
		const vk::raii::DeviceMemory& GetBufferMemory() const { return m_BufferMemory; }

	private:
		vk::raii::Buffer m_Buffer = nullptr;
		vk::raii::DeviceMemory m_BufferMemory = nullptr;
	};

	class IndexBuffer
	{
	public:
		explicit IndexBuffer(const std::vector<uint32_t>& indices);

		const vk::raii::Buffer& GetBuffer() const { return m_Buffer; }
		const vk::raii::DeviceMemory& GetBufferMemory() const { return m_BufferMemory; }

	private:
		vk::raii::Buffer m_Buffer = nullptr;
		vk::raii::DeviceMemory m_BufferMemory = nullptr;
	};

	class UniformBuffer
	{
	public:
		explicit UniformBuffer(vk::DeviceSize bufferSize);

		const vk::raii::Buffer& GetBuffer() const { return m_Buffer; }
		const vk::raii::DeviceMemory& GetBufferMemory() const { return m_BufferMemory; }
		void* GetMappedData() const { return m_MappedData; }

	private:
		vk::raii::Buffer m_Buffer = nullptr;
		vk::raii::DeviceMemory m_BufferMemory = nullptr;
		void* m_MappedData = nullptr;
	};
}