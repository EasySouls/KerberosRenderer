#pragma once

#include "Vulkan.hpp"
#include "ShaderResourceSet.hpp"

#include <deque>
#include <vector>

namespace Kerberos
{
	class DescriptorWriter
	{
	public:
		DescriptorWriter(const vk::raii::DescriptorSetLayout& layout, ShaderResourceSet& set);

		void WriteStorageBuffer(uint32_t binding, const vk::Buffer& buffer, vk::DeviceSize size, vk::DeviceSize offset = 0);
		void WriteUniformBuffer(uint32_t binding, const vk::Buffer& buffer, vk::DeviceSize size, vk::DeviceSize offset = 0);
		void WriteSampledImage(uint32_t binding, const vk::ImageView& imageView);
		void WriteSampler(uint32_t binding, const vk::Sampler& sampler);

		void Flush();

	private:
		bool m_UseDescriptorBuffers = false;
		const vk::raii::DescriptorSetLayout* m_Layout = nullptr;
		ShaderResourceSet* m_Set = nullptr;
		vk::PhysicalDeviceDescriptorBufferPropertiesEXT m_DescProps{};

		std::deque<vk::DescriptorBufferInfo> m_BufferInfos;
		std::deque<vk::DescriptorImageInfo> m_ImageInfos;
		std::vector<vk::WriteDescriptorSet> m_Writes;
	};
}