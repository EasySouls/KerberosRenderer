#include "kbrpch.hpp"
#include "GraphicsPipeline.hpp"

#include "Vertex.hpp"
#include "VulkanContext.hpp"

#include <map>

namespace 
{
	vk::CullModeFlags GetVkCullMode(const Kerberos::CullMode cullMode)
	{
		using namespace Kerberos;

		if (cullMode == CullMode::Back)
			return vk::CullModeFlagBits::eBack;
		if (cullMode == CullMode::Front)
			return vk::CullModeFlagBits::eFront;
		if (cullMode == CullMode::None)
			return vk::CullModeFlagBits::eNone;

		KBR_CORE_ASSERT(false, "Unknown cull mode!");
		return vk::CullModeFlagBits::eNone;
	}

	vk::CompareOp GetVkCompareOp(const Kerberos::DepthTestFunc compareOp)
	{
		using namespace Kerberos;

		if (compareOp == DepthTestFunc::Never)
			return vk::CompareOp::eNever;
		if (compareOp == DepthTestFunc::Less)
			return vk::CompareOp::eLess;
		if (compareOp == DepthTestFunc::Equal)
			return vk::CompareOp::eEqual;
		if (compareOp == DepthTestFunc::LessOrEqual)
			return vk::CompareOp::eLessOrEqual;
		if (compareOp == DepthTestFunc::Greater)
			return vk::CompareOp::eGreater;
		if (compareOp == DepthTestFunc::NotEqual)
			return vk::CompareOp::eNotEqual;
		if (compareOp == DepthTestFunc::GreaterOrEqual)
			return vk::CompareOp::eGreaterOrEqual;
		if (compareOp == DepthTestFunc::Always)
			return vk::CompareOp::eAlways;

		KBR_CORE_ASSERT(false, "Unknown depth test function!");
		return vk::CompareOp::eAlways;
	}

	constexpr vk::PipelineColorBlendAttachmentState noBlendAttachment{
		.blendEnable = vk::False,
		.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
	};

	constexpr vk::PipelineColorBlendAttachmentState additiveBlendAttachment{
		.blendEnable = vk::True,
		.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha,
		.dstColorBlendFactor = vk::BlendFactor::eOne,
		.colorBlendOp = vk::BlendOp::eAdd,
		.srcAlphaBlendFactor = vk::BlendFactor::eOne,
		.dstAlphaBlendFactor = vk::BlendFactor::eZero,
		.alphaBlendOp = vk::BlendOp::eAdd,
		.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
	};

	constexpr vk::PipelineColorBlendAttachmentState alphaBlendAttachment{
		.blendEnable = vk::True,
		.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha,
		.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
		.colorBlendOp = vk::BlendOp::eAdd,
		.srcAlphaBlendFactor = vk::BlendFactor::eOne,
		.dstAlphaBlendFactor = vk::BlendFactor::eZero,
		.alphaBlendOp = vk::BlendOp::eAdd,
		.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
	};

	std::map<Kerberos::BlendMode, vk::PipelineColorBlendAttachmentState> s_BlendModeToVkBlendState
	{
		{ Kerberos::BlendMode::None, noBlendAttachment },
		{ Kerberos::BlendMode::Additive, additiveBlendAttachment },
		{ Kerberos::BlendMode::AlphaBlend, alphaBlendAttachment }
	};
}

namespace Kerberos
{
	GraphicsPipeline::GraphicsPipeline(GraphicsPipelineSpecification spec) 
		: m_Specification(std::move(spec))
	{
		StoreSpecializationData();
		CreatePipeline(m_Specification);
	}

	void GraphicsPipeline::Bind(const vk::raii::CommandBuffer& cmd) const 
	{
		KBR_CORE_ASSERT(m_Pipeline != nullptr, "Pipeline is null!");

		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_Pipeline);
	}

	void GraphicsPipeline::Recompile() 
	{
		KBR_CORE_ASSERT(m_Pipeline != nullptr, "Pipeline not created yet!");
		KBR_CORE_ASSERT(m_Specification.Shader, "Shader is null!");

		const auto result = m_Specification.Shader->Recompile();
		if (!result) 
		{
			KBR_CORE_ERROR("Failed to recompile shader for graphics pipeline: {}", m_Specification.Name);
			return;
		}
		if (!*result)
		{
			KBR_CORE_INFO("Shader for graphics pipeline '{}' is already up to date. No recompilation needed.", m_Specification.Name);
			return;
		}

		CreatePipeline(m_Specification);
	}

	void GraphicsPipeline::CreatePipeline(const GraphicsPipelineSpecification& spec)
	{
		KBR_CORE_ASSERT(spec.BlendModes.size() == spec.ColorAttachmentFormats.size(), "Blend modes size must match color attachment formats size!");
		KBR_CORE_ASSERT(spec.Shader, "Shader is null!");
		KBR_CORE_ASSERT(spec.PipelineLayout, "Pipeline layout is null!");

		auto& context = VulkanContext::Get();
		const auto& device = context.GetDevice();

		vk::PipelineVertexInputStateCreateInfo vertexInputInfo{
			.vertexBindingDescriptionCount = 1,
			.pVertexBindingDescriptions = &spec.BindingDescription,
			.vertexAttributeDescriptionCount = static_cast<uint32_t>(spec.InputAttributeDescriptions.size()),
			.pVertexAttributeDescriptions = spec.InputAttributeDescriptions.data(),
		};

		vk::SampleCountFlagBits sampleCount = spec.SampleCount;
		if (const auto maxSampleCount = context.GetMaxMSAASamples(); sampleCount > maxSampleCount)
		{
			KBR_CORE_WARN("Pipeline created with more sample count than the maximum!");
			sampleCount = maxSampleCount;
		}

		const vk::PipelineMultisampleStateCreateInfo multisampling{
			.rasterizationSamples = sampleCount,
			.sampleShadingEnable = vk::False,
			.minSampleShading = 1.0f,
			.pSampleMask = nullptr,
			.alphaToCoverageEnable = vk::False,
			.alphaToOneEnable = vk::False
		};

		const vk::PipelineRasterizationStateCreateInfo rasterizer{
			.depthClampEnable = spec.EnableDepthClamp ? vk::True : vk::False,
			.rasterizerDiscardEnable = vk::False,
			.polygonMode = spec.PolygonModeFill ? vk::PolygonMode::eFill : vk::PolygonMode::eLine,
			.cullMode = GetVkCullMode(spec.CullMode),
			.frontFace = spec.FrontFace == FrontFace::CounterClockwise ? vk::FrontFace::eCounterClockwise : vk::FrontFace::eClockwise,
			.depthBiasEnable = spec.EnableDepthBias ? vk::True : vk::False,
			.lineWidth = 1.0f
		};

		const vk::PipelineDepthStencilStateCreateInfo opaqueDepthStencil{
			.depthTestEnable = spec.EnableDepthTest ? vk::True : vk::False,
			.depthWriteEnable = spec.EnableDepthWrite ? vk::True : vk::False,
			.depthCompareOp = GetVkCompareOp(spec.DepthTestFunc),
			.depthBoundsTestEnable = vk::False,
			.stencilTestEnable = vk::False,
			.minDepthBounds = 0.0f,
			.maxDepthBounds = 1.0f,
		};

		std::vector<vk::PipelineColorBlendAttachmentState> opaqueColorBlendAttachments(spec.ColorAttachmentFormats.size());
		for (size_t i = 0; i < spec.ColorAttachmentFormats.size(); i++)
		{
			const auto blendMode = spec.BlendModes[i];
			opaqueColorBlendAttachments[i] = s_BlendModeToVkBlendState[blendMode];
		}

		const vk::PipelineColorBlendStateCreateInfo colorBlending{
			.logicOpEnable = vk::False,
			.logicOp = vk::LogicOp::eCopy,
			.attachmentCount = static_cast<uint32_t>(opaqueColorBlendAttachments.size()),
			.pAttachments = opaqueColorBlendAttachments.data()
		};

		const vk::PipelineRenderingCreateInfo pipelineRenderingCreateInfo{
			.colorAttachmentCount = static_cast<uint32_t>(spec.ColorAttachmentFormats.size()),
			.pColorAttachmentFormats = spec.ColorAttachmentFormats.data(),
			.depthAttachmentFormat = spec.DepthAttachmentFormat.value_or(vk::Format::eUndefined)
		};

		const vk::PipelineDynamicStateCreateInfo dynamicStateInfo{
			.dynamicStateCount = static_cast<uint32_t>(spec.DynamicStates.size()),
			.pDynamicStates = spec.DynamicStates.data()
		};

		constexpr vk::PipelineInputAssemblyStateCreateInfo inputAssembly{ .topology = vk::PrimitiveTopology::eTriangleList };

		constexpr vk::PipelineViewportStateCreateInfo viewportState{ .viewportCount = 1, .scissorCount = 1 };

		std::vector<vk::PipelineShaderStageCreateInfo> shaderStages = spec.Shader->GetPipelineShaderStageCreateInfo();

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
				KBR_CORE_ERROR("Failed to find shader stage for specialization constant in pipeline: {}", spec.Name);
				return;
			}
			shaderStages[shaderStageIndex].pSpecializationInfo = &specInfo;
		}

		const vk::GraphicsPipelineCreateInfo pipelineCreateInfo{
			.pNext = &pipelineRenderingCreateInfo,
			.stageCount = static_cast<uint32_t>(shaderStages.size()),
			.pStages = shaderStages.data(),
			.pVertexInputState = &vertexInputInfo,
			.pInputAssemblyState = &inputAssembly,
			.pViewportState = &viewportState,
			.pRasterizationState = &rasterizer,
			.pMultisampleState = &multisampling,
			.pDepthStencilState = &opaqueDepthStencil,
			.pColorBlendState = &colorBlending,
			.pDynamicState = &dynamicStateInfo,
			.layout = spec.PipelineLayout,
			.renderPass = nullptr
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

	void GraphicsPipeline::StoreSpecializationData() 
	{
		for (auto& [stage, specInfo] : m_Specification.SpecializationMapEntries)
		{
			auto& ownedMapEntries = m_SpecializationMapEntriesStorage[stage];
			auto& ownedData = m_SpecializationDataStorage[stage];

			if (specInfo.mapEntryCount > 0)
			{
				KBR_CORE_ASSERT(specInfo.pMapEntries != nullptr, "Specialization map entries pointer is null!");
				ownedMapEntries.assign(specInfo.pMapEntries, specInfo.pMapEntries + specInfo.mapEntryCount);
			}

			if (specInfo.dataSize > 0)
			{
				KBR_CORE_ASSERT(specInfo.pData != nullptr, "Specialization data pointer is null!");
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
