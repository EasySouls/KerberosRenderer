#include "pch.hpp"
#include "GameLayer.hpp"

#include <ranges>

#include "VulkanContext.hpp"
#include "io.hpp"
#include "Vertex.hpp"
#include "ModelLoader.hpp"
#include "Shader.hpp"
#include "Buffer.hpp"
#include "Events/WindowResizedEvent.hpp"
#include "Renderer/SkyboxUtils.hpp"
#include "Scene/Camera/EditorCamera.hpp"
#include "Scene/Camera/FirstPersonCamera.hpp"
#include "Input/KeyCodes.hpp"
#include "Logging/Log.hpp"

#include <glm/gtc/matrix_inverse.hpp>

#include "imgui.h"

#include "backends/imgui_impl_vulkan.h"

namespace Game
{
	GameLayer::GameLayer() 
		: Layer("GameLayer"), m_SkyboxMesh(std::nullopt)
	{
	}

	GameLayer::~GameLayer() 
	{
		for (const auto& node : m_SceneNodes)
		{
			delete node;
		}

		m_SceneNodes.clear();
		
		kbr::VulkanContext::Get().WaitIdle();

		ImGui_ImplVulkan_RemoveTexture(m_ColorOutputDescriptorSet);
		ImGui_ImplVulkan_RemoveTexture(m_ShadowMapDescriptorSet);
	}

	void GameLayer::OnAttach() 
	{
		KBR_CORE_INFO("GameLayer attached!");

		m_Camera = std::make_unique<kbr::FirstPersonCamera>(45.0f, 16.0f / 9.0f, 0.1f, 1000.0f);
		m_Camera->SetFlipY(true);
		m_Camera->SetPosition(glm::vec3(0.0f, 15.0f, 45.0f));
		m_ViewportSize = { 1280.0f, 720.0f };

		m_MaterialRegistry.Add("Gold", std::make_shared<kbr::Material>("Gold", glm::vec3(1.0f, 0.765557f, 0.336057f), 0.1f, 1.0f));
		m_MaterialRegistry.Add("Copper", std::make_shared<kbr::Material>("Copper", glm::vec3(0.955008f, 0.637427f, 0.538163f), 0.1f, 1.0f));
		m_MaterialRegistry.Add("Chromium", std::make_shared<kbr::Material>("Chromium", glm::vec3(0.549585f, 0.556114f, 0.554256f), 0.1f, 1.0f));
		m_MaterialRegistry.Add("Nickel", std::make_shared<kbr::Material>("Nickel", glm::vec3(0.659777f, 0.608679f, 0.525649f), 0.1f, 1.0f));
		m_MaterialRegistry.Add("Titanium", std::make_shared<kbr::Material>("Titanium", glm::vec3(0.541931f, 0.496791f, 0.449419f), 0.1f, 1.0f));
		m_MaterialRegistry.Add("Cobalt", std::make_shared<kbr::Material>("Cobalt", glm::vec3(0.662124f, 0.654864f, 0.633732f), 0.1f, 1.0f));
		m_MaterialRegistry.Add("Platinum", std::make_shared<kbr::Material>("Platinum", glm::vec3(0.672411f, 0.637331f, 0.585456f), 0.1f, 1.0f));
		m_MaterialRegistry.Add("White", std::make_shared<kbr::Material>("White", glm::vec3(1.0f), 0.1f, 1.0f));
		m_MaterialRegistry.Add("Red", std::make_shared<kbr::Material>("Red", glm::vec3(1.0f, 0.0f, 0.0f), 0.1f, 1.0f));
		m_MaterialRegistry.Add("Blue", std::make_shared<kbr::Material>("Blue", glm::vec3(0.0f, 0.0f, 1.0f), 0.1f, 1.0f));
		m_MaterialRegistry.Add("Black", std::make_shared<kbr::Material>("Black", glm::vec3(0.0f), 0.1f, 1.0f));

		KBR_CORE_INFO("Size of SceneUniformData: {} bytes", sizeof(SceneUniformData));
		KBR_CORE_INFO("Size of UniformDataParams: {} bytes", sizeof(UniformDataParams));
		KBR_CORE_INFO("Size of PerObjectData: {} bytes", sizeof(PerObjectData));
		KBR_CORE_INFO("Size of material UniformBlock: {} bytes", sizeof(kbr::Material::UniformBlock));

		constexpr kbr::GLTFLoadingFlags loadingFlags = kbr::GLTFLoadingFlags::None;

		m_SkyboxMesh = kbr::ModelLoader::LoadModel("assets/models/cube.gltf", loadingFlags);
		m_SkyboxTexture.LoadFromFile(
			"assets/textures/hdr/pisa_cube.ktx",
			vk::Format::eR16G16B16A16Sfloat,
			vk::ImageUsageFlagBits::eSampled
		);

		// Load models
		m_Meshes["avocado"] = std::make_shared<kbr::Mesh>(kbr::ModelLoader::LoadModel("assets/models/avocado/Avocado.gltf", loadingFlags));
		m_Meshes["cube"] = std::make_shared<kbr::Mesh>(kbr::ModelLoader::LoadModel("assets/models/cube.gltf", loadingFlags));
		m_Meshes["sphere"] = std::make_shared<kbr::Mesh>(kbr::ModelLoader::LoadModel("assets/models/sphere.gltf", loadingFlags));

		KBR_CORE_INFO("Loaded {} mesh(es)!", m_Meshes.size());

		const std::vector<std::pair<std::string, vk::Format>> textureFiles = {
			{ "assets/models/avocado/Avocado_baseColor.ktx2", vk::Format::eR8G8B8A8Srgb },
			{ "assets/models/avocado/Avocado_normal.ktx2", vk::Format::eR8G8B8A8Unorm },
			{ "assets/textures/stonefloor01_color_rgba.ktx", vk::Format::eR8G8B8A8Srgb },
			{ "assets/textures/stonefloor01_normal_rgba.ktx", vk::Format::eR8G8B8A8Unorm },
			{ "assets/textures/stonefloor02_color_rgba.ktx", vk::Format::eR8G8B8A8Srgb },
			{ "assets/textures/stonefloor02_normal_rgba.ktx", vk::Format::eR8G8B8A8Unorm },
		};

		m_Textures.reserve(textureFiles.size());
		for (const auto& [filepath, format] : textureFiles)
		{
			auto texture = std::make_shared<kbr::Texture2D>();
			texture->LoadFromFile(filepath, format);
			m_Textures.push_back(texture);
		}

		KBR_CORE_INFO("Loaded {} texture(s)!", m_Textures.size());

		const auto& avocadoMaterial = m_MaterialRegistry.AddAndRetrieve("Avocado", std::make_shared<kbr::Material>("Avocado", glm::vec3(1.0f), 0.9f, 0.03f, m_Textures[0], m_Textures[1]));
		const auto& stoneFloorMaterial = m_MaterialRegistry.AddAndRetrieve("Stone Floor", std::make_shared<kbr::Material>("Stone Floor", glm::vec3(1.0f), 0.8f, 0.05f, m_Textures[2], m_Textures[3]));
		const auto& stoneFloor2Material = m_MaterialRegistry.AddAndRetrieve("Stone Floor 2", std::make_shared<kbr::Material>("Stone Floor 2", glm::vec3(1.0f), 0.9f, 0.0f, m_Textures[4], m_Textures[5]));

		m_SceneNodes.push_back(new kbr::Node{
			.Position = glm::vec3(6.0f, 9.5f, 0.0f),
			.Rotation = glm::vec3(0.0f),
			.Scale = glm::vec3(50.0f),
			.Mesh = m_Meshes["avocado"],
			.Material = avocadoMaterial,
			.Name = "Avocado"
		});

		m_SceneNodes.push_back(new kbr::Node{
			.Position = glm::vec3(2.0f, 0.0f, 0.0f),
			.Rotation = glm::vec3(0.0f),
			.Scale = glm::vec3(1.0f),
			.Mesh = m_Meshes["cube"],
			.Material = stoneFloorMaterial,
			.Name = "Cube"
		});

		m_SceneNodes.push_back(new kbr::Node{
			.Position = glm::vec3(2.0f, 6.0f, 3.0f),
			.Rotation = glm::vec3(0.0f),
			.Scale = glm::vec3(1.0f),
			.Mesh = m_Meshes["sphere"],
			.Material = stoneFloorMaterial,
			.Name = "Sphere"
		});

		m_SceneNodes.push_back(new kbr::Node{
			.Position = glm::vec3(2.0f, -10.0f, 3.0f),
			.Rotation = glm::vec3(0.0f),
			.Scale = glm::vec3(20.0f, 0.1f, 20.0f),
			.Mesh = m_Meshes["cube"],
			.Material = stoneFloor2Material,
			.Name = "Floor"
		});

		// Setup initial directional light which we will use to generate the shadow map
		m_UniformDataParams.lights[0] = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);

		CreateVulkanResources();

		KBR_CORE_INFO("Prepared Vulkan resources!");
	}
	
	void GameLayer::OnDetach() 
	{
		KBR_CORE_INFO("GameLayer detached!");
	}

	void GameLayer::OnUpdate(const float deltaTime)
	{
		m_Fps = 1.0f / deltaTime;
		m_Time += deltaTime;

		// Resize the output images and the camera if the viewport size has changed
		if (m_ViewportSize.x > 0.0f && m_ViewportSize.y > 0.0f && 
			(static_cast<int>(m_OutputSize.x) != static_cast<int>(m_ViewportSize.x) || static_cast<int>(m_OutputSize.y) != static_cast<int>(m_ViewportSize.y)))
		{
			m_Camera->SetViewportSize(m_ViewportSize.x, m_ViewportSize.y);

			ResizeResources();
		}

		m_Camera->OnUpdate(deltaTime);

		const auto& context = kbr::VulkanContext::Get();

		const auto cmd = context.BeginSingleTimeCommands();

		context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkCommandBuffer>(*cmd)),
								 vk::ObjectType::eCommandBuffer,
								   "GameLayer Single Time Command Buffer");

		// TODO: Get current image index from VulkanContext
		constexpr uint32_t currentImage = 0;

		UpdateLights(m_Time, currentImage);
		UpdateSceneUniformBuffers(currentImage);

		/*const glm::mat4 translation = glm::translate(glm::mat4(1.0f), m_ObjectPosition);
		const glm::mat4 model = translation *
			glm::rotate(glm::mat4(1.0f), m_ObjectRotation.x, glm::vec3(1.0f, 0.0f, 0.0f)) *
			glm::rotate(glm::mat4(1.0f), m_ObjectRotation.y, glm::vec3(0.0f, 1.0f, 0.0f)) *
			glm::rotate(glm::mat4(1.0f), m_ObjectRotation.z, glm::vec3(0.0f, 0.0f, 1.0f)) *
			glm::scale(glm::mat4(1.0f), m_ObjectScale);*/

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
				.clearValue = vk::ClearDepthStencilValue{ .depth = 1.0f, .stencil = 0 }
			};

			const vk::Rect2D renderArea{
				.offset = vk::Offset2D{ .x = 0, .y = 0 },
				.extent = vk::Extent2D{ .width = m_ShadowMapSize, .height = m_ShadowMapSize }
			};

			const vk::RenderingInfo shadowMapRenderingInfo{
				.renderArea = renderArea,
				.layerCount = 1,
				.colorAttachmentCount = 0,
				.pColorAttachments = nullptr,
				.pDepthAttachment = &shadowMapDepthAttachmentInfo
			};

			cmd.beginRendering(shadowMapRenderingInfo);
			cmd.setViewport(0, vk::Viewport{
				                .x = 0.0f, .y = 0.0f, 
								.width = static_cast<float>(m_ShadowMapSize),
				                .height = static_cast<float>(m_ShadowMapSize), 
								.minDepth = 0.0f,
				                .maxDepth = 1.0f 
							});
			cmd.setScissor(0, renderArea);

			cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_ShadowMapPipeline);

			for (size_t i = 0; i < m_SceneNodes.size(); ++i)
			{
				auto& node = m_SceneNodes[i];
				if (!node->Visible || !node->Mesh || !node->Material)
					continue;

				UpdatePerObjectUniformBuffer(currentImage, static_cast<uint32_t>(i), node->GetTransform(), *node->Material);

				uint32_t dynamicOffset = static_cast<uint32_t>(i * m_DynamicAlignment);

				cmd.bindDescriptorSets(
					vk::PipelineBindPoint::eGraphics, 
					*m_PBRPipelineLayout, 
					0, 
					*m_DescriptorSets[currentImage].scene, 
					{ dynamicOffset });
				
				node->Mesh->Draw(cmd);
			}

			cmd.endRendering();

			KBR_CORE_TRACE("Shadow pass done!");
		}

		// Transition shadow map image layout for shader read
		{
			vk::ImageMemoryBarrier2 barrier = {
			.srcStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
			.srcAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
			.dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
			.dstAccessMask = vk::AccessFlagBits2::eShaderRead,
			.oldLayout = vk::ImageLayout::eDepthAttachmentOptimal,
			.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
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
		}

		// Transition color image layout for color attachment
		{
			vk::ImageMemoryBarrier2 barrier = {
			.srcStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
			.srcAccessMask = vk::AccessFlagBits2::eShaderRead,
			.dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
			.dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
			.oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
			.newLayout = vk::ImageLayout::eColorAttachmentOptimal,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = m_ColorImage,
			.subresourceRange = {
				.aspectMask = vk::ImageAspectFlagBits::eColor,
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
		}

		// Transition depth image to depth attachment optimal
		{
			vk::ImageMemoryBarrier2 barrier = {
			.srcStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
			.srcAccessMask = vk::AccessFlagBits2::eShaderRead,
			.dstStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
			.dstAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
			.oldLayout = vk::ImageLayout::eUndefined,
			.newLayout = vk::ImageLayout::eDepthAttachmentOptimal,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = m_DepthImage,
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
		}

		const vk::Viewport viewport{
			.x = 0.0f,
			.y = 0.0f,
			.width = m_OutputSize.x,
			.height = m_OutputSize.y,
			.minDepth = 0.0f,
			.maxDepth = 1.0f
		};

		const vk::Rect2D renderArea{
				.offset = vk::Offset2D{ .x = 0, .y = 0 },
				.extent = vk::Extent2D{ .width = static_cast<uint32_t>(m_OutputSize.x), .height = static_cast<uint32_t>(m_OutputSize.y) }
		};

		// Render opaque objects
		{
			vk::RenderingAttachmentInfo colorAttachmentInfo{
				.imageView = m_ColorImageView,
				.imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
				.loadOp = vk::AttachmentLoadOp::eClear,
				.storeOp = vk::AttachmentStoreOp::eStore,
				.clearValue = vk::ClearColorValue{ std::array{0.0f, 0.0f, 0.0f, 1.0f} }
			};

			vk::RenderingAttachmentInfo depthAttachmentInfo{
				.imageView = m_DepthImageView,
				.imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
				.loadOp = vk::AttachmentLoadOp::eClear,
				.storeOp = vk::AttachmentStoreOp::eDontCare,
				.clearValue = vk::ClearDepthStencilValue{ .depth = 1.0f, .stencil = 0 }
			};

			const vk::RenderingInfo renderingInfo{
				.renderArea = renderArea,
				.layerCount = 1,
				.colorAttachmentCount = 1,
				.pColorAttachments = &colorAttachmentInfo,
				.pDepthAttachment = &depthAttachmentInfo
			};

			cmd.beginRendering(renderingInfo);

			cmd.setViewport(0, viewport);

			cmd.setScissor(0, renderArea);

			if (m_DisplaySkybox && m_SkyboxMesh.has_value())
			{
				cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_SkyboxPipeline);
				cmd.bindDescriptorSets(
					vk::PipelineBindPoint::eGraphics, 
					*m_PBRPipelineLayout, 
					0, 
					*m_DescriptorSets[currentImage].skybox, 
					{ 0 });

				m_SkyboxMesh->Draw(cmd);
			}

			const auto& opaquePipeline = m_EnablePCF ? m_PBROpaquePipelinePCF : m_PBROpaquePipeline;
			cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *opaquePipeline);

			for (size_t i = 0; i < m_SceneNodes.size(); ++i)
			{
				auto& node = m_SceneNodes[i];

				if (!node->Visible || !node->Mesh || !node->Material || node->Material->IsTransparent())
					continue;

				UpdatePerObjectUniformBuffer(currentImage, static_cast<uint32_t>(i), node->GetTransform(), *node->Material);

				uint32_t dynamicOffset = static_cast<uint32_t>(i * m_DynamicAlignment);

				cmd.bindDescriptorSets(
					vk::PipelineBindPoint::eGraphics,
					*m_PBRPipelineLayout,
					0,
					*m_DescriptorSets[currentImage].scene,
					{ dynamicOffset });

				node->Mesh->Draw(cmd);
			}

			if (m_DisplayDebugNormals)
			{
				cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_NormalDebugPipeline);
				for (size_t i = 0; i < m_SceneNodes.size(); ++i)
				{
					auto& node = m_SceneNodes[i];
					if (!node->Visible || !node->Mesh || !node->Material || node->Material->IsTransparent())
						continue;

					UpdatePerObjectUniformBuffer(currentImage, static_cast<uint32_t>(i), node->GetTransform(), *node->Material);
					
					uint32_t dynamicOffset = static_cast<uint32_t>(i * m_DynamicAlignment);
					
					cmd.bindDescriptorSets(
						vk::PipelineBindPoint::eGraphics,
						*m_PBRPipelineLayout,
						0,
						*m_DescriptorSets[currentImage].scene,
						{ dynamicOffset });

					node->Mesh->Draw(cmd);
				}
			}

			cmd.endRendering();

			KBR_CORE_TRACE("Opaque pass done!");
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

			const vk::RenderingInfo renderingInfo{
				.renderArea = renderArea,
				.layerCount = 1,
				.colorAttachmentCount = 1,
				.pColorAttachments = &colorAttachmentInfo,
				.pDepthAttachment = &depthAttachmentInfo
			};

			cmd.beginRendering(renderingInfo);

			cmd.setViewport(0, viewport);

			cmd.setScissor(0, renderArea);

			cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_PBRTransparentPipeline);

			/*for (size_t i = 0; i < m_SceneNodes.size(); ++i)
			{
				auto& node = m_SceneNodes[i];

				if (!node->Visible || !node->Mesh || !node->Material || !node->Material->IsTransparent())
					continue;

				UpdatePerObjectUniformBuffer(currentImage, i, node->GetTransform(), selectedMaterial);

				uint32_t dynamicOffset = static_cast<uint32_t>(i * m_DynamicAlignment);

				cmd.bindDescriptorSets(
					vk::PipelineBindPoint::eGraphics,
					*m_PBRPipelineLayout,
					0,
					*m_DescriptorSets[currentImage].scene,
					{ dynamicOffset });

				node->Mesh->Draw(cmd);
			}*/

			cmd.endRendering();

			KBR_CORE_TRACE("Transparent pass done!");
		}

		// Transition color image layout for shader read in ImGui
		{
			vk::ImageMemoryBarrier2 barrier = {
			.srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
			.srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
			.dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
			.dstAccessMask = vk::AccessFlagBits2::eShaderRead,
			.oldLayout = vk::ImageLayout::eColorAttachmentOptimal,
			.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = m_ColorImage,
			.subresourceRange = {
				.aspectMask = vk::ImageAspectFlagBits::eColor,
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

			KBR_CORE_TRACE("Color image transitioned for ImGui!");
		}

		context.EndSingleTimeCommands(cmd);

		KBR_CORE_TRACE("Frame rendered!");
	}

	void GameLayer::OnEvent(const std::shared_ptr<kbr::Event> event)
	{
		m_Camera->OnEvent(event);

		OnKeyPressed(event);
	}

	void GameLayer::OnImGuiRender()
	{
		ImGui::Begin("Viewport");

		m_ViewportSize = { ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y };

		ImGui::Image((ImTextureID)m_ColorOutputDescriptorSet, ImVec2(m_ViewportSize.x, m_ViewportSize.y));

		ImGui::End();

		ImGui::Begin("Debug");

		ImGui::Text("Time: %.2f seconds", m_Time);
		ImGui::Text("FPS: %.2f", m_Fps);

		ImGui::Separator();

		ImGui::Text("EditorCamera");
		const auto& camPos = m_Camera->GetPosition();
		ImGui::Text("Position: (%.2f, %.2f, %.2f)", camPos.x, camPos.y, camPos.z);
		ImGui::Text("Rotation: (Pitch: %.2f, Yaw: %.2f)", m_Camera->GetPitch(), m_Camera->GetYaw());
		ImGui::Text("Distance: %.2f", m_Camera->GetDistance());

		glm::vec3 focalPoint = m_Camera->GetFocalPoint();
		ImGui::DragFloat3("Focal point", glm::value_ptr(focalPoint), 0.1f, 0.0f, 0.0f, "%.3f", ImGuiSliderFlags_NoInput);

		ImGui::Separator();

		ImGui::Text("Color Output Image");
		ImGui::Text("Viewport size: %.2f x %.2f", m_ViewportSize.x, m_ViewportSize.y);
		ImGui::Text("Output size: %.2f x %.2f", m_OutputSize.x, m_OutputSize.y);

		ImGui::Separator();

		// Object transform controls
		ImGui::Text("Nodes");
		for (size_t i = 0; i < m_SceneNodes.size(); ++i)
		{
			if (ImGui::TreeNode(m_SceneNodes[i]->Name.c_str()))
			{
				ImGui::Checkbox(("Visible##" + std::to_string(i)).c_str(), &m_SceneNodes[i]->Visible);

				ImGui::Separator();

				ImGui::DragFloat3(("Position##" + std::to_string(i)).c_str(), glm::value_ptr(m_SceneNodes[i]->Position), 0.1f);
				ImGui::DragFloat3(("Rotation##" + std::to_string(i)).c_str(), glm::value_ptr(m_SceneNodes[i]->Rotation), 0.01f);
				ImGui::DragFloat3(("Scale##" + std::to_string(i)).c_str(), glm::value_ptr(m_SceneNodes[i]->Scale), 0.1f, 0.1f, 100.0f);

				ImGui::Separator();

				ImGui::Text("Material");
				ImGui::Text("Name: %s", m_SceneNodes[i]->Material->name.c_str());
				ImGui::ColorEdit3(("Albedo Color##" + std::to_string(i)).c_str(), glm::value_ptr(m_SceneNodes[i]->Material->Params.albedo));
				ImGui::DragFloat(("Metallic##" + std::to_string(i)).c_str(), &m_SceneNodes[i]->Material->Params.metallic, 0.01f, 0.0f, 1.0f);
				ImGui::DragFloat(("Roughness##" + std::to_string(i)).c_str(), &m_SceneNodes[i]->Material->Params.roughness, 0.01f, 0.0f, 1.0f);
				ImGui::TreePop();
			}
		}

		ImGui::Separator();

		// Light controls
		ImGui::Text("Light directions");
		ImGui::DragFloat3("Light 1 Direction", glm::value_ptr(m_UniformDataParams.lights[0]), 0.1f);
		ImGui::Text("Light 2: (%.2f, %.2f, %.2f)", m_UniformDataParams.lights[1].x, m_UniformDataParams.lights[1].y, m_UniformDataParams.lights[1].z);
		ImGui::Text("Light 2: (%.2f, %.2f, %.2f)", m_UniformDataParams.lights[2].x, m_UniformDataParams.lights[2].y, m_UniformDataParams.lights[2].z);
		ImGui::DragFloat3("Light 4 Direction", glm::value_ptr(m_UniformDataParams.lights[3]), 0.1f);
		ImGui::DragFloat("Exposure", &m_UniformDataParams.exposure, 0.1f, 0.1f, 10.0f);
		ImGui::DragFloat("Gamma", &m_UniformDataParams.gamma, 0.1f, 0.1f, 10.0f);
		ImGui::DragFloat3("Ambient Light Color", glm::value_ptr(m_SceneUniformData.ambientLightColor), 0.01f, 0.0f, 1.0f);

		ImGui::Separator();

		// Material selection
		ImGui::Text("Material");
		
		std::vector<const char*> materialNames;
		const uint32_t materialCount = m_MaterialRegistry.Size();
		materialNames.reserve(materialCount);
		for (const auto& mat : m_MaterialRegistry | std::views::values)
		{
			materialNames.push_back(mat->name.c_str());
		}
		ImGui::Combo("Select Material", &m_SelectedMaterialIndex, materialNames.data(), static_cast<int>(materialCount));

		ImGui::Separator();

		// Display shadow map
		ImGui::Text("Shadow Map");
		ImGui::Image((ImTextureID)m_ShadowMapDescriptorSet, ImVec2(256.0f, 256.0f));

		ImGui::Text("Light position for shadow map calculation:");
		ImGui::Text("(%.2f, %.2f, %.2f)",
					m_LightPosForShadowMapCalculation.x,
					m_LightPosForShadowMapCalculation.y,
					m_LightPosForShadowMapCalculation.z);

		ImGui::Separator();

		// Scene settings
		ImGui::Text("Settings");
		ImGui::Checkbox("Display Skybox", &m_DisplaySkybox);
		ImGui::Checkbox("Display normals", &m_DisplayDebugNormals);
		ImGui::Checkbox("Enable PCF", &m_EnablePCF);

		ImGui::End();

		KBR_CORE_TRACE("ImGui rendered!");
	}

	void GameLayer::UpdateSceneUniformBuffers(const uint32_t currentImage) 
	{
		const glm::mat4& projection = m_Camera->GetProjectionMatrix();
		const glm::mat4& view = m_Camera->GetViewMatrix();

		m_SceneUniformData.projection = projection;
		m_SceneUniformData.view = view;
		m_SceneUniformData.lightSpaceMatrix = CalculateLightSpaceMatrix();
		m_SceneUniformData.camPos = m_Camera->GetPosition();

		std::memcpy(m_UniformBuffers[currentImage].scene->GetMappedData(), &m_SceneUniformData, sizeof(SceneUniformData));

		const glm::mat4 skyboxModel = glm::mat4(glm::mat3(view));
		m_SkyboxData.model = skyboxModel;
		m_SkyboxData.projection = projection;
		std::memcpy(m_UniformBuffers[currentImage].skybox->GetMappedData(), &m_SkyboxData, sizeof(SkyboxData));
	}

	void GameLayer::UpdatePerObjectUniformBuffer(const uint32_t currentImage, const uint32_t objectIndex,
	                                             const glm::mat4& model, const kbr::Material& material) 
	{
		m_PerObjectUniformData = {
			.model = model,
			.worldNormal = glm::inverseTranspose(model),
			.material = material.Params
		};

		char* data = static_cast<char*>(m_UniformBuffers[currentImage].perObject->GetMappedData());
		data += static_cast<size_t>(objectIndex) * m_DynamicAlignment;

		std::memcpy(data, &m_PerObjectUniformData, sizeof(PerObjectData));
	}

	void GameLayer::UpdateLights(const float time, const uint32_t currentImage) 
	{
		/*constexpr float p = 3.0f;
		m_UniformDataParams.lights[1] = glm::vec4(-p, -p * 0.5f, -p, 1.0f);
		m_UniformDataParams.lights[2] = glm::vec4(-p, -p * 0.5f, p, 1.0f);

		m_UniformDataParams.lights[1].x = sin(glm::radians(time * 80.0f)) * 20.0f;
		m_UniformDataParams.lights[1].z = cos(glm::radians(time * 80.0f)) * 20.0f;
		m_UniformDataParams.lights[2].x = cos(glm::radians(time * 80.0f)) * 20.0f;
		m_UniformDataParams.lights[2].y = sin(glm::radians(time * 80.0f)) * 20.0f;*/

		m_UniformDataParams.lights[1] = glm::vec4{ 0.0f };
		m_UniformDataParams.lights[2] = glm::vec4{ 0.0f };

		std::memcpy(m_UniformBuffers[currentImage].params->GetMappedData(), &m_UniformDataParams, sizeof(UniformDataParams));
	}

	void GameLayer::PrepareUniformBuffers()
	{
		const auto& context = kbr::VulkanContext::Get();
		const auto properties = context.GetProperties();

		m_MinUniformBufferOffsetAlignment = properties.limits.minUniformBufferOffsetAlignment;

		m_DynamicAlignment = sizeof(PerObjectData);
		if (m_MinUniformBufferOffsetAlignment > 0)
		{
			m_DynamicAlignment = (m_DynamicAlignment + m_MinUniformBufferOffsetAlignment - 1) & ~(m_MinUniformBufferOffsetAlignment - 1);
		}

		for (auto& [scene, params, perObject, skybox] : m_UniformBuffers)
		{
			scene = std::make_shared<kbr::UniformBuffer>(sizeof(SceneUniformData));

			params = std::make_shared<kbr::UniformBuffer>(sizeof(UniformDataParams));

			perObject = std::make_shared<kbr::UniformBuffer>(m_DynamicAlignment * MaxObjects);

			skybox = std::make_shared<kbr::UniformBuffer>(sizeof(SkyboxData));
		}
	}

	void GameLayer::SetupDescriptors() 
	{
		auto& context = kbr::VulkanContext::Get();
		const auto& device = context.GetDevice();

		std::vector<vk::DescriptorPoolSize> poolSizes = {
			vk::DescriptorPoolSize{
				.type = vk::DescriptorType::eUniformBuffer,
				.descriptorCount = 10
			},
			vk::DescriptorPoolSize{
				.type = vk::DescriptorType::eUniformBufferDynamic,
				.descriptorCount = 10
			},
			vk::DescriptorPoolSize{
				.type = vk::DescriptorType::eCombinedImageSampler,
				.descriptorCount = 20
			}
		};

		vk::DescriptorPoolCreateInfo poolInfo{
			.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
			.maxSets = 10,
			.poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
			.pPoolSizes = poolSizes.data()
		};

		m_DescriptorPool = vk::raii::DescriptorPool{ device, poolInfo };
		context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkDescriptorPool>(*m_DescriptorPool)),
								   vk::ObjectType::eDescriptorPool,
								   "PBR Descriptor Pool");

		std::vector<vk::DescriptorSetLayoutBinding> bindings = {
			vk::DescriptorSetLayoutBinding{ // Scene data
				.binding = 0,
				.descriptorType = vk::DescriptorType::eUniformBuffer,
				.descriptorCount = 1,
				.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eGeometry,
				.pImmutableSamplers = nullptr
			},
			vk::DescriptorSetLayoutBinding{ // Light data and other params
				.binding = 1,
				.descriptorType = vk::DescriptorType::eUniformBuffer,
				.descriptorCount = 1,
				.stageFlags = vk::ShaderStageFlagBits::eFragment,
				.pImmutableSamplers = nullptr
			},
			vk::DescriptorSetLayoutBinding{ // Per-object data
				.binding = 2,
				.descriptorType = vk::DescriptorType::eUniformBufferDynamic,
				.descriptorCount = 1,
				.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eGeometry,
				.pImmutableSamplers = nullptr
			},
			vk::DescriptorSetLayoutBinding{ // Shadow map
				.binding = 3,
				.descriptorType = vk::DescriptorType::eCombinedImageSampler,
				.descriptorCount = 1,
				.stageFlags = vk::ShaderStageFlagBits::eFragment,
				.pImmutableSamplers = nullptr
			},
			vk::DescriptorSetLayoutBinding{ // Irradiance map
				.binding = 4,
				.descriptorType = vk::DescriptorType::eCombinedImageSampler,
				.descriptorCount = 1,
				.stageFlags = vk::ShaderStageFlagBits::eFragment,
				.pImmutableSamplers = nullptr
			},
			vk::DescriptorSetLayoutBinding{ // BRDF LUT
				.binding = 5,
				.descriptorType = vk::DescriptorType::eCombinedImageSampler,
				.descriptorCount = 1,
				.stageFlags = vk::ShaderStageFlagBits::eFragment,
				.pImmutableSamplers = nullptr
			},
			vk::DescriptorSetLayoutBinding{ // Prefiltered environment map
				.binding = 6,
				.descriptorType = vk::DescriptorType::eCombinedImageSampler,
				.descriptorCount = 1,
				.stageFlags = vk::ShaderStageFlagBits::eFragment,
				.pImmutableSamplers = nullptr
			},
		};

		const vk::DescriptorSetLayoutCreateInfo layoutInfo{
			.bindingCount = static_cast<uint32_t>(bindings.size()),
			.pBindings = bindings.data()
		};

		m_DescriptorSetLayouts.scene = vk::raii::DescriptorSetLayout{ device, layoutInfo };
		context.SetObjectDebugName(m_DescriptorSetLayouts.scene,"PBR Descriptor Set Layout");

		m_DescriptorSetLayouts.textures = vk::raii::DescriptorSetLayout{ device, layoutInfo };
		context.SetObjectDebugName(m_DescriptorSetLayouts.textures, "Texture Descriptor Set Layout");

		const std::array<vk::DescriptorSetLayout, 2> setLayouts = {
			*m_DescriptorSetLayouts.scene,
			*m_DescriptorSetLayouts.textures
		};

		vk::DescriptorSetAllocateInfo allocInfo{
			.descriptorPool = m_DescriptorPool,
			.descriptorSetCount = static_cast<uint32_t>(m_DescriptorSets.size()), // TODO: Check if this is correct
			.pSetLayouts = setLayouts.data()
		};

		std::vector<vk::raii::DescriptorSet> sceneDescriptorSets = device.allocateDescriptorSets(allocInfo);
		std::vector<vk::raii::DescriptorSet> skyboxDescriptorSets = device.allocateDescriptorSets(allocInfo);
		for (uint32_t i = 0; i < m_UniformBuffers.size(); i++)
		{
			m_DescriptorSets[i].scene = std::move(sceneDescriptorSets[i]);
			context.SetObjectDebugName(m_DescriptorSets[i].scene,"PBR Descriptor Set[" + std::to_string(i) + "]");

			const vk::DescriptorBufferInfo sceneBufferInfo{
				.buffer = *m_UniformBuffers[i].scene->GetBuffer(),
				.offset = 0,
				.range = sizeof(SceneUniformData)
			};

			const vk::DescriptorBufferInfo paramsBufferInfo{
				.buffer = *m_UniformBuffers[i].params->GetBuffer(),
				.offset = 0,
				.range = sizeof(UniformDataParams)
			};

			const vk::DescriptorBufferInfo perObjectBufferInfo{
				.buffer = *m_UniformBuffers[i].perObject->GetBuffer(),
				.offset = 0,
				.range = sizeof(PerObjectData)
			};

			const vk::DescriptorImageInfo shadowMapImageInfo{
				.sampler = *m_ShadowMapSampler,
				.imageView = *m_ShadowMapImageView,
				.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
			};

			const vk::DescriptorImageInfo skyboxImageInfo{
				.sampler = *m_SkyboxTexture.GetSampler(),
				.imageView = *m_SkyboxTexture.GetImageView(),
				.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
			};

			const vk::DescriptorBufferInfo skyboxBufferInfo{
				.buffer = *m_UniformBuffers[i].skybox->GetBuffer(),
				.offset = 0,
				.range = sizeof(SkyboxData)
			};

			const std::vector descriptorWrites = {
				vk::WriteDescriptorSet{
					.dstSet = *m_DescriptorSets[i].scene,
					.dstBinding = 0,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = vk::DescriptorType::eUniformBuffer,
					.pBufferInfo = &sceneBufferInfo
				},
				vk::WriteDescriptorSet{
					.dstSet = *m_DescriptorSets[i].scene,
					.dstBinding = 1,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = vk::DescriptorType::eUniformBuffer,
					.pBufferInfo = &paramsBufferInfo
				},
				vk::WriteDescriptorSet{
					.dstSet = *m_DescriptorSets[i].scene,
					.dstBinding = 2,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = vk::DescriptorType::eUniformBufferDynamic,
					.pBufferInfo = &perObjectBufferInfo
				},
				vk::WriteDescriptorSet{
					.dstSet = *m_DescriptorSets[i].scene,
					.dstBinding = 3,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = vk::DescriptorType::eCombinedImageSampler,
					.pImageInfo = &shadowMapImageInfo
				},
				vk::WriteDescriptorSet{
					.dstSet = *m_DescriptorSets[i].scene,
					.dstBinding = 4,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = vk::DescriptorType::eCombinedImageSampler,
					.pImageInfo = &m_IrradianceCubeTexture.descriptor
				},
				vk::WriteDescriptorSet{
					.dstSet = *m_DescriptorSets[i].scene,
					.dstBinding = 5,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = vk::DescriptorType::eCombinedImageSampler,
					.pImageInfo = &m_LutBrdfTexture.descriptor
				},
				vk::WriteDescriptorSet{
					.dstSet = *m_DescriptorSets[i].scene,
					.dstBinding = 6,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = vk::DescriptorType::eCombinedImageSampler,
					.pImageInfo = &m_PrefilteredCubeTexture.descriptor
				}
			};

			device.updateDescriptorSets(descriptorWrites, {});

			m_DescriptorSets[i].skybox = std::move(skyboxDescriptorSets[i]);
			context.SetObjectDebugName(m_DescriptorSets[i].skybox, "Skybox Descriptor Set[" + std::to_string(i) + "]");

			const std::vector skyboxDescriptorWrites = {
				vk::WriteDescriptorSet{
					.dstSet = *m_DescriptorSets[i].skybox,
					.dstBinding = 0,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = vk::DescriptorType::eUniformBuffer,
					.pBufferInfo = &skyboxBufferInfo
				},
				vk::WriteDescriptorSet{
					.dstSet = *m_DescriptorSets[i].skybox,
					.dstBinding = 1,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = vk::DescriptorType::eUniformBuffer,
					.pBufferInfo = &paramsBufferInfo
				},
				vk::WriteDescriptorSet{
					.dstSet = *m_DescriptorSets[i].skybox,
					.dstBinding = 4,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = vk::DescriptorType::eCombinedImageSampler,
					.pImageInfo = &skyboxImageInfo
				}
			};

			device.updateDescriptorSets(skyboxDescriptorWrites, {});
		}
	}

	glm::mat4 GameLayer::CalculateLightSpaceMatrix() 
	{
		constexpr float nearPlane = 0.1f;
		constexpr float farPlane = 100.0f;
		constexpr float orthoSize = 20.0f;
		glm::mat4 lightProjection = glm::ortho(-orthoSize, orthoSize, -orthoSize, orthoSize, nearPlane, farPlane);
		lightProjection[1][1] *= -1.0f;

		constexpr glm::vec3 sceneCenter = glm::vec3(0.0f, 0.0f, 0.0f);
		constexpr float lightDistance = 20.0f;

		/*const glm::vec3 lightDirRaw = glm::vec3(m_UniformDataParams.lights[0]);
		const glm::vec3 lightDir = glm::length2(lightDirRaw) > std::numeric_limits<float>::epsilon()
			? glm::normalize(lightDirRaw) 
			: glm::vec3(0.0f, 1.0f, 0.0f);*/

		const glm::vec3 lightDir = glm::normalize(glm::vec3(m_UniformDataParams.lights[0]));

		const glm::vec3 lightPos = sceneCenter + lightDir * lightDistance;
		m_LightPosForShadowMapCalculation = lightPos;
		constexpr glm::vec3 lightTarget = sceneCenter; /// Look at origin

		glm::vec3 lightUp = glm::vec3(0.0f, 1.0f, 0.0f);
		if (glm::abs(glm::dot(lightDir, lightUp)) > 0.99f)
		{
			lightUp = glm::vec3(1.0f, 0.0f, 0.0f);
		}

		const glm::mat4 lightView = glm::lookAt(lightPos, lightTarget, lightUp);

		// Correction matrix for Vulkan Clip Space
		// Y: -1 (flip logic), Z: 0.5 scale + 0.5 offset ([-1,1] -> [0,1])
		//constexpr glm::mat4 correction = glm::mat4(
		//	1.0f, 0.0f, 0.0f, 0.0f,
		//	0.0f, -1.0f, 0.0f, 0.0f,
		//	0.0f, 0.0f, 0.5f, 0.0f,
		//	0.0f, 0.0f, 0.5f, 1.0f);

		return lightProjection * lightView;
	}

	void GameLayer::CreateVulkanResources()
	{
		kbr::SkyboxUtils::GenerateBRDFLUT(m_LutBrdfTexture);
		kbr::SkyboxUtils::GenerateIrradianceCube(m_IrradianceCubeTexture, m_SkyboxTexture.descriptor, *m_SkyboxMesh);
		kbr::SkyboxUtils::GeneratePrefilteredEnvMap(m_PrefilteredCubeTexture, m_SkyboxTexture.descriptor, *m_SkyboxMesh);

		PrepareUniformBuffers();

		auto& context = kbr::VulkanContext::Get();
		auto& device = context.GetDevice();

		// Create samplers
		{
			vk::SamplerCreateInfo samplerInfo{
				.magFilter = vk::Filter::eLinear,
				.minFilter = vk::Filter::eLinear,
				.mipmapMode = vk::SamplerMipmapMode::eLinear,
				.addressModeU = vk::SamplerAddressMode::eRepeat,
				.addressModeV = vk::SamplerAddressMode::eRepeat,
				.addressModeW = vk::SamplerAddressMode::eRepeat,
				.mipLodBias = 0.0f,
				.anisotropyEnable = vk::True,
				.maxAnisotropy = 16.0f,
				.compareEnable = vk::False,
				.compareOp = vk::CompareOp::eAlways,
				.minLod = 0.0f,
				.maxLod = VK_LOD_CLAMP_NONE,
				.borderColor = vk::BorderColor::eIntOpaqueBlack,
				.unnormalizedCoordinates = vk::False
			};
			m_ColorSampler = vk::raii::Sampler{ device, samplerInfo };

			context.SetObjectDebugName(m_ColorSampler,"Color Texture Sampler");

			vk::SamplerCreateInfo shadowSamplerInfo{
				.magFilter = vk::Filter::eLinear,
				.minFilter = vk::Filter::eLinear,
				.mipmapMode = vk::SamplerMipmapMode::eLinear,
				.addressModeU = vk::SamplerAddressMode::eClampToBorder,
				.addressModeV = vk::SamplerAddressMode::eClampToBorder,
				.addressModeW = vk::SamplerAddressMode::eClampToBorder,
				.mipLodBias = 0.0f,
				.anisotropyEnable = vk::False,
				.compareEnable = vk::False,
				.compareOp = vk::CompareOp::eAlways,
				.minLod = 0.0f,
				.maxLod = 1.0f,
				.borderColor = vk::BorderColor::eFloatOpaqueWhite,
				.unnormalizedCoordinates = vk::False
			};
			m_ShadowMapSampler = vk::raii::Sampler{ device, shadowSamplerInfo };

			context.SetObjectDebugName(m_ShadowMapSampler, "Shadow Map Sampler");
		}

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
			const vk::Format shadowMapFormat = context.FindSupportedFormat(
				{ vk::Format::eD32Sfloat },
				vk::ImageTiling::eOptimal,
				vk::FormatFeatureFlagBits::eDepthStencilAttachment | vk::FormatFeatureFlagBits::eSampledImage
			);

			constexpr uint32_t shadowMapMipLevels = 1;

			kbr::CreateImage(device,
			                 m_ShadowMapSize,
			                 m_ShadowMapSize,
			                 shadowMapMipLevels,
			                 vk::SampleCountFlagBits::e1,
			                 shadowMapFormat,
			                 vk::ImageTiling::eOptimal,
			                 vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled,
			                 vk::MemoryPropertyFlagBits::eDeviceLocal,
			                 m_ShadowMapImage,
			                 m_ShadowMapImageMemory);

			context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkImage>(*m_ShadowMapImage)),
									   vk::ObjectType::eImage,
									   "Shadow Map Image");
			context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkDeviceMemory>(*m_ShadowMapImageMemory)),
									   vk::ObjectType::eDeviceMemory,
									   "Shadow Map Image Memory");

			m_ShadowMapImageView = kbr::CreateImageView(device,
			                                            m_ShadowMapImage,
			                                            shadowMapFormat,
			                                            vk::ImageAspectFlagBits::eDepth,
			                                            shadowMapMipLevels);

			context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkImageView>(*m_ShadowMapImageView)),
									   vk::ObjectType::eImageView,
									   "Shadow Map Image View");

			// We do this here, because the descriptors will use the shadow map image view,
			// but has to happen before we create the pipeline
			SetupDescriptors();

			// Create shadow map image layout transition
			/*context.TransitionImageLayout(shadowMapImage,
										  vk::ImageLayout::eUndefined,
										  vk::ImageLayout::eDepthStencilAttachmentOptimal,
										  shadowMapMipLevels);*/


			vk::PipelineLayoutCreateInfo pipelineLayoutInfo{
				.setLayoutCount = 1,
				.pSetLayouts = &*m_DescriptorSetLayouts.scene,
				.pushConstantRangeCount = 0,
				.pPushConstantRanges = nullptr
			};

			m_ShadowMapPipelineLayout = vk::raii::PipelineLayout{ device, pipelineLayoutInfo };
			context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkPipelineLayout>(*m_ShadowMapPipelineLayout)),
									   vk::ObjectType::ePipelineLayout,
									   "Shadow Map Pipeline Layout");

			// Create shader for shadow mapping
			kbr::Shader shadowMapShader("assets/shaders/shadowmap.spv", "ShadowMap");

			/*constexpr vk::VertexInputBindingDescription bindingDescription = { 0, sizeof(glm::vec3), vk::VertexInputRate::eVertex };
			constexpr std::array attributeDescriptions = {
				vk::VertexInputAttributeDescription{.location = 0, .binding = 0, .format = vk::Format::eR32G32B32Sfloat, .offset = 0 }
			};
			vk::PipelineVertexInputStateCreateInfo vertexInputInfo{
				.vertexBindingDescriptionCount = 1,
				.pVertexBindingDescriptions = &bindingDescription,
				.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size()),
				.pVertexAttributeDescriptions = attributeDescriptions.data(),
			};*/

			const auto bindingDesc = kbr::Vertex::GetBindingDescription();
			const auto attributeDescs = kbr::Vertex::GetAttributeDescriptions();

			vk::PipelineVertexInputStateCreateInfo vertexInputInfo{
				.vertexBindingDescriptionCount = 1,
				.pVertexBindingDescriptions = &bindingDesc,
				.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescs.size()),
				.pVertexAttributeDescriptions = attributeDescs.data(),
			};

			vk::PipelineRasterizationStateCreateInfo rasterizer{
				.depthClampEnable = vk::True,
				.rasterizerDiscardEnable = vk::False,
				.polygonMode = vk::PolygonMode::eFill,
				.cullMode = vk::CullModeFlagBits::eFront,
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
				.depthCompareOp = vk::CompareOp::eLessOrEqual,
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

			const auto shaderStages = shadowMapShader.GetPipelineShaderStageCreateInfo();

			vk::GraphicsPipelineCreateInfo pipelineInfo{
				.pNext = &pipelineRenderingCreateInfo,
				.stageCount = 2,
				.pStages = shaderStages.data(),
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

			context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkPipeline>(*m_ShadowMapPipeline)),
									   vk::ObjectType::ePipeline,
									   "Shadow Map Pipeline");
		}

		// Create the opaque and transparent pipeline resources
		{
			const vk::Format colorFormat = context.FindSupportedFormat(
				{ vk::Format::eR32G32B32A32Sfloat, vk::Format::eR32G32B32A32Uint },
				vk::ImageTiling::eOptimal,
				vk::FormatFeatureFlagBits::eColorAttachment | vk::FormatFeatureFlagBits::eColorAttachmentBlend | vk::FormatFeatureFlagBits::eSampledImage
			);
			const uint32_t imageWidth = static_cast<uint32_t>(m_ViewportSize.x);
			const uint32_t imageHeight = static_cast<uint32_t>(m_ViewportSize.y);
			constexpr uint32_t mipLevels = 1;

			kbr::CreateImage(device,
			                 imageWidth,
			                 imageHeight,
			                 mipLevels,
			                 vk::SampleCountFlagBits::e1,
			                 colorFormat,
			                 vk::ImageTiling::eOptimal,
			                 vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
			                 vk::MemoryPropertyFlagBits::eDeviceLocal,
			                 m_ColorImage,
			                 m_ColorImageMemory);

			context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkImage>(*m_ColorImage)),
									   vk::ObjectType::eImage,
									   "Color Attachment Image");

			context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkDeviceMemory>(*m_ColorImageMemory)),
									   vk::ObjectType::eDeviceMemory,
									   "Color Attachment Image Memory");

			m_ColorImageView = kbr::CreateImageView(device, m_ColorImage, colorFormat, vk::ImageAspectFlagBits::eColor, mipLevels);

			context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkImageView>(*m_ColorImageView)),
									   vk::ObjectType::eImageView,
									   "Color Attachment Image View");

			const vk::Format depthFormat = context.FindSupportedFormat(
				{ vk::Format::eD32Sfloat },
				vk::ImageTiling::eOptimal,
				vk::FormatFeatureFlagBits::eDepthStencilAttachment
			);

			kbr::CreateImage(
				device,
				imageWidth,
				imageHeight,
				mipLevels,
				vk::SampleCountFlagBits::e1,
				depthFormat,
				vk::ImageTiling::eOptimal,
				vk::ImageUsageFlagBits::eDepthStencilAttachment,
				vk::MemoryPropertyFlagBits::eDeviceLocal,
				m_DepthImage,
				m_DepthImageMemory);

			context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkImage>(*m_DepthImage)),
									   vk::ObjectType::eImage,
									   "Depth Attachment Image");

			context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkDeviceMemory>(*m_DepthImageMemory)),
									   vk::ObjectType::eDeviceMemory,
									   "Depth Attachment Image Memory");

			m_DepthImageView = kbr::CreateImageView(device, m_DepthImage, depthFormat, vk::ImageAspectFlagBits::eDepth, mipLevels);

			context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkImageView>(*m_DepthImageView)),
									   vk::ObjectType::eImageView,
									   "Depth Attachment Image View");

			const std::array<vk::DescriptorSetLayout, 2> setLayouts = {
				m_DescriptorSetLayouts.scene,
				m_DescriptorSetLayouts.textures
			};

			vk::PipelineLayoutCreateInfo pipelineLayoutInfo{
				.setLayoutCount = static_cast<uint32_t>(setLayouts.size()),
				.pSetLayouts = setLayouts.data(),
				.pushConstantRangeCount = 0,
				.pPushConstantRanges = nullptr
			};

			m_PBRPipelineLayout = vk::raii::PipelineLayout{ device, pipelineLayoutInfo };

			context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkPipelineLayout>(*m_PBRPipelineLayout)),
									   vk::ObjectType::ePipelineLayout,
									   "PBR Pipeline Layout");

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
				.depthCompareOp = vk::CompareOp::eLessOrEqual,
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

			kbr::Shader pbrShader("assets/shaders/pbrbasic.spv", "PBR");
			auto shaderStages = pbrShader.GetPipelineShaderStageCreateInfo();

			vk::GraphicsPipelineCreateInfo opaquePipelineInfo{
				.pNext = &pipelineRenderingCreateInfo,
				.stageCount = static_cast<uint32_t>(shaderStages.size()),
				.pStages = shaderStages.data(),
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

			uint32_t enablePCF = 0;
			vk::SpecializationMapEntry specializationMapEntry{
				.constantID = 0,
				.offset = 0,
				.size = sizeof(uint32_t)
			};
			vk::SpecializationInfo specializationInfo{
				.mapEntryCount = 1,
				.pMapEntries = &specializationMapEntry,
				.dataSize = sizeof(uint32_t),
				.pData = &enablePCF
			};
			shaderStages[1].pSpecializationInfo = &specializationInfo;

			m_PBROpaquePipeline = vk::raii::Pipeline(device, nullptr, opaquePipelineInfo);

			context.SetObjectDebugName(m_PBROpaquePipeline,"PBR Opaque Pipeline");

			enablePCF = 1;
			m_PBROpaquePipelinePCF = vk::raii::Pipeline(device, nullptr, opaquePipelineInfo);

			context.SetObjectDebugName(m_PBROpaquePipelinePCF, "PBR Opaque Pipeline PCF");

			kbr::Shader normalDebugShader("assets/shaders/normaldebug.spv", "NormalDebug");
			const auto normalDebugShaderStages = normalDebugShader.GetPipelineShaderStageCreateInfo();

			opaquePipelineInfo.stageCount = static_cast<uint32_t>(normalDebugShaderStages.size());
			opaquePipelineInfo.pStages = normalDebugShaderStages.data();

			m_NormalDebugPipeline = vk::raii::Pipeline(device, nullptr, opaquePipelineInfo);

			context.SetObjectDebugName(m_NormalDebugPipeline, "Normal Debug Pipeline");

			vk::GraphicsPipelineCreateInfo transparentPipelineInfo{
				.pNext = &pipelineRenderingCreateInfo,
				.stageCount = 2,
				.pStages = shaderStages.data(),
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

			context.SetObjectDebugName(m_PBRTransparentPipeline,"PBR Transparent Pipeline");

			kbr::Shader skyboxShader("assets/shaders/skybox.spv", "Skybox");
			const auto skyboxShaderStages = skyboxShader.GetPipelineShaderStageCreateInfo();

			opaqueDepthStencil.depthWriteEnable = vk::False;
			opaqueDepthStencil.depthTestEnable = vk::False;

			vk::PipelineDepthStencilStateCreateInfo skyboxDepthStencil{
				.depthTestEnable = vk::True,
				.depthWriteEnable = vk::False,
				.depthCompareOp = vk::CompareOp::eLessOrEqual,
				.depthBoundsTestEnable = vk::False,
				.stencilTestEnable = vk::False,
				.minDepthBounds = 0.0f,
				.maxDepthBounds = 1.0f,
			};

			vk::PipelineRasterizationStateCreateInfo skyboxRasterizer{
				.depthClampEnable = vk::False,
				.rasterizerDiscardEnable = vk::False,
				.polygonMode = vk::PolygonMode::eFill,
				.cullMode = vk::CullModeFlagBits::eFront,
				.frontFace = vk::FrontFace::eCounterClockwise,
				.depthBiasEnable = vk::False,
				.lineWidth = 1.0f
			};

			opaqueRasterizer.cullMode = vk::CullModeFlagBits::eFront;

			vk::GraphicsPipelineCreateInfo skyboxPipelineInfo{
				.pNext = &pipelineRenderingCreateInfo,
				.stageCount = 2,
				.pStages = skyboxShaderStages.data(),
				.pVertexInputState = &vertexInputInfo,
				.pInputAssemblyState = &inputAssembly,
				.pViewportState = &viewportState,
				.pRasterizationState = &skyboxRasterizer,
				.pMultisampleState = &multisampling,
				.pDepthStencilState = &skyboxDepthStencil,
				.pColorBlendState = &colorBlending,
				.pDynamicState = &dynamicStateInfo,
				.layout = m_PBRPipelineLayout,
				.renderPass = nullptr
			};
			m_SkyboxPipeline = vk::raii::Pipeline(device, nullptr, skyboxPipelineInfo);
			context.SetObjectDebugName(m_SkyboxPipeline, "Skybox Pipeline");
		}

		// Transition color image to shader read layout
		{
			vk::ImageMemoryBarrier2 barrier = {
				.srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe,
				.srcAccessMask = {},
				.dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
				.dstAccessMask = vk::AccessFlagBits2::eShaderRead,
				.oldLayout = vk::ImageLayout::eUndefined,
				.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
				.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
				.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
				.image = m_ColorImage,
				.subresourceRange = {
					.aspectMask = vk::ImageAspectFlagBits::eColor,
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

			const auto cmd = context.BeginSingleTimeCommands();
			context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkCommandBuffer>(*cmd)),
									   vk::ObjectType::eCommandBuffer,
									   "GameLayer Single Time Command Buffer for Color Image Layout Transition");
			cmd.pipelineBarrier2(dependencyInfo);
			context.EndSingleTimeCommands(cmd);
		}

		// Create descriptor set for the output image for ImGui rendering
		{
			m_ColorOutputDescriptorSet = ImGui_ImplVulkan_AddTexture(*m_ColorSampler, *m_ColorImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

			context.SetObjectDebugName(reinterpret_cast<uint64_t>(m_ColorOutputDescriptorSet),
									   vk::ObjectType::eDescriptorSet,
									   "Color Output Descriptor Set for ImGui");

			m_ShadowMapDescriptorSet = ImGui_ImplVulkan_AddTexture(*m_ColorSampler, *m_ShadowMapImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			context.SetObjectDebugName(reinterpret_cast<uint64_t>(m_ShadowMapDescriptorSet),
									   vk::ObjectType::eDescriptorSet,
									   "Shadow Map Descriptor Set for ImGui");
		}

		m_OutputSize = m_ViewportSize;
	}

	void GameLayer::ResizeResources() 
	{
		// Resize the color and depth image, the shadowmap image can keep its size
		auto& context = kbr::VulkanContext::Get();
		const auto& device = context.GetDevice();

		constexpr uint32_t mipLevels = 1;
		const vk::Format colorFormat = context.FindSupportedFormat(
			{ vk::Format::eR32G32B32A32Sfloat, vk::Format::eR32G32B32A32Uint },
			vk::ImageTiling::eOptimal,
			vk::FormatFeatureFlagBits::eColorAttachment | vk::FormatFeatureFlagBits::eColorAttachmentBlend | vk::FormatFeatureFlagBits::eSampledImage
		);

		device.waitIdle();

		// Destroy old resources
		ImGui_ImplVulkan_RemoveTexture(m_ColorOutputDescriptorSet);

		m_ColorImageView.clear();
		m_ColorImage.clear();
		m_ColorImageMemory.clear();
		m_DepthImageView.clear();
		m_DepthImage.clear();
		m_DepthImageMemory.clear();

		// Recreate resources with new size
		const uint32_t imageWidth = static_cast<uint32_t>(m_ViewportSize.x);
		const uint32_t imageHeight = static_cast<uint32_t>(m_ViewportSize.y);
		kbr::CreateImage(device,
		                 imageWidth,
		                 imageHeight,
		                 mipLevels,
		                 vk::SampleCountFlagBits::e1,
		                 colorFormat,
		                 vk::ImageTiling::eOptimal,
		                 vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
		                 vk::MemoryPropertyFlagBits::eDeviceLocal,
		                 m_ColorImage,
		                 m_ColorImageMemory);

		context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkImage>(*m_ColorImage)),
								   vk::ObjectType::eImage,
								   "Color Attachment Image");

		context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkDeviceMemory>(*m_ColorImageMemory)),
								   vk::ObjectType::eDeviceMemory,
								   "Color Attachment Image Memory");

		m_ColorImageView = kbr::CreateImageView(device, m_ColorImage, colorFormat, vk::ImageAspectFlagBits::eColor, mipLevels);

		context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkImageView>(*m_ColorImageView)),
								   vk::ObjectType::eImageView,
								   "Color Attachment Image View");

		const vk::Format depthFormat = context.FindSupportedFormat(
			{ vk::Format::eD32Sfloat },
			vk::ImageTiling::eOptimal,
			vk::FormatFeatureFlagBits::eDepthStencilAttachment
		);

		kbr::CreateImage(
			device,
			imageWidth,
			imageHeight,
			mipLevels,
			vk::SampleCountFlagBits::e1,
			depthFormat,
			vk::ImageTiling::eOptimal,
			vk::ImageUsageFlagBits::eDepthStencilAttachment,
			vk::MemoryPropertyFlagBits::eDeviceLocal,
			m_DepthImage,
			m_DepthImageMemory);

		context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkImage>(*m_DepthImage)),
								   vk::ObjectType::eImage,
								   "Depth Attachment Image");

		context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkDeviceMemory>(*m_DepthImageMemory)),
								   vk::ObjectType::eDeviceMemory,
								   "Depth Attachment Image Memory");

		m_DepthImageView = kbr::CreateImageView(device, m_DepthImage, depthFormat, vk::ImageAspectFlagBits::eDepth, mipLevels);

		context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkImageView>(*m_DepthImageView)),
								   vk::ObjectType::eImageView,
								   "Depth Attachment Image View");

		// Transition color image to shader read layout
		{
			vk::ImageMemoryBarrier2 barrier = {
				.srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe,
				.srcAccessMask = {},
				.dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
				.dstAccessMask = vk::AccessFlagBits2::eShaderRead,
				.oldLayout = vk::ImageLayout::eUndefined,
				.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
				.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
				.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
				.image = m_ColorImage,
				.subresourceRange = {
					.aspectMask = vk::ImageAspectFlagBits::eColor,
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
			const auto cmd = context.BeginSingleTimeCommands();
			context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkCommandBuffer>(*cmd)),
									   vk::ObjectType::eCommandBuffer,
									   "GameLayer Single Time Command Buffer for Color Image Layout Transition");
			cmd.pipelineBarrier2(dependencyInfo);
			context.EndSingleTimeCommands(cmd);
		}

		// Recreate descriptor set for the output image for ImGui rendering
		{
			m_ColorOutputDescriptorSet = ImGui_ImplVulkan_AddTexture(*m_ColorSampler, *m_ColorImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			context.SetObjectDebugName(reinterpret_cast<uint64_t>(m_ColorOutputDescriptorSet),
									   vk::ObjectType::eDescriptorSet,
									   "Color Output Descriptor Set for ImGui");
		}

		m_OutputSize = m_ViewportSize;
	}

	void GameLayer::OnKeyPressed(const std::shared_ptr<kbr::Event>& event) const 
	{
		if (const auto keyPressedEvent = std::dynamic_pointer_cast<kbr::KeyPressedEvent>(event))
		{
			const int key = keyPressedEvent->GetKeyCode();
			if (key == kbr::Key::F)
			{
				const glm::vec3 avocadoPosition = m_SceneNodes[0]->Position;
				m_Camera->Focus(avocadoPosition);
			}
		}
	}
}
