#include "ComputePipeline.hpp"

#include "Vertex.hpp"
#include "VulkanContext.hpp"

import Kerberos;

namespace Kerberos
{
	ComputePipeline::ComputePipeline(ComputePipelineSpecification spec)
		: m_Specification(std::move(spec))
	{
		StoreSpecializationData();
		CreatePipeline(m_Specification);
	}

	void ComputePipeline::Bind(const vk::raii::CommandBuffer& cmd) const
	{
		KBRAssert(m_Pipeline != nullptr, "Pipeline is null!");

		cmd.bindPipeline(vk::PipelineBindPoint::eCompute, m_Pipeline);
	}

	void ComputePipeline::Recompile()
	{
		KBRAssert(m_Pipeline != nullptr, "Pipeline not created yet!");
		KBRAssert(m_Specification.Shader != nullptr, "Shader is null!");

		const auto result = m_Specification.Shader->Recompile();
		if (!result)
		{
			Log::CoreError("Failed to recompile shader for compute pipeline: {}", m_Specification.Name);
			return;
		}
		if (!*result)
		{
			Log::CoreInfo("Shader for compute pipeline '{}' is already up to date. No recompilation needed.", m_Specification.Name);
			return;
		}

		CreatePipeline(m_Specification);
	}

	void ComputePipeline::CreatePipeline(const ComputePipelineSpecification& spec)
	{
		KBRAssert(spec.Shader != nullptr, "Shader is null!");
		KBRAssert(spec.PipelineLayout, "Pipeline layout is null!");

		auto& context = VulkanContext::Get();
		const auto& device = context.GetDevice();

		std::vector<vk::PipelineShaderStageCreateInfo> shaderStages = spec.Shader->GetPipelineShaderStageCreateInfo();
		KBRAssert(shaderStages.size() == 1, "Compute pipeline must have exactly one shader stage!");

		for (const auto& [shaderStage, specInfo] : spec.SpecializationMapEntries)
		{
			int shaderStageIndex = -1;
			for (size_t i = 0; i < shaderStages.size(); i++)
			{
				if (shaderStages[i].stage == shaderStage)
				{
					shaderStageIndex = static_cast<int>(i);
					break;
				}
			}
			if (shaderStageIndex == -1)
			{
				Log::CoreError("Failed to find shader stage for specialization constant in pipeline: {}", spec.Name);
				return;
			}
			shaderStages[shaderStageIndex].pSpecializationInfo = &specInfo;
		}

		const vk::PipelineShaderStageCreateInfo& computeShaderStage = shaderStages[0];

		const vk::PipelineCreateFlags flags = spec.UsingDescriptorBuffers ? vk::PipelineCreateFlagBits::eDescriptorBufferEXT : vk::PipelineCreateFlags();
		const vk::ComputePipelineCreateInfo pipelineCreateInfo{
			.flags = flags,
			.stage = computeShaderStage,
			.layout = spec.PipelineLayout
		};

		constexpr vk::PipelineCacheCreateInfo pipelineCacheCreateInfo{
			.flags = vk::PipelineCacheCreateFlags(),
			.initialDataSize = 0,
			.pInitialData = nullptr
		};
		m_PipelineCache = vk::raii::PipelineCache{ device, pipelineCacheCreateInfo };

		m_Pipeline = vk::raii::Pipeline{ device,m_PipelineCache, pipelineCreateInfo };

		context.SetObjectDebugName(m_Pipeline, spec.Name);
	}

	void ComputePipeline::StoreSpecializationData()
	{
		for (auto& [stage, specInfo] : m_Specification.SpecializationMapEntries)
		{
			auto& ownedMapEntries = m_SpecializationMapEntriesStorage[stage];
			auto& ownedData = m_SpecializationDataStorage[stage];

			if (specInfo.mapEntryCount > 0)
			{
				KBRAssert(specInfo.pMapEntries != nullptr, "Specialization map entries pointer is null!");
				ownedMapEntries.assign(specInfo.pMapEntries, specInfo.pMapEntries + specInfo.mapEntryCount);
			}

			if (specInfo.dataSize > 0)
			{
				KBRAssert(specInfo.pData != nullptr, "Specialization data pointer is null!");
				ownedData.resize(specInfo.dataSize);
				std::memcpy(ownedData.data(), specInfo.pData, specInfo.dataSize);
			}

			specInfo.mapEntryCount = static_cast<uint32_t>(ownedMapEntries.size());
			specInfo.pMapEntries = ownedMapEntries.empty() ? nullptr : ownedMapEntries.data();
			specInfo.dataSize = ownedData.size();
			specInfo.pData = ownedData.empty() ? nullptr : ownedData.data();
		}
	}
}