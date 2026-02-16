#include "pch.hpp"
#include "SkyboxUtils.hpp"

#include "Vulkan.hpp"
#include "VulkanContext.hpp"
#include "Shader.hpp"
#include "Utils.hpp"
#include "Logging/Log.hpp"

#include <glm/glm.hpp>

#include <chrono>
#include <numbers>

#include "Vertex.hpp"

namespace kbr::SkyboxUtils
{
	void GenerateBRDFLUT(Texture2D& texture) 
	{
		auto tStart = std::chrono::high_resolution_clock::now();

		// R16G16 is supported pretty much everywhere
		constexpr vk::Format format = vk::Format::eR16G16Sfloat;
		constexpr int32_t dim = 512;

		auto& context = VulkanContext::Get();
		const auto& device = context.GetDevice();

		// Image
		vk::ImageCreateInfo imageInfo{
			.imageType = vk::ImageType::e2D,
			.format = format,
			.extent = {
				.width = dim,
				.height = dim,
				.depth = 1
			},
			.mipLevels = 1,
			.arrayLayers = 1,
			.samples = vk::SampleCountFlagBits::e1,
			.tiling = vk::ImageTiling::eOptimal,
			.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
		};

		texture.image = device.createImage(imageInfo);
		context.SetObjectDebugName(texture.image, "BRDFLUT_Image");

		vk::MemoryRequirements memReqs = texture.image.getMemoryRequirements();
		vk::MemoryAllocateInfo memAlloc{
			.allocationSize = memReqs.size,
			.memoryTypeIndex = FindMemoryType(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal)
		};

		texture.deviceMemory = device.allocateMemory(memAlloc);
		texture.image.bindMemory(texture.deviceMemory, 0);
		context.SetObjectDebugName(texture.deviceMemory, "BRDFLUT_ImageMemory");

		// Image view
		vk::ImageViewCreateInfo viewInfo{
			.image = texture.image,
			.viewType = vk::ImageViewType::e2D,
			.format = format,
			.subresourceRange = {
				.aspectMask = vk::ImageAspectFlagBits::eColor,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1
			}
		};

		texture.view = device.createImageView(viewInfo);
		context.SetObjectDebugName(texture.view, "BRDFLUT_ImageView");


		// Sampler
		vk::SamplerCreateInfo samplerInfo{
			.magFilter = vk::Filter::eLinear,
			.minFilter = vk::Filter::eLinear,
			.mipmapMode = vk::SamplerMipmapMode::eLinear,
			.addressModeU = vk::SamplerAddressMode::eClampToEdge,
			.addressModeV = vk::SamplerAddressMode::eClampToEdge,
			.addressModeW = vk::SamplerAddressMode::eClampToEdge,
			.minLod = 0.0f,
			.maxLod = 1.0f,
			.borderColor = vk::BorderColor::eFloatOpaqueWhite
		};

		texture.sampler = device.createSampler(samplerInfo);
		texture.width = dim;
		texture.height = dim;
		texture.mipLevels = 1;
		texture.layerCount = 1;
		texture.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		texture.UpdateDescriptor();

		context.SetObjectDebugName(texture.sampler, "BRDFLUT_Sampler");

		vk::AttachmentDescription attrDesc{
			.format = format,
			.samples = vk::SampleCountFlagBits::e1,
			.loadOp = vk::AttachmentLoadOp::eClear,
			.storeOp = vk::AttachmentStoreOp::eStore,
			.stencilLoadOp = vk::AttachmentLoadOp::eDontCare,
			.stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
			.initialLayout = vk::ImageLayout::eUndefined,
			.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal
		};

		// Color attachment
		vk::AttachmentReference colorReference{
			.attachment = 0, 
			.layout = vk::ImageLayout::eColorAttachmentOptimal 
		};

		vk::SubpassDescription subpassDescription{
			.pipelineBindPoint = vk::PipelineBindPoint::eGraphics,
			.colorAttachmentCount = 1,
			.pColorAttachments = &colorReference
		};

		// Use subpass dependencies for layout transitions
		std::array<vk::SubpassDependency, 2> dependencies;
		dependencies[0].srcSubpass = vk::SubpassExternal;
		dependencies[0].dstSubpass = 0;
		dependencies[0].srcStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
		dependencies[0].dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		dependencies[0].srcAccessMask = vk::AccessFlagBits::eMemoryRead;
		dependencies[0].dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
		dependencies[0].dependencyFlags = vk::DependencyFlagBits::eByRegion;
		dependencies[1].srcSubpass = 0;
		dependencies[1].dstSubpass = vk::SubpassExternal;
		dependencies[1].srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		dependencies[1].dstStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
		dependencies[1].srcAccessMask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
		dependencies[1].dstAccessMask = vk::AccessFlagBits::eMemoryRead;
		dependencies[1].dependencyFlags = vk::DependencyFlagBits::eByRegion;

		// Create the actual renderpass
		vk::RenderPassCreateInfo renderPassInfo{
			.attachmentCount = 1,
			.pAttachments = &attrDesc,
			.subpassCount = 1,
			.pSubpasses = &subpassDescription,
			.dependencyCount = static_cast<uint32_t>(dependencies.size()),
			.pDependencies = dependencies.data()
		};

		vk::raii::RenderPass renderpass = device.createRenderPass(renderPassInfo);
		context.SetObjectDebugName(renderpass, "BRDFLUT_Renderpass");

		vk::FramebufferCreateInfo framebufferInfo{
			.renderPass = *renderpass,
			.attachmentCount = 1,
			.pAttachments = &*texture.view,
			.width = dim,
			.height = dim,
			.layers = 1
		};

		vk::raii::Framebuffer framebuffer = device.createFramebuffer(framebufferInfo);
		context.SetObjectDebugName(framebuffer, "BRDFLUT_Framebuffer");

		// Descriptors
		std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings = {};

		vk::DescriptorSetLayoutCreateInfo descriptorsetlayoutInfo{
			.bindingCount = static_cast<uint32_t>(setLayoutBindings.size()),
			.pBindings = setLayoutBindings.data()
		};
		vk::raii::DescriptorSetLayout descriptorSetLayout = device.createDescriptorSetLayout(descriptorsetlayoutInfo);
		context.SetObjectDebugName(descriptorSetLayout, "BRDFLUT_DescriptorSetLayout");


		// Descriptor Pool
		std::vector<vk::DescriptorPoolSize> poolSizes = {
			vk::DescriptorPoolSize{
				.type = vk::DescriptorType::eCombinedImageSampler,
				.descriptorCount = 1
			}
		};
		vk::DescriptorPoolCreateInfo descriptorPoolInfo{
			.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
			.maxSets = 1,
			.poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
			.pPoolSizes = poolSizes.data()
		};
		vk::raii::DescriptorPool descriptorPool = device.createDescriptorPool(descriptorPoolInfo);

		// Descriptor sets
		vk::DescriptorSetAllocateInfo allocInfo{
			.descriptorPool = descriptorPool,
			.descriptorSetCount = 1,
			.pSetLayouts = &*descriptorSetLayout
		};

		vk::raii::DescriptorSet descriptorSet = nullptr;
		{
			std::vector<vk::raii::DescriptorSet> descriptorSets = device.allocateDescriptorSets(allocInfo);
			descriptorSet = std::move(descriptorSets[0]);
			context.SetObjectDebugName(descriptorSet, "BRDFLUT_DescriptorSet");
		}

		// Pipeline layout
		vk::PipelineLayoutCreateInfo pipelineLayoutInfo{
			.setLayoutCount = 1,
			.pSetLayouts = &*descriptorSetLayout
		};
		vk::raii::PipelineLayout pipelinelayout = device.createPipelineLayout(pipelineLayoutInfo);
		context.SetObjectDebugName(pipelinelayout, "BRDFLUT_PipelineLayout");

		// Pipeline
		vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState{
			.topology = vk::PrimitiveTopology::eTriangleList,
			.primitiveRestartEnable = VK_FALSE
		};

		vk::PipelineRasterizationStateCreateInfo rasterizationState{
			.polygonMode = vk::PolygonMode::eFill,
			.cullMode = vk::CullModeFlagBits::eNone,
			.frontFace = vk::FrontFace::eCounterClockwise,
			.lineWidth = 1.0f
		};

		vk::PipelineColorBlendAttachmentState blendAttachmentState{
			.blendEnable = vk::False,
			.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
		};

		vk::PipelineColorBlendStateCreateInfo colorBlendState{
			.attachmentCount = 1,
			.pAttachments = &blendAttachmentState,
		};

		vk::PipelineDepthStencilStateCreateInfo depthStencilState{
			.depthTestEnable = VK_FALSE,
			.depthWriteEnable = VK_FALSE,
			.depthCompareOp = vk::CompareOp::eLessOrEqual,
			.depthBoundsTestEnable = VK_FALSE,
			.stencilTestEnable = VK_FALSE,
			.minDepthBounds = 0.0f,
			.maxDepthBounds = 1.0f,
		};

		vk::PipelineViewportStateCreateInfo viewportState{
			.viewportCount = 1,
			.scissorCount = 1
		};

		vk::PipelineMultisampleStateCreateInfo multisampleState{
			.rasterizationSamples = vk::SampleCountFlagBits::e1,
			.sampleShadingEnable = VK_FALSE,
			.minSampleShading = 1.0f,
			.pSampleMask = nullptr,
			.alphaToCoverageEnable = VK_FALSE,
			.alphaToOneEnable = VK_FALSE
		};

		std::vector dynamicStates = {
			vk::DynamicState::eViewport,
			vk::DynamicState::eScissor
		};

		const vk::PipelineDynamicStateCreateInfo dynamicStateInfo{
			.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
			.pDynamicStates = dynamicStates.data()
		};

		vk::PipelineVertexInputStateCreateInfo emptyInputState{};

		Shader genbrdflutShader("assets/shaders/genbrdflut.spv", "GenBRDFLUT");
		const auto shaderStages = genbrdflutShader.GetPipelineShaderStageCreateInfo();

		vk::GraphicsPipelineCreateInfo pipelineInfo{};
		pipelineInfo.pInputAssemblyState = &inputAssemblyState;
		pipelineInfo.pRasterizationState = &rasterizationState;
		pipelineInfo.pColorBlendState = &colorBlendState;
		pipelineInfo.pMultisampleState = &multisampleState;
		pipelineInfo.pViewportState = &viewportState;
		pipelineInfo.pDepthStencilState = &depthStencilState;
		pipelineInfo.pDynamicState = &dynamicStateInfo;
		pipelineInfo.stageCount = 2;
		pipelineInfo.pStages = shaderStages.data();
		pipelineInfo.pVertexInputState = &emptyInputState;
		pipelineInfo.layout = *pipelinelayout;
		pipelineInfo.renderPass = renderpass;

		vk::raii::Pipeline pipeline = device.createGraphicsPipeline(nullptr, pipelineInfo);
		context.SetObjectDebugName(pipeline, "BRDFLUT_Pipeline");

		// Render
		vk::ClearValue clearValues[1];
		clearValues[0] = vk::ClearColorValue{ std::array{0.0f, 0.0f, 0.0f, 1.0f} };

		vk::RenderPassBeginInfo renderPassBeginInfo{
			.renderPass = *renderpass,
			.framebuffer = *framebuffer,
			.renderArea = {
				.offset = { .x = 0, .y = 0 },
				.extent = { .width = static_cast<uint32_t>(dim), .height = static_cast<uint32_t>(dim) }
			},
			.clearValueCount = 1,
			.pClearValues = clearValues,
		};
		renderPassBeginInfo.renderPass = *renderpass;
		renderPassBeginInfo.renderArea.extent.width = dim;
		renderPassBeginInfo.renderArea.extent.height = dim;
		renderPassBeginInfo.clearValueCount = 1;
		renderPassBeginInfo.pClearValues = clearValues;
		renderPassBeginInfo.framebuffer = *framebuffer;

		const auto cmd = context.BeginSingleTimeCommands();
		context.SetObjectDebugName(cmd, "BRDFLUT_CommandBuffer");

		cmd.beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);

		vk::Viewport viewport{
			.x = 0.0f,
			.y = 0.0f,
			.width = static_cast<float>(dim),
			.height = static_cast<float>(dim),
			.minDepth = 0.0f,
			.maxDepth = 1.0f
		};
		vk::Rect2D scissor{
			.offset = { .x = 0, .y = 0 },
			.extent = { .width = static_cast<uint32_t>(dim), .height = static_cast<uint32_t>(dim) }
		};
		cmd.setViewport(0, viewport);
		cmd.setScissor(0, scissor);

		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
		cmd.draw(3, 1, 0, 0);
		cmd.endRenderPass();

		context.EndSingleTimeCommands(cmd);

		auto tEnd = std::chrono::high_resolution_clock::now();
		auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
		KBR_CORE_INFO("Generating BRDF LUT took {} ms", tDiff);
	}

	void GenerateIrradianceCube(TextureCube& irradianceTexture, const vk::DescriptorImageInfo& envMapDescriptor, const Mesh& cubeMesh) 
	{
		auto tStart = std::chrono::high_resolution_clock::now();

		auto& context = VulkanContext::Get();
		const auto& device = context.GetDevice();

		constexpr vk::Format format = vk::Format::eR32G32B32A32Sfloat;
		constexpr int32_t dim = 64;
		const uint32_t numMips = static_cast<uint32_t>(floor(log2(dim))) + 1;

		irradianceTexture.mipLevels = numMips;
		irradianceTexture.height = dim;
		irradianceTexture.width = dim;

		// Image
		const vk::ImageCreateInfo imageInfo{
			.flags = vk::ImageCreateFlagBits::eCubeCompatible,
			.imageType = vk::ImageType::e2D,
			.format = format,
			.extent = {
				.width = dim,
				.height = dim,
				.depth = 1
			},
			.mipLevels = irradianceTexture.mipLevels,
			.arrayLayers = 6,
			.samples = vk::SampleCountFlagBits::e1,
			.tiling = vk::ImageTiling::eOptimal,
			.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
		};

		irradianceTexture.image = device.createImage(imageInfo);
		context.SetObjectDebugName(irradianceTexture.image, "IrradianceCube_Image");

		const vk::MemoryRequirements memReqs = irradianceTexture.image.getMemoryRequirements();
		const vk::MemoryAllocateInfo memAlloc{
			.allocationSize = memReqs.size,
			.memoryTypeIndex = FindMemoryType(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal)
		};

		irradianceTexture.deviceMemory = device.allocateMemory(memAlloc);
		irradianceTexture.image.bindMemory(irradianceTexture.deviceMemory, 0);
		context.SetObjectDebugName(irradianceTexture.deviceMemory, "IrradianceCube_ImageMemory");

		// Image view
		const vk::ImageViewCreateInfo viewInfo{
			.image = irradianceTexture.image,
			.viewType = vk::ImageViewType::eCube,
			.format = format,
			.subresourceRange = {
				.aspectMask = vk::ImageAspectFlagBits::eColor,
				.baseMipLevel = 0,
				.levelCount = numMips,
				.baseArrayLayer = 0,
				.layerCount = 6
			}
		};

		irradianceTexture.view = device.createImageView(viewInfo);
		context.SetObjectDebugName(irradianceTexture.view, "IrradianceCube_ImageView");

		// Sampler
		const vk::SamplerCreateInfo samplerInfo{
			.magFilter = vk::Filter::eLinear,
			.minFilter = vk::Filter::eLinear,
			.mipmapMode = vk::SamplerMipmapMode::eLinear,
			.addressModeU = vk::SamplerAddressMode::eClampToEdge,
			.addressModeV = vk::SamplerAddressMode::eClampToEdge,
			.addressModeW = vk::SamplerAddressMode::eClampToEdge,
			.minLod = 0.0f,
			.maxLod = static_cast<float>(numMips),
			.borderColor = vk::BorderColor::eFloatOpaqueWhite
		};

		irradianceTexture.sampler = device.createSampler(samplerInfo);
		irradianceTexture.width = dim;
		irradianceTexture.height = dim;
		irradianceTexture.layerCount = 6;
		irradianceTexture.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		irradianceTexture.UpdateDescriptor();
		context.SetObjectDebugName(irradianceTexture.sampler, "IrradianceCube_Sampler");

		vk::AttachmentDescription attrDesc{
			.format = format,
			.samples = vk::SampleCountFlagBits::e1,
			.loadOp = vk::AttachmentLoadOp::eClear,
			.storeOp = vk::AttachmentStoreOp::eStore,
			.stencilLoadOp = vk::AttachmentLoadOp::eDontCare,
			.stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
			.initialLayout = vk::ImageLayout::eUndefined,
			.finalLayout = vk::ImageLayout::eColorAttachmentOptimal
		};

		// Color attachment
		vk::AttachmentReference colorReference{
			.attachment = 0,
			.layout = vk::ImageLayout::eColorAttachmentOptimal
		};

		vk::SubpassDescription subpassDescription{
			.pipelineBindPoint = vk::PipelineBindPoint::eGraphics,
			.colorAttachmentCount = 1,
			.pColorAttachments = &colorReference
		};

		// Use subpass dependencies for layout transitions
		std::array<vk::SubpassDependency, 2> dependencies;
		dependencies[0].srcSubpass = vk::SubpassExternal;
		dependencies[0].dstSubpass = 0;
		dependencies[0].srcStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
		dependencies[0].dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		dependencies[0].srcAccessMask = vk::AccessFlagBits::eMemoryRead;
		dependencies[0].dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
		dependencies[0].dependencyFlags = vk::DependencyFlagBits::eByRegion;
		dependencies[1].srcSubpass = 0;
		dependencies[1].dstSubpass = vk::SubpassExternal;
		dependencies[1].srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		dependencies[1].dstStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
		dependencies[1].srcAccessMask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
		dependencies[1].dstAccessMask = vk::AccessFlagBits::eMemoryRead;
		dependencies[1].dependencyFlags = vk::DependencyFlagBits::eByRegion;

		// Create the actual renderpass
		vk::RenderPassCreateInfo renderPassInfo{
			.attachmentCount = 1,
			.pAttachments = &attrDesc,
			.subpassCount = 1,
			.pSubpasses = &subpassDescription,
			.dependencyCount = static_cast<uint32_t>(dependencies.size()),
			.pDependencies = dependencies.data()
		};

		vk::raii::RenderPass renderpass = device.createRenderPass(renderPassInfo);
		context.SetObjectDebugName(renderpass, "IrradianceCube_Renderpass");

		struct OffscreenResources
		{
			vk::raii::Image image = nullptr;
			vk::raii::ImageView view = nullptr;
			vk::raii::DeviceMemory memory = nullptr;
			vk::raii::Framebuffer framebuffer = nullptr;
		};
		OffscreenResources offscreen;

		// Offscreen framebuffer
		{
			// Color attachment
			vk::ImageCreateInfo offscreenImageInfo{
				.imageType = vk::ImageType::e2D,
				.format = format,
				.extent = {
					.width = dim,
					.height = dim,
					.depth = 1
				},
				.mipLevels = 1,
				.arrayLayers = 1,
				.samples = vk::SampleCountFlagBits::e1,
				.tiling = vk::ImageTiling::eOptimal,
				.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc,
			};
			offscreen.image = device.createImage(offscreenImageInfo);
			context.SetObjectDebugName(offscreen.image, "IrradianceCube_OffscreenImage");

			vk::MemoryRequirements offscreenMemReqs = offscreen.image.getMemoryRequirements();
			vk::MemoryAllocateInfo offscreenMemAlloc{
				.allocationSize = offscreenMemReqs.size,
				.memoryTypeIndex = FindMemoryType(offscreenMemReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal)
			};
			offscreen.memory = device.allocateMemory(offscreenMemAlloc);
			offscreen.image.bindMemory(*offscreen.memory, 0);
			context.SetObjectDebugName(offscreen.memory, "IrradianceCube_OffscreenImageMemory");

			const vk::ImageViewCreateInfo offscreenViewInfo{
				.image = *offscreen.image,
				.viewType = vk::ImageViewType::e2D,
				.format = format,
				.subresourceRange = {
					.aspectMask = vk::ImageAspectFlagBits::eColor,
					.baseMipLevel = 0,
					.levelCount = 1,
					.baseArrayLayer = 0,
					.layerCount = 1
				}
			};
			offscreen.view = device.createImageView(offscreenViewInfo);
			context.SetObjectDebugName(offscreen.view, "IrradianceCube_OffscreenImageView");

			vk::FramebufferCreateInfo offscreenFramebufferInfo{
				.renderPass = *renderpass,
				.attachmentCount = 1,
				.pAttachments = &*offscreen.view,
				.width = dim,
				.height = dim,
				.layers = 1
			};
			offscreen.framebuffer = device.createFramebuffer(offscreenFramebufferInfo);
			context.SetObjectDebugName(offscreen.framebuffer, "IrradianceCube_OffscreenFramebuffer");

			const auto cmd = context.BeginSingleTimeCommands();
			context.SetObjectDebugName(cmd, "IrradianceCube_OffscreenImageLayoutCmd");

			context.TransitionImageLayout(
				cmd,
				offscreen.image,
				vk::ImageLayout::eUndefined,
				vk::ImageLayout::eColorAttachmentOptimal,
				vk::ImageSubresourceRange{
					.aspectMask = vk::ImageAspectFlagBits::eColor,
					.baseMipLevel = 0,
					.levelCount = 1,
					.baseArrayLayer = 0,
					.layerCount = 1
				}
			);
			context.EndSingleTimeCommands(cmd);
		}

		// Descriptors
		vk::raii::DescriptorSetLayout descriptorSetLayout = nullptr;
		{
			std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings = {
				vk::DescriptorSetLayoutBinding{
					.binding = 0,
					.descriptorType = vk::DescriptorType::eCombinedImageSampler,
					.descriptorCount = 1,
					.stageFlags = vk::ShaderStageFlagBits::eFragment
				}
			};
			vk::DescriptorSetLayoutCreateInfo descriptorsetlayoutInfo{
				.bindingCount = static_cast<uint32_t>(setLayoutBindings.size()),
				.pBindings = setLayoutBindings.data()
			};
			descriptorSetLayout = device.createDescriptorSetLayout(descriptorsetlayoutInfo);
			context.SetObjectDebugName(descriptorSetLayout, "IrradianceCube_DescriptorSetLayout");
		}

		// Descriptor Pool
		vk::raii::DescriptorPool descriptorPool = nullptr;
		{
			std::vector<vk::DescriptorPoolSize> poolSizes = {
				vk::DescriptorPoolSize{
					.type = vk::DescriptorType::eCombinedImageSampler,
					.descriptorCount = 1
				}
			};
			vk::DescriptorPoolCreateInfo descriptorPoolInfo{
				.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
				.maxSets = 1,
				.poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
				.pPoolSizes = poolSizes.data()
			};
			descriptorPool = device.createDescriptorPool(descriptorPoolInfo);
			context.SetObjectDebugName(descriptorPool, "IrradianceCube_DescriptorPool");
		}

		// Descriptor sets
		vk::raii::DescriptorSet descriptorSet = nullptr;
		{
			vk::DescriptorSetAllocateInfo allocInfo{
				.descriptorPool = *descriptorPool,
				.descriptorSetCount = 1,
				.pSetLayouts = &*descriptorSetLayout
			};
			std::vector<vk::raii::DescriptorSet> descriptorSets = device.allocateDescriptorSets(allocInfo);
			descriptorSet = std::move(descriptorSets[0]);
			context.SetObjectDebugName(descriptorSet, "IrradianceCube_DescriptorSet");

			vk::WriteDescriptorSet writeDescriptorSet{
				.dstSet = *descriptorSet,
				.dstBinding = 0,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = vk::DescriptorType::eCombinedImageSampler,
				.pImageInfo = &envMapDescriptor
			};
			device.updateDescriptorSets(writeDescriptorSet, nullptr);
		}

		// Pipeline layout
		struct PushBlock
		{
			glm::mat4 mvp;
			// Sampling deltas
			float deltaPhi = (2.0f * static_cast<float>(std::numbers::pi)) / 180.0f;
			float deltaTheta = (0.5f * static_cast<float>(std::numbers::pi)) / 64.0f;
		} pushBlock;

		vk::raii::PipelineLayout pipelinelayout = nullptr;
		{
			vk::PushConstantRange pushConstantRange{
				.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
				.offset = 0,
				.size = sizeof(PushBlock)
			};
			vk::PipelineLayoutCreateInfo pipelineLayoutInfo{
				.setLayoutCount = 1,
				.pSetLayouts = &*descriptorSetLayout,
				.pushConstantRangeCount = 1,
				.pPushConstantRanges = &pushConstantRange
			};
			pipelinelayout = device.createPipelineLayout(pipelineLayoutInfo);
			context.SetObjectDebugName(pipelinelayout, "IrradianceCube_PipelineLayout");
		}

		// Pipeline
		vk::raii::Pipeline pipeline = nullptr;
		{
			vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState{
				.topology = vk::PrimitiveTopology::eTriangleList,
				.primitiveRestartEnable = VK_FALSE
			};

			vk::PipelineRasterizationStateCreateInfo rasterizationState{
				.polygonMode = vk::PolygonMode::eFill,
				.cullMode = vk::CullModeFlagBits::eNone,
				.frontFace = vk::FrontFace::eCounterClockwise,
				.lineWidth = 1.0f
			};

			vk::PipelineColorBlendAttachmentState blendAttachmentState{
				.blendEnable = vk::False,
				.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
			};

			vk::PipelineColorBlendStateCreateInfo colorBlendState{
				.attachmentCount = 1,
				.pAttachments = &blendAttachmentState,
			};

			vk::PipelineDepthStencilStateCreateInfo depthStencilState{
				.depthTestEnable = VK_FALSE,
				.depthWriteEnable = VK_FALSE,
				.depthCompareOp = vk::CompareOp::eLessOrEqual,
				.depthBoundsTestEnable = VK_FALSE,
				.stencilTestEnable = VK_FALSE,
				.minDepthBounds = 0.0f,
				.maxDepthBounds = 1.0f,
			};

			vk::PipelineViewportStateCreateInfo viewportState{
				.viewportCount = 1,
				.scissorCount = 1
			};

			vk::PipelineMultisampleStateCreateInfo multisampleState{
				.rasterizationSamples = vk::SampleCountFlagBits::e1,
				.sampleShadingEnable = VK_FALSE,
				.minSampleShading = 1.0f,
				.pSampleMask = nullptr,
				.alphaToCoverageEnable = VK_FALSE,
				.alphaToOneEnable = VK_FALSE
			};

			std::vector dynamicStates = {
				vk::DynamicState::eViewport,
				vk::DynamicState::eScissor
			};

			const vk::PipelineDynamicStateCreateInfo dynamicStateInfo{
				.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
				.pDynamicStates = dynamicStates.data()
			};

			const auto bindingDesc = Vertex::GetBindingDescription();
			const auto attributeDescs = Vertex::GetAttributeDescriptions();

			vk::PipelineVertexInputStateCreateInfo inputStateInfo{
				.vertexBindingDescriptionCount = 1,
				.pVertexBindingDescriptions = &bindingDesc,
				.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescs.size()),
				.pVertexAttributeDescriptions = attributeDescs.data()
			};

			Shader irradianceCubeShader("assets/shaders/irradiancecube.spv", "IrradianceCube");
			const auto shaderStages = irradianceCubeShader.GetPipelineShaderStageCreateInfo();

			vk::GraphicsPipelineCreateInfo pipelineInfo{};
			pipelineInfo.pInputAssemblyState = &inputAssemblyState;
			pipelineInfo.pRasterizationState = &rasterizationState;
			pipelineInfo.pColorBlendState = &colorBlendState;
			pipelineInfo.pMultisampleState = &multisampleState;
			pipelineInfo.pViewportState = &viewportState;
			pipelineInfo.pDepthStencilState = &depthStencilState;
			pipelineInfo.pDynamicState = &dynamicStateInfo;
			pipelineInfo.stageCount = 2;
			pipelineInfo.pStages = shaderStages.data();
			pipelineInfo.pVertexInputState = &inputStateInfo;
			pipelineInfo.layout = *pipelinelayout;
			pipelineInfo.renderPass = renderpass;

			pipeline = device.createGraphicsPipeline(nullptr, pipelineInfo);
			context.SetObjectDebugName(pipeline, "IrradianceCube_Pipeline");
		}

		vk::ClearValue clearValues[1];
		clearValues[0] = vk::ClearColorValue{ std::array{0.0f, 0.0f, 0.2f, 0.0f} };

		vk::RenderPassBeginInfo renderPassBeginInfo{
			.renderPass = *renderpass,
			.framebuffer = *offscreen.framebuffer,
			.renderArea = {
				.offset = { .x = 0, .y = 0 },
				.extent = { .width = static_cast<uint32_t>(dim), .height = static_cast<uint32_t>(dim) }
			},
			.clearValueCount = 1,
			.pClearValues = clearValues,
		};

		std::vector<glm::mat4> matrices = {
			// POSITIVE_X
			glm::rotate(glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f)), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
			// NEGATIVE_X
			glm::rotate(glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(0.0f, 1.0f, 0.0f)), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
			// POSITIVE_Y
			glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
			// NEGATIVE_Y
			glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
			// POSITIVE_Z
			glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
			// NEGATIVE_Z
			glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
		};

		const auto cmd = context.BeginSingleTimeCommands();
		context.SetObjectDebugName(cmd, "IrradianceCube_RenderCommandBuffer");

		vk::Viewport viewport{
			.x = 0.0f,
			.y = 0.0f,
			.width = static_cast<float>(dim),
			.height = static_cast<float>(dim),
			.minDepth = 0.0f,
			.maxDepth = 1.0f
		};

		vk::Rect2D scissor{
			.offset = { .x = 0, .y = 0 },
			.extent = { .width = static_cast<uint32_t>(dim), .height = static_cast<uint32_t>(dim) }
		};

		cmd.setViewport(0, viewport);
		cmd.setScissor(0, scissor);

		vk::ImageSubresourceRange irradianceSubresourceRange{
			.aspectMask = vk::ImageAspectFlagBits::eColor,
			.baseMipLevel = 0,
			.levelCount = irradianceTexture.mipLevels,
			.baseArrayLayer = 0,
			.layerCount = 6
		};

		context.TransitionImageLayout(
			cmd,
			irradianceTexture.image,
			vk::ImageLayout::eUndefined,
			vk::ImageLayout::eTransferDstOptimal,
			irradianceSubresourceRange
		);

		for (uint32_t m = 0; m < numMips; m++) {
			for (uint32_t f = 0; f < 6; f++) {
				viewport.width = static_cast<float>(dim * std::pow(0.5f, m));
				viewport.height = static_cast<float>(dim * std::pow(0.5f, m));
				cmd.setViewport(0, viewport);

				// Render scene from cube face's point of view
				cmd.beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);

				// Update shader push constant block
				pushBlock.mvp = glm::perspective(static_cast<float>(std::numbers::pi / 2.0), 1.0f, 0.1f, 512.0f) * matrices[f];

				cmd.pushConstants<PushBlock>(*pipelinelayout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, pushBlock);

				cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
				cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelinelayout, 0, *descriptorSet, {});

				cubeMesh.Draw(cmd);

				cmd.endRenderPass();

				constexpr vk::ImageSubresourceRange offscreenSubresourceRange{
					.aspectMask = vk::ImageAspectFlagBits::eColor,
					.baseMipLevel = 0,
					.levelCount = 1,
					.baseArrayLayer = 0,
					.layerCount = 1
				};

				context.TransitionImageLayout(
					cmd,
					offscreen.image,
					vk::ImageLayout::eColorAttachmentOptimal,
					vk::ImageLayout::eTransferSrcOptimal,
					offscreenSubresourceRange
				);

				// Copy region for transfer from framebuffer to cube face
				vk::ImageCopy copyRegion = {};

				copyRegion.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
				copyRegion.srcSubresource.baseArrayLayer = 0;
				copyRegion.srcSubresource.mipLevel = 0;
				copyRegion.srcSubresource.layerCount = 1;
				copyRegion.srcOffset = { .x = 0, .y = 0, .z = 0 };

				copyRegion.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
				copyRegion.dstSubresource.baseArrayLayer = f;
				copyRegion.dstSubresource.mipLevel = m;
				copyRegion.dstSubresource.layerCount = 1;
				copyRegion.dstOffset = { .x = 0, .y = 0, .z = 0 };

				copyRegion.extent.width = static_cast<uint32_t>(viewport.width);
				copyRegion.extent.height = static_cast<uint32_t>(viewport.height);
				copyRegion.extent.depth = 1;

				cmd.copyImage(
					offscreen.image,
					vk::ImageLayout::eTransferSrcOptimal,
					irradianceTexture.image,
					vk::ImageLayout::eTransferDstOptimal,
					copyRegion);

				// Transform framebuffer color attachment back
				context.TransitionImageLayout(
					cmd,
					offscreen.image,
					vk::ImageLayout::eTransferSrcOptimal,
					vk::ImageLayout::eColorAttachmentOptimal,
					offscreenSubresourceRange
				);
			}
		}

		context.TransitionImageLayout(
			cmd,
			irradianceTexture.image,
			vk::ImageLayout::eTransferDstOptimal,
			vk::ImageLayout::eShaderReadOnlyOptimal,
			irradianceSubresourceRange
		);

		context.EndSingleTimeCommands(cmd);

		auto tEnd = std::chrono::high_resolution_clock::now();
		auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
		KBR_CORE_INFO("Generating irradiance cube with {} mip levels took {} ms", numMips, tDiff);
	}

	void GeneratePrefilteredEnvMap(TextureCube& prefilteredEnvMap, const vk::DescriptorImageInfo& envMapDescriptor,
		const Mesh& cubeMesh) 
	{
		auto tStart = std::chrono::high_resolution_clock::now();

		auto& context = VulkanContext::Get();
		const auto& device = context.GetDevice();

		constexpr vk::Format format = vk::Format::eR16G16B16A16Sfloat;
		constexpr int32_t dim = 512;
		const uint32_t numMips = static_cast<uint32_t>(floor(log2(dim))) + 1;

		prefilteredEnvMap.mipLevels = numMips;
		prefilteredEnvMap.height = dim;
		prefilteredEnvMap.width = dim;

		// Image
		const vk::ImageCreateInfo imageInfo{
			.flags = vk::ImageCreateFlagBits::eCubeCompatible,
			.imageType = vk::ImageType::e2D,
			.format = format,
			.extent = {
				.width = dim,
				.height = dim,
				.depth = 1
			},
			.mipLevels = prefilteredEnvMap.mipLevels,
			.arrayLayers = 6,
			.samples = vk::SampleCountFlagBits::e1,
			.tiling = vk::ImageTiling::eOptimal,
			.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
		};

		prefilteredEnvMap.image = device.createImage(imageInfo);
		context.SetObjectDebugName(prefilteredEnvMap.image, "PrefilteredEnvMap_Image");

		const vk::MemoryRequirements memReqs = prefilteredEnvMap.image.getMemoryRequirements();
		const vk::MemoryAllocateInfo memAlloc{
			.allocationSize = memReqs.size,
			.memoryTypeIndex = FindMemoryType(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal)
		};

		prefilteredEnvMap.deviceMemory = device.allocateMemory(memAlloc);
		prefilteredEnvMap.image.bindMemory(prefilteredEnvMap.deviceMemory, 0);
		context.SetObjectDebugName(prefilteredEnvMap.deviceMemory, "PrefilteredEnvMap_ImageMemory");

		// Image view
		const vk::ImageViewCreateInfo viewInfo{
			.image = prefilteredEnvMap.image,
			.viewType = vk::ImageViewType::eCube,
			.format = format,
			.subresourceRange = {
				.aspectMask = vk::ImageAspectFlagBits::eColor,
				.baseMipLevel = 0,
				.levelCount = numMips,
				.baseArrayLayer = 0,
				.layerCount = 6
			}
		};

		prefilteredEnvMap.view = device.createImageView(viewInfo);
		context.SetObjectDebugName(prefilteredEnvMap.view, "PrefilteredEnvMap_ImageView");

		// Sampler
		const vk::SamplerCreateInfo samplerInfo{
			.magFilter = vk::Filter::eLinear,
			.minFilter = vk::Filter::eLinear,
			.mipmapMode = vk::SamplerMipmapMode::eLinear,
			.addressModeU = vk::SamplerAddressMode::eClampToEdge,
			.addressModeV = vk::SamplerAddressMode::eClampToEdge,
			.addressModeW = vk::SamplerAddressMode::eClampToEdge,
			.minLod = 0.0f,
			.maxLod = static_cast<float>(numMips),
			.borderColor = vk::BorderColor::eFloatOpaqueWhite
		};

		prefilteredEnvMap.sampler = device.createSampler(samplerInfo);
		prefilteredEnvMap.layerCount = 6;
		prefilteredEnvMap.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		prefilteredEnvMap.UpdateDescriptor();
		context.SetObjectDebugName(prefilteredEnvMap.sampler, "PrefilteredEnvMap_Sampler");

		vk::AttachmentDescription attrDesc{
			.format = format,
			.samples = vk::SampleCountFlagBits::e1,
			.loadOp = vk::AttachmentLoadOp::eClear,
			.storeOp = vk::AttachmentStoreOp::eStore,
			.stencilLoadOp = vk::AttachmentLoadOp::eDontCare,
			.stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
			.initialLayout = vk::ImageLayout::eUndefined,
			.finalLayout = vk::ImageLayout::eColorAttachmentOptimal
		};

		// Color attachment
		vk::AttachmentReference colorReference{
			.attachment = 0,
			.layout = vk::ImageLayout::eColorAttachmentOptimal
		};

		vk::SubpassDescription subpassDescription{
			.pipelineBindPoint = vk::PipelineBindPoint::eGraphics,
			.colorAttachmentCount = 1,
			.pColorAttachments = &colorReference
		};

		// Use subpass dependencies for layout transitions
		std::array<vk::SubpassDependency, 2> dependencies;
		dependencies[0].srcSubpass = vk::SubpassExternal;
		dependencies[0].dstSubpass = 0;
		dependencies[0].srcStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
		dependencies[0].dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		dependencies[0].srcAccessMask = vk::AccessFlagBits::eMemoryRead;
		dependencies[0].dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
		dependencies[0].dependencyFlags = vk::DependencyFlagBits::eByRegion;
		dependencies[1].srcSubpass = 0;
		dependencies[1].dstSubpass = vk::SubpassExternal;
		dependencies[1].srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		dependencies[1].dstStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
		dependencies[1].srcAccessMask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
		dependencies[1].dstAccessMask = vk::AccessFlagBits::eMemoryRead;
		dependencies[1].dependencyFlags = vk::DependencyFlagBits::eByRegion;

		// Create the actual renderpass
		vk::RenderPassCreateInfo renderPassInfo{
			.attachmentCount = 1,
			.pAttachments = &attrDesc,
			.subpassCount = 1,
			.pSubpasses = &subpassDescription,
			.dependencyCount = static_cast<uint32_t>(dependencies.size()),
			.pDependencies = dependencies.data()
		};

		vk::raii::RenderPass renderpass = device.createRenderPass(renderPassInfo);
		context.SetObjectDebugName(renderpass, "PrefilteredEnvMap_Renderpass");

		struct OffscreenResources
		{
			vk::raii::Image image = nullptr;
			vk::raii::ImageView view = nullptr;
			vk::raii::DeviceMemory memory = nullptr;
			vk::raii::Framebuffer framebuffer = nullptr;
		};
		OffscreenResources offscreen;

		// Offscreen framebuffer
		{
			// Color attachment
			vk::ImageCreateInfo offscreenImageInfo{
				.imageType = vk::ImageType::e2D,
				.format = format,
				.extent = {
					.width = dim,
					.height = dim,
					.depth = 1
				},
				.mipLevels = 1,
				.arrayLayers = 1,
				.samples = vk::SampleCountFlagBits::e1,
				.tiling = vk::ImageTiling::eOptimal,
				.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc,
			};
			offscreen.image = device.createImage(offscreenImageInfo);
			context.SetObjectDebugName(offscreen.image, "PrefilteredEnvMap_OffscreenImage");

			vk::MemoryRequirements offscreenMemReqs = offscreen.image.getMemoryRequirements();
			vk::MemoryAllocateInfo offscreenMemAlloc{
				.allocationSize = offscreenMemReqs.size,
				.memoryTypeIndex = FindMemoryType(offscreenMemReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal)
			};
			offscreen.memory = device.allocateMemory(offscreenMemAlloc);
			offscreen.image.bindMemory(*offscreen.memory, 0);
			context.SetObjectDebugName(offscreen.memory, "PrefilteredEnvMap_OffscreenImageMemory");

			const vk::ImageViewCreateInfo offscreenViewInfo{
				.image = *offscreen.image,
				.viewType = vk::ImageViewType::e2D,
				.format = format,
				.subresourceRange = {
					.aspectMask = vk::ImageAspectFlagBits::eColor,
					.baseMipLevel = 0,
					.levelCount = 1,
					.baseArrayLayer = 0,
					.layerCount = 1
				}
			};
			offscreen.view = device.createImageView(offscreenViewInfo);
			context.SetObjectDebugName(offscreen.view, "PrefilteredEnvMap_OffscreenImageView");

			vk::FramebufferCreateInfo offscreenFramebufferInfo{
				.renderPass = *renderpass,
				.attachmentCount = 1,
				.pAttachments = &*offscreen.view,
				.width = dim,
				.height = dim,
				.layers = 1
			};
			offscreen.framebuffer = device.createFramebuffer(offscreenFramebufferInfo);
			context.SetObjectDebugName(offscreen.framebuffer, "PrefilteredEnvMap_OffscreenFramebuffer");

			const auto cmd = context.BeginSingleTimeCommands();
			context.SetObjectDebugName(cmd, "PrefilteredEnvMap_OffscreenCommandBuffer");

			context.TransitionImageLayout(
				cmd,
				offscreen.image,
				vk::ImageLayout::eUndefined,
				vk::ImageLayout::eColorAttachmentOptimal,
				vk::ImageSubresourceRange{
					.aspectMask = vk::ImageAspectFlagBits::eColor,
					.baseMipLevel = 0,
					.levelCount = 1,
					.baseArrayLayer = 0,
					.layerCount = 1
				}
			);
			context.EndSingleTimeCommands(cmd);
		}

		// Descriptors
		vk::raii::DescriptorSetLayout descriptorSetLayout = nullptr;
		{
			std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings = {
				vk::DescriptorSetLayoutBinding{
					.binding = 0,
					.descriptorType = vk::DescriptorType::eCombinedImageSampler,
					.descriptorCount = 1,
					.stageFlags = vk::ShaderStageFlagBits::eFragment
				}
			};
			vk::DescriptorSetLayoutCreateInfo descriptorsetlayoutInfo{
				.bindingCount = static_cast<uint32_t>(setLayoutBindings.size()),
				.pBindings = setLayoutBindings.data()
			};
			descriptorSetLayout = device.createDescriptorSetLayout(descriptorsetlayoutInfo);
			context.SetObjectDebugName(descriptorSetLayout, "PrefilteredEnvMap_DescriptorSetLayout");
		}

		// Descriptor Pool
		vk::raii::DescriptorPool descriptorPool = nullptr;
		{
			std::vector<vk::DescriptorPoolSize> poolSizes = {
				vk::DescriptorPoolSize{
					.type = vk::DescriptorType::eCombinedImageSampler,
					.descriptorCount = 1
				}
			};
			vk::DescriptorPoolCreateInfo descriptorPoolInfo{
				.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
				.maxSets = 1,
				.poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
				.pPoolSizes = poolSizes.data()
			};
			descriptorPool = device.createDescriptorPool(descriptorPoolInfo);
			context.SetObjectDebugName(descriptorPool, "PrefilteredEnvMap_DescriptorPool");
		}

		// Descriptor sets
		vk::raii::DescriptorSet descriptorSet = nullptr;
		{
			vk::DescriptorSetAllocateInfo allocInfo{
				.descriptorPool = *descriptorPool,
				.descriptorSetCount = 1,
				.pSetLayouts = &*descriptorSetLayout
			};
			std::vector<vk::raii::DescriptorSet> descriptorSets = device.allocateDescriptorSets(allocInfo);
			descriptorSet = std::move(descriptorSets[0]);
			context.SetObjectDebugName(descriptorSet, "PrefilteredEnvMap_DescriptorSet");

			vk::WriteDescriptorSet writeDescriptorSet{
				.dstSet = *descriptorSet,
				.dstBinding = 0,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = vk::DescriptorType::eCombinedImageSampler,
				.pImageInfo = &envMapDescriptor
			};
			device.updateDescriptorSets(writeDescriptorSet, nullptr);
		}

		// Pipeline layout
		struct PushBlock
		{
			glm::mat4 mvp;
			float roughness;
			uint32_t numSamples = 32u;
		} pushBlock;

		vk::raii::PipelineLayout pipelinelayout = nullptr;
		{
			vk::PushConstantRange pushConstantRange{
				.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
				.offset = 0,
				.size = sizeof(PushBlock)
			};
			vk::PipelineLayoutCreateInfo pipelineLayoutInfo{
				.setLayoutCount = 1,
				.pSetLayouts = &*descriptorSetLayout,
				.pushConstantRangeCount = 1,
				.pPushConstantRanges = &pushConstantRange
			};
			pipelinelayout = device.createPipelineLayout(pipelineLayoutInfo);
			context.SetObjectDebugName(pipelinelayout, "PrefilteredEnvMap_PipelineLayout");
		}

		// Pipeline
		vk::raii::Pipeline pipeline = nullptr;
		{
			vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState{
				.topology = vk::PrimitiveTopology::eTriangleList,
				.primitiveRestartEnable = VK_FALSE
			};

			vk::PipelineRasterizationStateCreateInfo rasterizationState{
				.polygonMode = vk::PolygonMode::eFill,
				.cullMode = vk::CullModeFlagBits::eNone,
				.frontFace = vk::FrontFace::eCounterClockwise,
				.lineWidth = 1.0f
			};

			vk::PipelineColorBlendAttachmentState blendAttachmentState{
				.blendEnable = vk::False,
				.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
			};

			vk::PipelineColorBlendStateCreateInfo colorBlendState{
				.attachmentCount = 1,
				.pAttachments = &blendAttachmentState,
			};

			vk::PipelineDepthStencilStateCreateInfo depthStencilState{
				.depthTestEnable = VK_FALSE,
				.depthWriteEnable = VK_FALSE,
				.depthCompareOp = vk::CompareOp::eLessOrEqual,
				.depthBoundsTestEnable = VK_FALSE,
				.stencilTestEnable = VK_FALSE,
				.minDepthBounds = 0.0f,
				.maxDepthBounds = 1.0f,
			};

			vk::PipelineViewportStateCreateInfo viewportState{
				.viewportCount = 1,
				.scissorCount = 1
			};

			vk::PipelineMultisampleStateCreateInfo multisampleState{
				.rasterizationSamples = vk::SampleCountFlagBits::e1,
				.sampleShadingEnable = VK_FALSE,
				.minSampleShading = 1.0f,
				.pSampleMask = nullptr,
				.alphaToCoverageEnable = VK_FALSE,
				.alphaToOneEnable = VK_FALSE
			};

			std::vector dynamicStates = {
				vk::DynamicState::eViewport,
				vk::DynamicState::eScissor
			};

			const vk::PipelineDynamicStateCreateInfo dynamicStateInfo{
				.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
				.pDynamicStates = dynamicStates.data()
			};

			const auto bindingDesc = Vertex::GetBindingDescription();
			const auto attributeDescs = Vertex::GetAttributeDescriptions();

			vk::PipelineVertexInputStateCreateInfo inputStateInfo{
				.vertexBindingDescriptionCount = 1,
				.pVertexBindingDescriptions = &bindingDesc,
				.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescs.size()),
				.pVertexAttributeDescriptions = attributeDescs.data()
			};

			Shader prefilterEnvMapShader("assets/shaders/prefilterenvmap.spv", "PrefilterEnvMap");
			const auto shaderStages = prefilterEnvMapShader.GetPipelineShaderStageCreateInfo();

			vk::GraphicsPipelineCreateInfo pipelineInfo{};
			pipelineInfo.pInputAssemblyState = &inputAssemblyState;
			pipelineInfo.pRasterizationState = &rasterizationState;
			pipelineInfo.pColorBlendState = &colorBlendState;
			pipelineInfo.pMultisampleState = &multisampleState;
			pipelineInfo.pViewportState = &viewportState;
			pipelineInfo.pDepthStencilState = &depthStencilState;
			pipelineInfo.pDynamicState = &dynamicStateInfo;
			pipelineInfo.stageCount = 2;
			pipelineInfo.pStages = shaderStages.data();
			pipelineInfo.pVertexInputState = &inputStateInfo;
			pipelineInfo.layout = *pipelinelayout;
			pipelineInfo.renderPass = renderpass;

			pipeline = device.createGraphicsPipeline(nullptr, pipelineInfo);
			context.SetObjectDebugName(pipeline, "PrefilteredEnvMap_Pipeline");
		}

		vk::ClearValue clearValues[1];
		clearValues[0] = vk::ClearColorValue{ std::array{0.0f, 0.0f, 0.2f, 0.0f} };

		vk::RenderPassBeginInfo renderPassBeginInfo{
			.renderPass = *renderpass,
			.framebuffer = *offscreen.framebuffer,
			.renderArea = {
				.offset = { .x = 0, .y = 0 },
				.extent = { .width = static_cast<uint32_t>(dim), .height = static_cast<uint32_t>(dim) }
			},
			.clearValueCount = 1,
			.pClearValues = clearValues,
		};

		std::vector<glm::mat4> matrices = {
			// POSITIVE_X
			glm::rotate(glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f)), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
			// NEGATIVE_X
			glm::rotate(glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(0.0f, 1.0f, 0.0f)), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
			// POSITIVE_Y
			glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
			// NEGATIVE_Y
			glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
			// POSITIVE_Z
			glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
			// NEGATIVE_Z
			glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
		};

		const auto cmd = context.BeginSingleTimeCommands();
		context.SetObjectDebugName(cmd, "PrefilteredEnvMap_RenderCommandBuffer");

		vk::Viewport viewport{
			.x = 0.0f,
			.y = 0.0f,
			.width = static_cast<float>(dim),
			.height = static_cast<float>(dim),
			.minDepth = 0.0f,
			.maxDepth = 1.0f
		};
		vk::Rect2D scissor{
			.offset = { .x = 0, .y = 0 },
			.extent = { .width = static_cast<uint32_t>(dim), .height = static_cast<uint32_t>(dim) }
		};

		cmd.setViewport(0, viewport);
		cmd.setScissor(0, scissor);

		vk::ImageSubresourceRange prefilterSubresourceRange{
			.aspectMask = vk::ImageAspectFlagBits::eColor,
			.baseMipLevel = 0,
			.levelCount = prefilteredEnvMap.mipLevels,
			.baseArrayLayer = 0,
			.layerCount = 6
		};

		context.TransitionImageLayout(
			cmd,
			prefilteredEnvMap.image,
			vk::ImageLayout::eUndefined,
			vk::ImageLayout::eTransferDstOptimal,
			prefilterSubresourceRange
		);

		for (uint32_t m = 0; m < numMips; m++) 
		{
			pushBlock.roughness = static_cast<float>(m) / static_cast<float>(numMips - 1);

			for (uint32_t f = 0; f < 6; f++) 
			{
				viewport.width = static_cast<float>(dim * std::pow(0.5f, m));
				viewport.height = static_cast<float>(dim * std::pow(0.5f, m));
				cmd.setViewport(0, viewport);

				// Render scene from cube face's point of view
				cmd.beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);

				// Update shader push constant block
				pushBlock.mvp = glm::perspective(static_cast<float>(std::numbers::pi / 2.0), 1.0f, 0.1f, 512.0f) * matrices[f];

				cmd.pushConstants<PushBlock>(*pipelinelayout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, pushBlock);

				cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
				cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelinelayout, 0, *descriptorSet, {});

				cubeMesh.Draw(cmd);

				cmd.endRenderPass();

				vk::ImageSubresourceRange offscreenSubresourceRange{
					.aspectMask = vk::ImageAspectFlagBits::eColor,
					.baseMipLevel = 0,
					.levelCount = 1,
					.baseArrayLayer = 0,
					.layerCount = 1
				};

				// Transition framebuffer color attachment to transfer source
				context.TransitionImageLayout(
					cmd,
					offscreen.image,
					vk::ImageLayout::eColorAttachmentOptimal,
					vk::ImageLayout::eTransferSrcOptimal,
					offscreenSubresourceRange
				);

				// Copy region for transfer from framebuffer to cube face
				vk::ImageCopy copyRegion = {};

				copyRegion.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
				copyRegion.srcSubresource.baseArrayLayer = 0;
				copyRegion.srcSubresource.mipLevel = 0;
				copyRegion.srcSubresource.layerCount = 1;
				copyRegion.srcOffset = { .x = 0, .y = 0, .z = 0 };

				copyRegion.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
				copyRegion.dstSubresource.baseArrayLayer = f;
				copyRegion.dstSubresource.mipLevel = m;
				copyRegion.dstSubresource.layerCount = 1;
				copyRegion.dstOffset = { .x = 0, .y = 0, .z = 0 };

				copyRegion.extent.width = static_cast<uint32_t>(viewport.width);
				copyRegion.extent.height = static_cast<uint32_t>(viewport.height);
				copyRegion.extent.depth = 1;

				cmd.copyImage(
					offscreen.image,
					vk::ImageLayout::eTransferSrcOptimal,
					prefilteredEnvMap.image,
					vk::ImageLayout::eTransferDstOptimal,
					copyRegion);

				// Transform framebuffer color attachment back
				context.TransitionImageLayout(
					cmd,
					offscreen.image,
					vk::ImageLayout::eTransferSrcOptimal,
					vk::ImageLayout::eColorAttachmentOptimal,
					offscreenSubresourceRange
				);
			}
		}

		context.TransitionImageLayout(
			cmd,
			prefilteredEnvMap.image,
			vk::ImageLayout::eTransferDstOptimal,
			vk::ImageLayout::eShaderReadOnlyOptimal,
			prefilterSubresourceRange
		);

		context.EndSingleTimeCommands(cmd);

		auto tEnd = std::chrono::high_resolution_clock::now();
		auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
		KBR_CORE_INFO("Generating pre-filtered enivornment cube with {} mip levels took {} ms", numMips, tDiff);
	}

}
