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
		if (sets.empty())
			return;

		if (const auto& context = VulkanContext::Get(); context.UseDescriptorBuffers())
		{
			std::vector<vk::DescriptorBufferBindingInfoEXT> bindingInfos(sets.size());;
			std::vector<uint32_t> bufferIndices(sets.size());
			std::vector<vk::DeviceSize> offsets(sets.size());

			for (size_t i = 0; i < sets.size(); ++i)
			{
				bindingInfos[i] = { .address = sets[i].BufferAddress, .usage = vk::BufferUsageFlagBits::eResourceDescriptorBufferEXT };
				bufferIndices[i] = static_cast<uint32_t>(i);
				offsets[i] = sets[i].BufferOffset;
			}

			cmd.bindDescriptorBuffersEXT({ bindingInfos });
			cmd.setDescriptorBufferOffsetsEXT(bindPoint, *pipelineLayout, firstSet, bufferIndices, offsets);
		}
		else
		{
			std::vector<vk::DescriptorSet> rawSets(sets.size());
			for (size_t i = 0; i < sets.size(); i++)
			{
				rawSets[i] = sets[i].DescriptorSet;
			}

			cmd.bindDescriptorSets(bindPoint, *pipelineLayout, firstSet, rawSets, nullptr);
		}
	}

	vk::raii::DescriptorSetLayout DescriptorManager::CreateDescriptorSetLayout(
		const std::vector<vk::DescriptorSetLayoutBinding>& bindings)
	{
		auto& context = VulkanContext::Get();
		vk::DescriptorSetLayoutCreateFlags flags{};

		if (context.UseDescriptorBuffers())
		{
			flags |= vk::DescriptorSetLayoutCreateFlagBits::eDescriptorBufferEXT;
		}

		const vk::DescriptorSetLayoutCreateInfo layoutInfo{
			.flags = flags,
			.bindingCount = static_cast<uint32_t>(bindings.size()),
			.pBindings = bindings.data()
		};

		return { context.GetDevice(), layoutInfo };
	}
}