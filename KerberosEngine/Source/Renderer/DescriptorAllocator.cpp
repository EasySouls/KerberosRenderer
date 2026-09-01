#include "DescriptorAllocator.hpp"

#include "VulkanContext.hpp"

import Kerberos;

namespace Kerberos
{
	DescriptorAllocator::DescriptorAllocator(const uint32_t maxSets, vk::DeviceSize bufferHeapSize)
	{
		auto& context = VulkanContext::Get();
		const auto& device = context.GetDevice();
		const auto& physicalDevice = context.GetPhysicalDevice();

		m_UseDescriptorBuffers = context.UseDescriptorBuffers();

		if (m_UseDescriptorBuffers)
		{
			VkBufferCreateInfo bufferInfo{};
			bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			bufferInfo.size = bufferHeapSize;
			bufferInfo.usage = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
			bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			VmaAllocationCreateInfo allocInfo{};
			allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

			VkBuffer buffer = nullptr;
			if (vmaCreateBuffer(VulkanContext::Get().GetAllocator().get(), &bufferInfo, &allocInfo, &buffer, &m_Allocation, nullptr) != VK_SUCCESS)
				KBRAssert(false, "Failed to create descriptor buffer!");

			m_Handle = vk::Buffer(buffer);
			vmaMapMemory(VulkanContext::Get().GetAllocator().get(), m_Allocation, &m_MappedData);

			const vk::BufferDeviceAddressInfo addressInfo{ .buffer = m_Handle };
			m_DeviceAddress = device.getBufferAddress(addressInfo);

			m_CurrentOffset = 0;

			const auto result = physicalDevice.getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceDescriptorBufferPropertiesEXT>();
			const auto descBufferProps = result.get<vk::PhysicalDeviceDescriptorBufferPropertiesEXT>();

			m_OffsetAlignment = descBufferProps.descriptorBufferOffsetAlignment;
		}
		else
		{
			// Create a descriptor pool for traditional descriptor sets

			std::vector<vk::DescriptorPoolSize> poolSizes = {
				{ .type = vk::DescriptorType::eUniformBuffer, .descriptorCount = maxSets },
				{ .type = vk::DescriptorType::eStorageBuffer, .descriptorCount = maxSets },
				{ .type = vk::DescriptorType::eSampledImage, .descriptorCount = maxSets },
				{ .type = vk::DescriptorType::eSampler, .descriptorCount = maxSets }
			};

			const vk::DescriptorPoolCreateInfo poolInfo{
				.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
				.maxSets = maxSets,
				.poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
				.pPoolSizes = poolSizes.data()
			};
			m_DescriptorPool = vk::raii::DescriptorPool{ device, poolInfo };
		}
	}

	DescriptorAllocator::~DescriptorAllocator()
	{
		const auto& allocator = VulkanContext::Get().GetAllocator().get();

		vmaUnmapMemory(allocator, m_Allocation);
		vmaDestroyBuffer(allocator, m_Handle, m_Allocation);
	}

	ShaderResourceSet DescriptorAllocator::Allocate(const vk::raii::DescriptorSetLayout& layout, const std::string& debugName)
	{
		ShaderResourceSet set{};

		auto& context = VulkanContext::Get();
		const auto& device = context.GetDevice();

		if (m_UseDescriptorBuffers)
		{
			const vk::DeviceSize layoutSize = layout.getSizeEXT();

			auto alignOffset = [](const vk::DeviceSize offset, const vk::DeviceSize alignment)
				{
					return (offset + alignment - 1) & ~(alignment - 1);
				};

			m_CurrentOffset = alignOffset(m_CurrentOffset, m_OffsetAlignment);

			set.BackingBuffer = m_Handle;
			set.BufferAddress = m_DeviceAddress;
			set.BufferOffset = m_CurrentOffset;
			set.MappedData = static_cast<uint8_t*>(m_MappedData) + m_CurrentOffset;

			m_CurrentOffset += layoutSize;

#ifdef KBR_DEBUG
			if (!debugName.empty())
			{
				m_AllocationDebugNames[m_CurrentOffset] = debugName;

				Kerberos::Log::CoreTrace("Descriptor Allocator: Bound '{}' at offset {} (size {})", debugName, m_CurrentOffset, layoutSize);
			}
#endif
		}
		else
		{
			const vk::DescriptorSetAllocateInfo allocInfo{
				.descriptorPool = *m_DescriptorPool,
				.descriptorSetCount = 1,
				.pSetLayouts = &(*layout)
			};
			set.DescriptorSet = std::move(device.allocateDescriptorSets(allocInfo).front());
		}

		return set;
	}

	void DescriptorAllocator::Reset()
	{
		if (m_UseDescriptorBuffers)
		{
			m_CurrentOffset = 0;
		}
		else
		{
			m_DescriptorPool.reset();
		}
	}
}