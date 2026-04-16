#pragma once

#include "Vulkan.hpp"
#include "Shaders/Shader.hpp"

#include <string>
#include <vector>
#include <unordered_map>

namespace Kerberos
{
	struct ComputePipelineSpecification
	{
		std::string Name;
		Ref<Shader> Shader;
		vk::PipelineLayout PipelineLayout;

		std::unordered_map<vk::ShaderStageFlagBits, vk::SpecializationInfo> SpecializationMapEntries;
	};


	class ComputePipeline final
	{
	public:
		explicit ComputePipeline(ComputePipelineSpecification spec);

		void Bind(const vk::raii::CommandBuffer& cmd) const;

		void Recompile();

		const vk::raii::Pipeline& GetVulkanPipeline() const { return m_Pipeline; }

	private:
		void CreatePipeline(const ComputePipelineSpecification& spec);
		void StoreSpecializationData();

	private:
		ComputePipelineSpecification m_Specification;

		vk::raii::Pipeline m_Pipeline = nullptr;
		vk::raii::PipelineCache m_PipelineCache = nullptr;

		// Needed to back the storage of the specialization map entries and data, as the specialization info in the pipeline create info references them
		std::unordered_map<vk::ShaderStageFlagBits, std::vector<vk::SpecializationMapEntry>> m_SpecializationMapEntriesStorage;
		std::unordered_map<vk::ShaderStageFlagBits, std::vector<uint8_t>> m_SpecializationDataStorage;
	};
}