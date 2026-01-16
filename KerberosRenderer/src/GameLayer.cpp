#include "GameLayer.hpp"

#include "VulkanContext.hpp"
#include "io.hpp"
#include "Texture.hpp"

#include "glm/glm.hpp"
#include "imgui.h"

#include <iostream>


namespace Game
{
	GameLayer::GameLayer() 
		: Layer("GameLayer")
	{
	}

	void GameLayer::OnAttach() 
	{
		std::cout << "GameLayer attached!\n";

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
						shadowMapImage,
						shadowMapImageMemory);

			shadowMapImageView = CreateImageView(device, shadowMapImage, shadowMapFormat, vk::ImageAspectFlagBits::eDepth, shadowMapMipLevels);

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

			shadowMapPipelineLayout = vk::raii::PipelineLayout{ device, pipelineLayoutInfo };

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

			vk::PipelineInputAssemblyStateCreateInfo inputAssembly{ .topology = vk::PrimitiveTopology::eTriangleList };

			vk::PipelineViewportStateCreateInfo viewportState{ .viewportCount = 1, .scissorCount = 1 };

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
				.layout = shadowMapPipelineLayout,
				.renderPass = nullptr
			};

			shadowMapPipeline = vk::raii::Pipeline(device, nullptr, pipelineInfo);
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
			.image = shadowMapImage,
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
				.imageView = shadowMapImageView,
				.imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
				.loadOp = vk::AttachmentLoadOp::eClear,
				.storeOp = vk::AttachmentStoreOp::eStore,
				.clearValue = vk::ClearDepthStencilValue{ 1.0f, 0 }
			};

			const vk::RenderingInfo shadowMapRenderingInfo{
				.renderArea = vk::Rect2D{ vk::Offset2D{ 0, 0 }, vk::Extent2D{ 2048, 2048 } },
				.layerCount = 1,
				.pDepthAttachment = &shadowMapDepthAttachmentInfo
			};

			cmd.beginRendering(shadowMapRenderingInfo);
			cmd.setViewport(0, vk::Viewport{ 0.0f, 0.0f, 2048.0f, 2048.0f, 0.0f, 1.0f });
			cmd.setScissor(0, vk::Rect2D{ vk::Offset2D{ 0, 0 }, vk::Extent2D{ 2048, 2048 } });

			cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *shadowMapPipeline);

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
