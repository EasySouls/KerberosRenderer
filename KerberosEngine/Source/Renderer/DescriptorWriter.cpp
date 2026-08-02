#include "kbrpch.hpp"
#include "DescriptorWriter.hpp"

#include "VulkanContext.hpp"

namespace Kerberos
{
	DescriptorWriter::DescriptorWriter(const vk::raii::DescriptorSetLayout& layout, ShaderResourceSet& set)
	{
		m_Layout = &layout;
		m_Set = &set;
		if (const auto& context = VulkanContext::Get(); context.UseDescriptorBuffers())
		{
			m_UseDescriptorBuffers = true;
			m_DescriptorSize = m_Layout->getSizeEXT();
		}
		else
		{
			m_UseDescriptorBuffers = false;
		}
	}

	void DescriptorWriter::WriteStorageBuffer(const uint32_t binding, const vk::raii::Buffer& buffer, const vk::DeviceSize size,
		const vk::DeviceSize offset)
	{
		auto& context = VulkanContext::Get();
		const auto& device = context.GetDevice();

		if (m_UseDescriptorBuffers)
		{
			const vk::DeviceSize layoutOffset = m_Layout->getBindingOffsetEXT(binding);

			const vk::DescriptorAddressInfoEXT addrInfo{
				.address = device.getBufferAddress({ .buffer = buffer }) + offset,
				.range = size,
				.format = vk::Format::eUndefined
			};
			const vk::DescriptorGetInfoEXT getInfo{
				.type = vk::DescriptorType::eStorageBuffer, // TODO: Make this dynamic based on the descriptor type in the layout
				.data = vk::DescriptorDataEXT(&addrInfo)
			};

			device.getDescriptorEXT(getInfo, m_DescriptorSize, static_cast<uint8_t*>(m_Set->MappedData) + layoutOffset);
		}
		else
		{
			m_BufferInfos.push_back({.buffer = *buffer, .offset = offset, .range = size });
			m_Writes.push_back({ 
				.dstSet = m_Set->DescriptorSet,
				.dstBinding = binding,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = vk::DescriptorType::eStorageBuffer, // TODO: Make this dynamic based on the descriptor type in the layout
				.pImageInfo = nullptr,
				.pBufferInfo = &m_BufferInfos.back(),
				.pTexelBufferView = nullptr
			});
		}
	}

	void DescriptorWriter::Flush() const
	{
		if (!m_UseDescriptorBuffers && !m_Writes.empty())
		{
			VulkanContext::Get().GetDevice().updateDescriptorSets(m_Writes, nullptr);
		}
	}
}
