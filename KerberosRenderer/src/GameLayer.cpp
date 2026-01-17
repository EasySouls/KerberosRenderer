#include "GameLayer.hpp"

#include "VulkanContext.hpp"
#include "io.hpp"
#include "Texture.hpp"

#include "glm/glm.hpp"
#include "imgui.h"

#include <iostream>

#include "Vertex.hpp"


namespace Game
{
	GameLayer::GameLayer() 
		: Layer("GameLayer")
	{
	}

	void GameLayer::OnAttach() 
	{
		std::cout << "GameLayer attached!\n";

		m_Materials.emplace_back("Gold", glm::vec3(1.0f, 0.765557f, 0.336057f), 0.1f, 1.0f);
		m_Materials.emplace_back("Copper", glm::vec3(0.955008f, 0.637427f, 0.538163f), 0.1f, 1.0f);
		m_Materials.emplace_back("Chromium", glm::vec3(0.549585f, 0.556114f, 0.554256f), 0.1f, 1.0f);
		m_Materials.emplace_back("Nickel", glm::vec3(0.659777f, 0.608679f, 0.525649f), 0.1f, 1.0f);
		m_Materials.emplace_back("Titanium", glm::vec3(0.541931f, 0.496791f, 0.449419f), 0.1f, 1.0f);
		m_Materials.emplace_back("Cobalt", glm::vec3(0.662124f, 0.654864f, 0.633732f), 0.1f, 1.0f);
		m_Materials.emplace_back("Platinum", glm::vec3(0.672411f, 0.637331f, 0.585456f), 0.1f, 1.0f);
		m_Materials.emplace_back("White", glm::vec3(1.0f), 0.1f, 1.0f);
		m_Materials.emplace_back("Red", glm::vec3(1.0f, 0.0f, 0.0f), 0.1f, 1.0f);
		m_Materials.emplace_back("Blue", glm::vec3(0.0f, 0.0f, 1.0f), 0.1f, 1.0f);
		m_Materials.emplace_back("Black", glm::vec3(0.0f), 0.1f, 1.0f);

		auto& context = kbr::VulkanContext::Get();
		auto& device = context.GetDevice();

		// Create shared pipeline states
		std::vector dynamicStates = {
			vk::DynamicState::eViewport,
			vk::DynamicState::eScissor
		};

		const vk::PipelineDynamicStateCreateInfo dynamicStateInfo{
			.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
			.pDynamicStates = dynamicStates.data()
		};

		vk::PipelineInputAssemblyStateCreateInfo inputAssembly{ .topology = vk::PrimitiveTopology::eTriangleList };

		vk::PipelineViewportStateCreateInfo viewportState{ .viewportCount = 1, .scissorCount = 1 };

		// Create the shadow map resources
		{
			// Create shadow map image
			const vk::Format shadowMapFormat = context.FindSupportedFormat({ vk::Format::eD32Sfloat }, vk::ImageTiling::eOptimal, vk::FormatFeatureFlagBits::eDepthStencilAttachment);
			constexpr uint32_t shadowMapSize = 2048;
			constexpr uint32_t shadowMapMipLevels = 1;

			CreateImage(device,
						shadowMapSize,
						shadowMapSize,
						shadowMapMipLevels,
						vk::SampleCountFlagBits::e1,
						shadowMapFormat,
						vk::ImageTiling::eOptimal,
						vk::ImageUsageFlagBits::eDepthStencilAttachment,
						vk::MemoryPropertyFlagBits::eDeviceLocal,
						m_ShadowMapImage,
						m_ShadowMapImageMemory);

			m_ShadowMapImageView = CreateImageView(device, m_ShadowMapImage, shadowMapFormat, vk::ImageAspectFlagBits::eDepth, shadowMapMipLevels);

			// Create shadow map image layout transition
			/*context.TransitionImageLayout(shadowMapImage,
										  vk::ImageLayout::eUndefined,
										  vk::ImageLayout::eDepthStencilAttachmentOptimal,
										  shadowMapMipLevels);*/

			// Create descriptor set layout for shadow map shader
			constexpr vk::DescriptorSetLayoutBinding shadowMapLayoutBinding(
				0,
				vk::DescriptorType::eUniformBuffer,
				1,
				vk::ShaderStageFlagBits::eVertex,
				nullptr);

			const vk::DescriptorSetLayoutCreateInfo shadowMapLayoutInfo{
				.bindingCount = 1,
				.pBindings = &shadowMapLayoutBinding
			};

			auto shadowMapDescriptorSetLayout = vk::raii::DescriptorSetLayout{ device, shadowMapLayoutInfo };

			vk::PipelineLayoutCreateInfo pipelineLayoutInfo{
				.setLayoutCount = 1,
				.pSetLayouts = &*shadowMapDescriptorSetLayout,
				.pushConstantRangeCount = 0
			};

			m_ShadowMapPipelineLayout = vk::raii::PipelineLayout{ device, pipelineLayoutInfo };

			// Create shader for shadow mapping
			const auto shaderCode = IO::ReadFile("src/shaders/shadowmap.spv");
			const vk::ShaderModuleCreateInfo shaderInfo{ 
				.codeSize = shaderCode.size() * sizeof(char), 
				.pCode = reinterpret_cast<const uint32_t*>(shaderCode.data()) 
			};
			vk::raii::ShaderModule shaderModule{ device, shaderInfo };

			const vk::PipelineShaderStageCreateInfo vertShaderStageInfo{ .stage = vk::ShaderStageFlagBits::eVertex, .module = shaderModule,  .pName = "vertMain" };
			const vk::PipelineShaderStageCreateInfo fragShaderStageInfo{ .stage = vk::ShaderStageFlagBits::eFragment, .module = shaderModule, .pName = "fragMain" };
			vk::PipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

			constexpr vk::VertexInputBindingDescription bindingDescription = { 0, sizeof(glm::vec3), vk::VertexInputRate::eVertex};
			constexpr std::array attributeDescriptions = {
				vk::VertexInputAttributeDescription{.location = 0, .binding = 0, .format = vk::Format::eR32G32B32Sfloat, .offset = 0 }
			};
			vk::PipelineVertexInputStateCreateInfo vertexInputInfo{
				.vertexBindingDescriptionCount = 1,
				.pVertexBindingDescriptions = &bindingDescription,
				.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size()),
				.pVertexAttributeDescriptions = attributeDescriptions.data(),
			};

			vk::PipelineRasterizationStateCreateInfo rasterizer{
				.depthClampEnable = vk::True,
				.rasterizerDiscardEnable = vk::False,
				.polygonMode = vk::PolygonMode::eFill,
				.cullMode = vk::CullModeFlagBits::eBack,
				.frontFace = vk::FrontFace::eCounterClockwise,
				.depthBiasEnable = vk::True,
				.depthBiasConstantFactor = 1.25f,
				.depthBiasSlopeFactor = 1.75f,
				.lineWidth = 1.0f
			};

			vk::PipelineMultisampleStateCreateInfo multisampling{
				.rasterizationSamples = vk::SampleCountFlagBits::e1,
				.sampleShadingEnable = vk::False,
				.minSampleShading = 1.0f,
				.pSampleMask = nullptr,
				.alphaToCoverageEnable = vk::False,
				.alphaToOneEnable = vk::False
			};

			vk::PipelineDepthStencilStateCreateInfo depthStencil{
				.depthTestEnable = vk::True,
				.depthWriteEnable = vk::True,
				.depthCompareOp = vk::CompareOp::eLess,
				.depthBoundsTestEnable = vk::False,
				.stencilTestEnable = vk::False,
				.minDepthBounds = 0.0f,
				.maxDepthBounds = 1.0f,
			};

			vk::PipelineColorBlendAttachmentState colorBlendAttachment{
				.blendEnable = vk::False,
				.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
			};

			vk::PipelineColorBlendStateCreateInfo colorBlending{
				.logicOpEnable = vk::False,
				.logicOp = vk::LogicOp::eCopy,
				.attachmentCount = 1,
				.pAttachments = &colorBlendAttachment
			};

			vk::PipelineRenderingCreateInfo pipelineRenderingCreateInfo{
				.colorAttachmentCount = 0,
				.pColorAttachmentFormats = nullptr,
				.depthAttachmentFormat = shadowMapFormat
			};

			vk::GraphicsPipelineCreateInfo pipelineInfo{
				.pNext = &pipelineRenderingCreateInfo,
				.stageCount = 2,
				.pStages = shaderStages,
				.pVertexInputState = &vertexInputInfo,
				.pInputAssemblyState = &inputAssembly,
				.pViewportState = &viewportState,
				.pRasterizationState = &rasterizer,
				.pMultisampleState = &multisampling,
				.pDepthStencilState = &depthStencil,
				.pColorBlendState = &colorBlending,
				.pDynamicState = &dynamicStateInfo,
				.layout = m_ShadowMapPipelineLayout,
				.renderPass = nullptr
			};

			m_ShadowMapPipeline = vk::raii::Pipeline(device, nullptr, pipelineInfo);
		}

		// Create the opaque and transparent pipeline resources
		{
			const vk::Format colorFormat = context.FindSupportedFormat(
				{ vk::Format::eR32G32B32A32Uint }, 
				vk::ImageTiling::eOptimal, 
				vk::FormatFeatureFlagBits::eColorAttachment
			);
			constexpr uint32_t imageSize = 2048;
			constexpr uint32_t mipLevels = 1;

			CreateImage(device,
						imageSize,
						imageSize,
						mipLevels,
						vk::SampleCountFlagBits::e1,
						colorFormat,
						vk::ImageTiling::eOptimal,
						vk::ImageUsageFlagBits::eColorAttachment,
						vk::MemoryPropertyFlagBits::eDeviceLocal,
						m_ColorImage,
						m_ColorImageMemory);

			m_ColorImageView = CreateImageView(device, m_ColorImage, colorFormat, vk::ImageAspectFlagBits::eColor, mipLevels);

			const vk::Format depthFormat = context.FindSupportedFormat(
				{ vk::Format::eD32Sfloat }, 
				vk::ImageTiling::eOptimal, 
				vk::FormatFeatureFlagBits::eDepthStencilAttachment
			);

			CreateImage(
				device,
				imageSize,
				imageSize,
				mipLevels,
				vk::SampleCountFlagBits::e1,
				depthFormat,
				vk::ImageTiling::eOptimal,
				vk::ImageUsageFlagBits::eDepthStencilAttachment,
				vk::MemoryPropertyFlagBits::eDeviceLocal,
				m_DepthImage,
				m_DepthImageMemory);

			m_DepthImageView = CreateImageView(device, m_DepthImage, depthFormat, vk::ImageAspectFlagBits::eDepth, mipLevels);

			constexpr std::array bindings = {
				vk::DescriptorSetLayoutBinding{
					.binding = 0,
					.descriptorType = vk::DescriptorType::eUniformBuffer,
					.descriptorCount = 1,
					.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
					.pImmutableSamplers = nullptr
				},
				vk::DescriptorSetLayoutBinding{
					.binding = 1,
					.descriptorType = vk::DescriptorType::eCombinedImageSampler,
					.descriptorCount = 1,
					.stageFlags = vk::ShaderStageFlagBits::eFragment,
					.pImmutableSamplers = nullptr
				}
			};

			const vk::DescriptorSetLayoutCreateInfo layoutInfo{
				.bindingCount = static_cast<uint32_t>(bindings.size()),
				.pBindings = bindings.data()
			};

			auto descriptorSetLayout = vk::raii::DescriptorSetLayout{ device, layoutInfo };

			vk::PipelineLayoutCreateInfo pipelineLayoutInfo{
				.setLayoutCount = 1,
				.pSetLayouts = &*descriptorSetLayout,
				.pushConstantRangeCount = 0
			};

			m_PBRPipelineLayout = vk::raii::PipelineLayout{ device, pipelineLayoutInfo };

			const auto shaderCode = IO::ReadFile("src/shaders/pbrbasic.spv");
			const vk::ShaderModuleCreateInfo shaderInfo{
				.codeSize = shaderCode.size() * sizeof(char),
				.pCode = reinterpret_cast<const uint32_t*>(shaderCode.data())
			};
			vk::raii::ShaderModule shaderModule{ device, shaderInfo };

			const vk::PipelineShaderStageCreateInfo vertShaderStageInfo{ .stage = vk::ShaderStageFlagBits::eVertex, .module = shaderModule,  .pName = "vertMain" };
			const vk::PipelineShaderStageCreateInfo fragShaderStageInfo{ .stage = vk::ShaderStageFlagBits::eFragment, .module = shaderModule, .pName = "fragMain" };
			vk::PipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

			const auto bindingDesc = kbr::Vertex::GetBindingDescription();
			const auto attributeDescs = kbr::Vertex::GetAttributeDescriptions();

			vk::PipelineVertexInputStateCreateInfo vertexInputInfo{
				.vertexBindingDescriptionCount = 1,
				.pVertexBindingDescriptions = &bindingDesc,
				.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescs.size()),
				.pVertexAttributeDescriptions = attributeDescs.data(),
			};

			vk::PipelineMultisampleStateCreateInfo multisampling{
				.rasterizationSamples = vk::SampleCountFlagBits::e1,
				.sampleShadingEnable = vk::False,
				.minSampleShading = 1.0f,
				.pSampleMask = nullptr,
				.alphaToCoverageEnable = vk::False,
				.alphaToOneEnable = vk::False
			};

			vk::PipelineRasterizationStateCreateInfo opaqueRasterizer{
				.depthClampEnable = vk::False,
				.rasterizerDiscardEnable = vk::False,
				.polygonMode = vk::PolygonMode::eFill,
				.cullMode = vk::CullModeFlagBits::eBack,
				.frontFace = vk::FrontFace::eCounterClockwise,
				.depthBiasEnable = vk::False,
				.lineWidth = 1.0f
			};

			vk::PipelineRasterizationStateCreateInfo transparentRasterizer{
				.depthClampEnable = vk::False,
				.rasterizerDiscardEnable = vk::False,
				.polygonMode = vk::PolygonMode::eFill,
				.cullMode = vk::CullModeFlagBits::eNone,
				.frontFace = vk::FrontFace::eCounterClockwise,
				.depthBiasEnable = vk::False,
				.lineWidth = 1.0f
			};

			vk::PipelineDepthStencilStateCreateInfo opaqueDepthStencil{
				.depthTestEnable = vk::True,
				.depthWriteEnable = vk::True,
				.depthCompareOp = vk::CompareOp::eLess,
				.depthBoundsTestEnable = vk::False,
				.stencilTestEnable = vk::False,
				.minDepthBounds = 0.0f,
				.maxDepthBounds = 1.0f,
			};

			vk::PipelineDepthStencilStateCreateInfo transparentDepthStencil{
				.depthTestEnable = vk::True,
				.depthWriteEnable = vk::False,
				.depthCompareOp = vk::CompareOp::eLess,
				.depthBoundsTestEnable = vk::False,
				.stencilTestEnable = vk::False,
				.minDepthBounds = 0.0f,
				.maxDepthBounds = 1.0f,
			};

			vk::PipelineColorBlendAttachmentState opaqueColorBlendAttachment{
				.blendEnable = vk::False,
				.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
			};

			vk::PipelineColorBlendStateCreateInfo colorBlending{
				.logicOpEnable = vk::False,
				.logicOp = vk::LogicOp::eCopy,
				.attachmentCount = 1,
				.pAttachments = &opaqueColorBlendAttachment
			};

			vk::PipelineColorBlendAttachmentState transparentColorBlendAttachment{
				.blendEnable = vk::True,
				.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha,
				.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
				.colorBlendOp = vk::BlendOp::eAdd,
				.srcAlphaBlendFactor = vk::BlendFactor::eOne,
				.dstAlphaBlendFactor = vk::BlendFactor::eZero,
				.alphaBlendOp = vk::BlendOp::eAdd,
				.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
			};

			vk::PipelineColorBlendStateCreateInfo transparentColorBlending{
				.logicOpEnable = vk::False,
				.logicOp = vk::LogicOp::eCopy,
				.attachmentCount = 1,
				.pAttachments = &transparentColorBlendAttachment
			};

			vk::PipelineRenderingCreateInfo pipelineRenderingCreateInfo{
				.colorAttachmentCount = 1,
				.pColorAttachmentFormats = &colorFormat,
				.depthAttachmentFormat = depthFormat
			};

			vk::GraphicsPipelineCreateInfo opaquePipelineInfo{
				.pNext = &pipelineRenderingCreateInfo,
				.stageCount = 2,
				.pStages = shaderStages,
				.pVertexInputState = &vertexInputInfo,
				.pInputAssemblyState = &inputAssembly,
				.pViewportState = &viewportState,
				.pRasterizationState = &opaqueRasterizer,
				.pMultisampleState = &multisampling,
				.pDepthStencilState = &opaqueDepthStencil,
				.pColorBlendState = &colorBlending,
				.pDynamicState = &dynamicStateInfo,
				.layout = m_PBRPipelineLayout,
				.renderPass = nullptr
			};

			m_PBROpaquePipeline = vk::raii::Pipeline(device, nullptr, opaquePipelineInfo);

			vk::GraphicsPipelineCreateInfo transparentPipelineInfo{
				.pNext = &pipelineRenderingCreateInfo,
				.stageCount = 2,
				.pStages = shaderStages,
				.pVertexInputState = &vertexInputInfo,
				.pInputAssemblyState = &inputAssembly,
				.pViewportState = &viewportState,
				.pRasterizationState = &transparentRasterizer,
				.pMultisampleState = &multisampling,
				.pDepthStencilState = &transparentDepthStencil,
				.pColorBlendState = &transparentColorBlending,
				.pDynamicState = &dynamicStateInfo,
				.layout = m_PBRPipelineLayout,
				.renderPass = nullptr
			};

			m_PBRTransparentPipeline = vk::raii::Pipeline(device, nullptr, transparentPipelineInfo);
		}

	}
	
	void GameLayer::OnDetach() 
	{
		std::cout << "GameLayer detached!\n";
	}

	void GameLayer::OnUpdate(const float deltaTime)
	{
		m_Fps = 1.0f / deltaTime;
		m_Time += deltaTime;

		const auto& context = kbr::VulkanContext::Get();

		const auto cmd = context.BeginSingleTimeCommands();

		// Render shadow map
		{
			vk::ImageMemoryBarrier2 barrier = {
			.srcStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
			.srcAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
			.dstStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
			.dstAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
			.oldLayout = vk::ImageLayout::eUndefined,
			.newLayout = vk::ImageLayout::eDepthAttachmentOptimal,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = m_ShadowMapImage,
			.subresourceRange = {
				.aspectMask = vk::ImageAspectFlagBits::eDepth,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1
			}
			};

			const vk::DependencyInfo dependencyInfo = {
				.dependencyFlags = {},
				.imageMemoryBarrierCount = 1,
				.pImageMemoryBarriers = &barrier
			};

			cmd.pipelineBarrier2(dependencyInfo);

			vk::RenderingAttachmentInfo shadowMapDepthAttachmentInfo{
				.imageView = m_ShadowMapImageView,
				.imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
				.loadOp = vk::AttachmentLoadOp::eClear,
				.storeOp = vk::AttachmentStoreOp::eStore,
				.clearValue = vk::ClearDepthStencilValue{.depth = 1.0f, .stencil = 0 }
			};

			constexpr vk::Rect2D renderArea{
				.offset = vk::Offset2D{ .x = 0, .y = 0 },
				.extent = vk::Extent2D{ .width = 2048, .height = 2048 }
			};

			const vk::RenderingInfo shadowMapRenderingInfo{
				.renderArea = renderArea,
				.layerCount = 1,
				.pDepthAttachment = &shadowMapDepthAttachmentInfo
			};

			cmd.beginRendering(shadowMapRenderingInfo);
			cmd.setViewport(0, vk::Viewport{.x = 0.0f, .y = 0.0f, .width = 2048.0f, .height = 2048.0f, .minDepth = 0.0f,
				                .maxDepth = 1.0f });
			cmd.setScissor(0, renderArea);

			cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_ShadowMapPipeline);

			cmd.endRendering();
		}

		// Render opaque objects
		{
			vk::RenderingAttachmentInfo colorAttachmentInfo{
				.imageView = m_ColorImageView,
				.imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
				.loadOp = vk::AttachmentLoadOp::eClear,
				.storeOp = vk::AttachmentStoreOp::eStore,
				.clearValue = vk::ClearColorValue{ std::array{1.0f, 0.0f, 0.0f, 1.0f} }
			};

			vk::RenderingAttachmentInfo depthAttachmentInfo{
				.imageView = m_DepthImageView,
				.imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
				.loadOp = vk::AttachmentLoadOp::eClear,
				.storeOp = vk::AttachmentStoreOp::eDontCare,
				.clearValue = vk::ClearDepthStencilValue{ .depth = 1.0f, .stencil = 0 }
			};

			constexpr vk::Rect2D renderArea{
				.offset = vk::Offset2D{ .x = 0, .y = 0 },
				.extent = vk::Extent2D{ .width = 2048, .height = 2048 }
			};

			const vk::RenderingInfo renderingInfo{
				.renderArea = renderArea,
				.layerCount = 1,
				.pColorAttachments = &colorAttachmentInfo,
				.pDepthAttachment = &depthAttachmentInfo
			};

			cmd.beginRendering(renderingInfo);

			cmd.setViewport(0, vk::Viewport{.x = 0.0f, .y = 0.0f, .width = 2048.0f, .height = 2048.0f, .minDepth = 0.0f,
								.maxDepth = 1.0f });

			cmd.setScissor(0, renderArea);

			cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_PBROpaquePipeline);

			cmd.endRendering();
		}

		// Render transparent objects
		{
			vk::RenderingAttachmentInfo colorAttachmentInfo{
				.imageView = m_ColorImageView,
				.imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
				.loadOp = vk::AttachmentLoadOp::eLoad,
				.storeOp = vk::AttachmentStoreOp::eStore,
			};

			vk::RenderingAttachmentInfo depthAttachmentInfo{
				.imageView = m_DepthImageView,
				.imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
				.loadOp = vk::AttachmentLoadOp::eLoad,
				.storeOp = vk::AttachmentStoreOp::eDontCare,
			};

			constexpr vk::Rect2D renderArea{
				.offset = vk::Offset2D{ .x = 0, .y = 0 },
				.extent = vk::Extent2D{ .width = 2048, .height = 2048 }
			};

			const vk::RenderingInfo renderingInfo{
				.renderArea = renderArea,
				.layerCount = 1,
				.pColorAttachments = &colorAttachmentInfo,
				.pDepthAttachment = &depthAttachmentInfo
			};

			cmd.beginRendering(renderingInfo);

			cmd.setViewport(0, vk::Viewport{.x = 0.0f, .y = 0.0f, .width = 2048.0f, .height = 2048.0f, .minDepth = 0.0f,
								.maxDepth = 1.0f });

			cmd.setScissor(0, renderArea);

			cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_PBRTransparentPipeline);

			cmd.endRendering();
		}

		context.EndSingleTimeCommands(cmd);
	}

	void GameLayer::OnEvent()
	{
	}

	void GameLayer::OnImGuiRender()
	{
		ImGui::Begin("Game Layer");
		ImGui::Text("Time: %.2f seconds", m_Time);
		ImGui::Text("FPS: %.2f", m_Fps);
		ImGui::End();
	}
}
