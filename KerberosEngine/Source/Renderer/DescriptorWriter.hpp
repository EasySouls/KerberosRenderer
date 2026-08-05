#pragma once

#include "Vulkan.hpp"
#include "ShaderResourceSet.hpp"

namespace Kerberos
{
	class DescriptorWriter
	{
	public:
		DescriptorWriter(const vk::raii::DescriptorSetLayout& layout, ShaderResourceSet& set);

		void WriteStorageBuffer(uint32_t binding, const vk::raii::Buffer& buffer, vk::DeviceSize size, vk::DeviceSize offset = 0);
		void WriteUniformBuffer(uint32_t binding, const vk::raii::Buffer& buffer, vk::DeviceSize size, vk::DeviceSize offset = 0);
		void WriteSampledImage(uint32_t binding, const vk::raii::ImageView& imageView);
		void WriteSampler(uint32_t binding, const vk::raii::Sampler& sampler);

		void Flush();

	private:
		bool m_UseDescriptorBuffers = false;
		const vk::raii::DescriptorSetLayout* m_Layout = nullptr;
		ShaderResourceSet* m_Set = nullptr;
		vk::DeviceSize m_DescriptorSize = 0;
		std::vector<vk::DescriptorBufferInfo> m_BufferInfos;
		std::vector<vk::DescriptorImageInfo> m_ImageInfos;
		std::vector<vk::WriteDescriptorSet> m_Writes;
	};
}