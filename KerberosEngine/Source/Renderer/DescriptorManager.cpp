#include "kbrpch.hpp"
#include "DescriptorManager.hpp"

#include "VulkanContext.hpp"

namespace Kerberos
{
	void DescriptorManager::BindSets(
		const vk::raii::CommandBuffer& cmd,
		const vk::PipelineBindPoint bindPoint,
		const vk::raii::PipelineLayout& pipelineLayout,
		const uint32_t firstSet,
		const std::vector<ShaderResourceSet>& sets)
	{
		if (const auto& context = VulkanContext::Get(); context.UseDescriptorBuffers())
		{
			vk::DescriptorBufferBindingInfoEXT bindingInfo{
				.address = sets[0].BufferAddress,
				.usage = vk::BufferUsageFlagBits::eResourceDescriptorBufferEXT
			};
			cmd.bindDescriptorBuffersEXT({ bindingInfo });

			const std::vector<uint32_t> bufferIndices(sets.size(), 0);
			std::vector<vk::DeviceSize> offsets(sets.size());
			for (size_t i = 0; i < sets.size(); i++)
			{
				offsets[i] = sets[i].BufferOffset;
			}

			cmd.setDescriptorBufferOffsetsEXT(bindPoint, *pipelineLayout, firstSet, bufferIndices, offsets);
		}
		else
		{
			std::vector<vk::DescriptorSet> traditionalSets(sets.size());
			for (size_t i = 0; i < sets.size(); i++)
				traditionalSets[i] = sets[i].DescriptorSet;

			cmd.bindDescriptorSets(bindPoint, *pipelineLayout, firstSet, traditionalSets, nullptr);
		}
	}
}
