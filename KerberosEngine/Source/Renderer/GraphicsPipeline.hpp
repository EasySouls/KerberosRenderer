#pragma once

#include "Vulkan.hpp"
#include "Shaders/Shader.hpp"

#include <string>
#include <vector>
#include <optional>
#include <unordered_map>

namespace Kerberos
{
	enum class CullMode : uint8_t
	{
		None = 0,
		Front = 1,
		Back = 2,
	};

	enum class FrontFace : uint8_t
	{
		Clockwise = 0,
		CounterClockwise = 1,
	};

	enum class DepthTestFunc : uint8_t
	{
		Never = 0,
		Less = 1,
		Equal = 2,
		LessOrEqual = 3,
		Greater = 4,
		NotEqual = 5,
		GreaterOrEqual = 6,
		Always = 7
	};

	enum class BlendMode : uint8_t
	{
		None = 0,
		Additive = 1,
		AlphaBlend = 2,
		Multiplicative = 3,
		PremultipliedAlpha = 4
	};

	enum class PrimitiveTopology : uint8_t
	{
		TriangleList = 0,
		LineList = 1
	};

	struct GraphicsPipelineSpecification
	{
		std::string Name;
		Ref<Shader> Shader;
		vk::PipelineLayout PipelineLayout;

		vk::VertexInputBindingDescription BindingDescription;
		std::vector<vk::VertexInputAttributeDescription> InputAttributeDescriptions;

		vk::SampleCountFlagBits SampleCount = vk::SampleCountFlagBits::e1;
		bool PolygonModeFill = true;
		CullMode CullMode = CullMode::Back;
		FrontFace FrontFace = FrontFace::CounterClockwise;

		bool EnableDepthClamp = false;
		bool EnableDepthBias = false;

		bool EnableDepthWrite = true;
		bool EnableDepthTest = true;
		DepthTestFunc DepthTestFunc = DepthTestFunc::LessOrEqual;
		PrimitiveTopology Topology = PrimitiveTopology::TriangleList;

		std::vector<BlendMode> BlendModes;
		std::vector<vk::Format> ColorAttachmentFormats;
		std::optional<vk::Format> DepthAttachmentFormat;

		std::vector<vk::DynamicState> DynamicStates;

		bool UsingDescriptorBuffers = false;

		std::unordered_map<vk::ShaderStageFlagBits, vk::SpecializationInfo> SpecializationMapEntries;
	};

	class GraphicsPipeline final
	{
	public:
		explicit GraphicsPipeline(GraphicsPipelineSpecification spec);

		void Bind(const vk::raii::CommandBuffer& cmd) const;

		void Recompile();

		const vk::raii::Pipeline& GetVulkanPipeline() const { return m_Pipeline; }

	private:
		void CreatePipeline(const GraphicsPipelineSpecification& spec);
		void StoreSpecializationData();

	private:
		GraphicsPipelineSpecification m_Specification;

		vk::raii::Pipeline m_Pipeline = nullptr;
		vk::raii::PipelineCache m_PipelineCache = nullptr;

		// Needed to back the storage of the specialization map entries and data, as the specialization info in the pipeline create info references them
		std::unordered_map<vk::ShaderStageFlagBits, std::vector<vk::SpecializationMapEntry>> m_SpecializationMapEntriesStorage;
		std::unordered_map<vk::ShaderStageFlagBits, std::vector<uint8_t>> m_SpecializationDataStorage;
	};
}
