#include "GameLayer.hpp"

#include "VulkanContext.hpp"
#include "io.hpp"
#include "Texture.hpp"
#include "Vertex.hpp"
#include "ModelLoader.hpp"
#include "Shader.hpp"
#include "events/WindowResizedEvent.hpp"
#include "logging/Log.hpp"

#include "glm/glm.hpp"
#include <glm/gtc/type_ptr.hpp>
#include "imgui.h"

#include "backends/imgui_impl_vulkan.h"

namespace Game
{
	GameLayer::GameLayer() 
		: Layer("GameLayer"), m_SkyboxMesh(std::nullopt)
	{
		//m_Camera.SetPosition(glm::vec3(0.0f, 1.0f, 5.0f));
	}

	void GameLayer::OnAttach() 
	{
		KBR_CORE_INFO("GameLayer attached!");

		m_Camera = kbr::Camera(45.0f, 16.0f / 9.0f, 0.1f, 1000.0f);
		m_ViewportSize = { 1280.0f, 720.0f };

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

		KBR_CORE_INFO("Size of SceneUniformData: {} bytes", sizeof(SceneUniformData));
		KBR_CORE_INFO("Size of UniformDataParams: {} bytes", sizeof(UniformDataParams));
		KBR_CORE_INFO("Size of PerObjectData: {} bytes", sizeof(PerObjectData));

		CreateVulkanResources();

		KBR_CORE_INFO("Prepared Vulkan resources!");

		// Load models
		m_Meshes.push_back(kbr::ModelLoader::LoadModel("assets/models/avocado/Avocado.gltf"));

		KBR_CORE_INFO("Loaded {} mesh(es)!", m_Meshes.size());

		m_SkyboxMesh = kbr::ModelLoader::LoadModel("assets/models/cube.gltf");
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
			m_Camera.SetViewportSize(m_OutputSize.x, m_OutputSize.y);

			ResizeResources();
		}

		m_Camera.OnUpdate(deltaTime);

		const auto& context = kbr::VulkanContext::Get();

		const auto cmd = context.BeginSingleTimeCommands();

		context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkCommandBuffer>(*cmd)),
								 vk::ObjectType::eCommandBuffer,
								   "GameLayer Single Time Command Buffer");

		// TODO: Get current image index from VulkanContext
		constexpr uint32_t currentImage = 0;

		UpdateLights(m_Time, currentImage);
		UpdateSceneUniformBuffers(currentImage);

		// TODO: Get the current object's position
		const glm::mat4 translation = glm::translate(glm::mat4(1.0f), m_ObjectPosition);
		const glm::mat4 model = translation *
			glm::rotate(glm::mat4(1.0f), m_ObjectRotation.x, glm::vec3(1.0f, 0.0f, 0.0f)) *
			glm::rotate(glm::mat4(1.0f), m_ObjectRotation.y, glm::vec3(0.0f, 1.0f, 0.0f)) *
			glm::rotate(glm::mat4(1.0f), m_ObjectRotation.z, glm::vec3(0.0f, 0.0f, 1.0f)) *
			glm::scale(glm::mat4(1.0f), m_ObjectScale);

		const Material selectedMaterial = m_Materials[m_SelectedMaterialIndex];

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

			const vk::Rect2D renderArea{
				.offset = vk::Offset2D{ .x = 0, .y = 0 },
				.extent = vk::Extent2D{.width = m_ShadowMapSize, .height = m_ShadowMapSize }
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
			cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *m_PBRPipelineLayout, 0, *m_DescriptorSets[currentImage], {});

			for (const auto& mesh : m_Meshes)
			{
				UpdatePerObjectUniformBuffer(currentImage, m_ObjectPosition, model, selectedMaterial);

				mesh.Draw(cmd);
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

			cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_PBROpaquePipeline);
			cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *m_PBRPipelineLayout, 0, *m_DescriptorSets[currentImage], {});

			for (const auto& mesh : m_Meshes)
			{
				UpdatePerObjectUniformBuffer(currentImage, m_ObjectPosition, model, selectedMaterial);

				mesh.Draw(cmd);
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
			cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *m_PBRPipelineLayout, 0, *m_DescriptorSets[currentImage], {});

			/*for (const auto& mesh : m_Meshes)
			{
				UpdatePerObjectUniformBuffer(currentImage, objectPosition, model, material);

				mesh.Draw(cmd);
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
		m_Camera.OnEvent(event);
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

		ImGui::Text("Camera");
		const auto& camPos = m_Camera.GetPosition();
		ImGui::Text("Position: (%.2f, %.2f, %.2f)", camPos.x, camPos.y, camPos.z);
		ImGui::Text("Rotation: (Pitch: %.2f, Yaw: %.2f)", m_Camera.GetPitch(), m_Camera.GetYaw());
		ImGui::Text("Distance: %.2f", m_Camera.GetDistance());

		ImGui::Separator();

		ImGui::Text("Color Output Image");
		ImGui::Text("Size: %.2f x %.2f", m_ViewportSize.x, m_ViewportSize.y);

		ImGui::Separator();

		// Object transform controls
		ImGui::Text("Object Transform");
		ImGui::DragFloat3("Position", glm::value_ptr(m_ObjectPosition), 0.1f);
		ImGui::DragFloat3("Rotation", glm::value_ptr(m_ObjectRotation), 0.01f);
		ImGui::DragFloat3("Scale", glm::value_ptr(m_ObjectScale), 0.1f, 0.1f, 100.0f);

		ImGui::Separator();

		// Light controls
		ImGui::Text("Light Positions");
		ImGui::Text("Light 1: (%.2f, %.2f, %.2f)", m_UniformDataParams.lights[0].x, m_UniformDataParams.lights[0].y, m_UniformDataParams.lights[0].z);
		ImGui::Text("Light 2: (%.2f, %.2f, %.2f)", m_UniformDataParams.lights[1].x, m_UniformDataParams.lights[1].y, m_UniformDataParams.lights[1].z);
		ImGui::DragFloat3("Light 3 Position", glm::value_ptr(m_UniformDataParams.lights[2]), 0.1f);
		ImGui::DragFloat3("Light 4 Position", glm::value_ptr(m_UniformDataParams.lights[3]), 0.1f);
		ImGui::DragFloat("Exposure", &m_UniformDataParams.exposure, 0.1f, 0.1f, 10.0f);
		ImGui::DragFloat("Gamma", &m_UniformDataParams.gamma, 0.1f, 0.1f, 10.0f);
		ImGui::DragFloat3("Ambient Light Color", glm::value_ptr(m_SceneUniformData.ambientLightColor), 0.01f, 0.0f, 1.0f);

		ImGui::Separator();

		// Material selection
		ImGui::Text("Material");
		const char* materialNames[11];
		for (size_t i = 0; i < m_Materials.size(); ++i)
		{
			materialNames[i] = m_Materials[i].name.c_str();
		}
		ImGui::Combo("Select Material", &m_SelectedMaterialIndex, materialNames, static_cast<int>(m_Materials.size()));

		ImGui::End();

		KBR_CORE_TRACE("ImGui rendered!");
	}

	void GameLayer::UpdateSceneUniformBuffers(const uint32_t currentImage) 
	{
		const glm::mat4& projection = m_Camera.GetProjectionMatrix();
		const glm::mat4& view = m_Camera.GetViewMatrix();

		m_SceneUniformData.projection = projection;
		m_SceneUniformData.view = view;
		m_SceneUniformData.viewProjection = view * projection;
		m_SceneUniformData.lightSpaceMatrix = CalculateLightSpaceMatrix();
		m_SceneUniformData.camPos = m_Camera.GetPosition();

		std::memcpy(m_UniformBuffers[currentImage].scene->GetMappedData(), &m_SceneUniformData, sizeof(SceneUniformData));
	}

	void GameLayer::UpdatePerObjectUniformBuffer(const uint32_t currentImage, const glm::vec3& objectPosition,
		const glm::mat4& model, const Material& material) 
	{
		m_PerObjectUniformData = {
			.position = objectPosition,
			.model = model,
			.worldNormal = glm::transpose(glm::inverse(glm::mat3(model))),
			.material = material.params
		};
		std::memcpy(m_UniformBuffers[currentImage].perObject->GetMappedData(), &m_PerObjectUniformData, sizeof(PerObjectData));
	}

	void GameLayer::UpdateLights(const float time, const uint32_t currentImage) 
	{
		constexpr float p = 5.0f;
		m_UniformDataParams.lights[0] = glm::vec4(-p, -p * 0.5f, -p, 1.0f);
		m_UniformDataParams.lights[1] = glm::vec4(-p, -p * 0.5f, p, 1.0f);
		// The third and fourth are controlled via ImGui

		m_UniformDataParams.lights[0].x = sin(glm::radians(time * 80.0f)) * 20.0f;
		m_UniformDataParams.lights[0].z = cos(glm::radians(time * 80.0f)) * 20.0f;
		m_UniformDataParams.lights[1].x = cos(glm::radians(time * 80.0f)) * 20.0f;
		m_UniformDataParams.lights[1].y = sin(glm::radians(time * 80.0f)) * 20.0f;

		std::memcpy(m_UniformBuffers[currentImage].params->GetMappedData(), &m_UniformDataParams, sizeof(UniformDataParams));
	}

	void GameLayer::PrepareUniformBuffers()
	{
		for (auto& [scene, params, perObject] : m_UniformBuffers)
		{
			scene = std::make_shared<kbr::UniformBuffer>(sizeof(SceneUniformData));

			params = std::make_shared<kbr::UniformBuffer>(sizeof(UniformDataParams));

			perObject = std::make_shared<kbr::UniformBuffer>(sizeof(PerObjectData));
		}
	}

	void GameLayer::SetupDescriptors() 
	{
		auto& context = kbr::VulkanContext::Get();
		const auto& device = context.GetDevice();

		std::vector<vk::DescriptorPoolSize> poolSizes = {
			vk::DescriptorPoolSize{
				.type = vk::DescriptorType::eUniformBuffer,
				.descriptorCount = 20
			},
			vk::DescriptorPoolSize{
				.type = vk::DescriptorType::eCombinedImageSampler,
				.descriptorCount = 10
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
			vk::DescriptorSetLayoutBinding{
				.binding = 0,
				.descriptorType = vk::DescriptorType::eUniformBuffer,
				.descriptorCount = 1,
				.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
				.pImmutableSamplers = nullptr
			},
			vk::DescriptorSetLayoutBinding{
				.binding = 1,
				.descriptorType = vk::DescriptorType::eUniformBuffer,
				.descriptorCount = 1,
				.stageFlags = vk::ShaderStageFlagBits::eFragment,
				.pImmutableSamplers = nullptr
			},
			vk::DescriptorSetLayoutBinding{
				.binding = 2,
				.descriptorType = vk::DescriptorType::eUniformBuffer,
				.descriptorCount = 1,
				.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
				.pImmutableSamplers = nullptr
			},
			vk::DescriptorSetLayoutBinding{
				.binding = 3,
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

		m_PBRDescriptorSetLayout = vk::raii::DescriptorSetLayout{ device, layoutInfo };
		context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkDescriptorSetLayout>(*m_PBRDescriptorSetLayout)),
								   vk::ObjectType::eDescriptorSetLayout,
								   "PBR Descriptor Set Layout");

		vk::DescriptorSetAllocateInfo allocInfo{
			.descriptorPool = m_DescriptorPool,
			.descriptorSetCount = static_cast<uint32_t>(m_DescriptorSets.size()),
			.pSetLayouts = &*m_PBRDescriptorSetLayout
		};
		std::vector<vk::raii::DescriptorSet> descriptorSets = device.allocateDescriptorSets(allocInfo);

		for (uint32_t i = 0; i < m_UniformBuffers.size(); i++)
		{
			m_DescriptorSets[i] = std::move(descriptorSets[i]);

			context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkDescriptorSet>(*m_DescriptorSets[i])),
									   vk::ObjectType::eDescriptorSet,
									   "PBR Descriptor Set[" + std::to_string(i) + "]");

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
				.sampler = *m_ColorSampler,
				.imageView = *m_ShadowMapImageView,
				.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
			};

			const std::vector descriptorWrites = {
				vk::WriteDescriptorSet{
					.dstSet = *m_DescriptorSets[i],
					.dstBinding = 0,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = vk::DescriptorType::eUniformBuffer,
					.pBufferInfo = &sceneBufferInfo
				},
				vk::WriteDescriptorSet{
					.dstSet = *m_DescriptorSets[i],
					.dstBinding = 1,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = vk::DescriptorType::eUniformBuffer,
					.pBufferInfo = &paramsBufferInfo
				},
				vk::WriteDescriptorSet{
					.dstSet = *m_DescriptorSets[i],
					.dstBinding = 2,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = vk::DescriptorType::eUniformBuffer,
					.pBufferInfo = &perObjectBufferInfo
				},
				vk::WriteDescriptorSet{
					.dstSet = *m_DescriptorSets[i],
					.dstBinding = 3,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = vk::DescriptorType::eCombinedImageSampler,
					.pImageInfo = &shadowMapImageInfo
				},
			};

			device.updateDescriptorSets(descriptorWrites, {});
		}
	}

	glm::mat4 GameLayer::CalculateLightSpaceMatrix() const 
	{
		constexpr float nearPlane = 1.0f;
		constexpr float farPlane = 50.0f;
		constexpr float orthoSize = 20.0f;
		const glm::mat4 lightProjection = glm::ortho(-orthoSize, orthoSize, -orthoSize, orthoSize, nearPlane, farPlane);

		constexpr glm::vec3 sceneCenter = glm::vec3(0.0f, 0.0f, 0.0f);
		constexpr float lightDistance = 20.0f;

		const glm::vec3 lightPos = sceneCenter - glm::normalize(glm::vec3(m_UniformDataParams.lights[3])) * lightDistance;
		constexpr glm::vec3 lightTarget = sceneCenter; /// Look at origin
		constexpr glm::vec3 lightUp = glm::vec3(0.0f, 1.0f, 0.0f);

		const glm::mat4 lightView = glm::lookAt(glm::vec3(lightPos), lightTarget, lightUp);

		return lightProjection * lightView;
	}

	void GameLayer::CreateVulkanResources()
	{
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

			context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkSampler>(*m_ColorSampler)),
									   vk::ObjectType::eSampler,
									   "Color Texture Sampler");
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

			CreateImage(device,
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

			m_ShadowMapImageView = CreateImageView(device,
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
				.pSetLayouts = &*m_PBRDescriptorSetLayout,
				.pushConstantRangeCount = 0,
				.pPushConstantRanges = nullptr
			};

			m_ShadowMapPipelineLayout = vk::raii::PipelineLayout{ device, pipelineLayoutInfo };
			context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkPipelineLayout>(*m_ShadowMapPipelineLayout)),
									   vk::ObjectType::ePipelineLayout,
									   "Shadow Map Pipeline Layout");

			// Create shader for shadow mapping
			kbr::Shader shadowMapShader("assets/shaders/shadowmap.spv", "ShadowMap");

			constexpr vk::VertexInputBindingDescription bindingDescription = { 0, sizeof(glm::vec3), vk::VertexInputRate::eVertex };
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

			CreateImage(device,
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

			m_ColorImageView = CreateImageView(device, m_ColorImage, colorFormat, vk::ImageAspectFlagBits::eColor, mipLevels);

			context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkImageView>(*m_ColorImageView)),
									   vk::ObjectType::eImageView,
									   "Color Attachment Image View");

			const vk::Format depthFormat = context.FindSupportedFormat(
				{ vk::Format::eD32Sfloat },
				vk::ImageTiling::eOptimal,
				vk::FormatFeatureFlagBits::eDepthStencilAttachment
			);

			CreateImage(
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

			m_DepthImageView = CreateImageView(device, m_DepthImage, depthFormat, vk::ImageAspectFlagBits::eDepth, mipLevels);

			context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkImageView>(*m_DepthImageView)),
									   vk::ObjectType::eImageView,
									   "Depth Attachment Image View");

			vk::PipelineLayoutCreateInfo pipelineLayoutInfo{
				.setLayoutCount = 1,
				.pSetLayouts = &*m_PBRDescriptorSetLayout,
				.pushConstantRangeCount = 0,
				.pPushConstantRanges = nullptr
			};

			m_PBRPipelineLayout = vk::raii::PipelineLayout{ device, pipelineLayoutInfo };

			context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkPipelineLayout>(*m_PBRPipelineLayout)),
									   vk::ObjectType::ePipelineLayout,
									   "PBR Pipeline Layout");

			kbr::Shader pbrShader("assets/shaders/pbrbasic.spv", "PBR");

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

			const auto shaderStages = pbrShader.GetPipelineShaderStageCreateInfo();

			vk::GraphicsPipelineCreateInfo opaquePipelineInfo{
				.pNext = &pipelineRenderingCreateInfo,
				.stageCount = 2,
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

			m_PBROpaquePipeline = vk::raii::Pipeline(device, nullptr, opaquePipelineInfo);

			context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkPipeline>(*m_PBROpaquePipeline)),
									   vk::ObjectType::ePipeline,
									   "PBR Opaque Pipeline");

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

			context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkPipeline>(*m_PBRTransparentPipeline)),
									   vk::ObjectType::ePipeline,
									   "PBR Transparent Pipeline");
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
		CreateImage(device,
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

		m_ColorImageView = CreateImageView(device, m_ColorImage, colorFormat, vk::ImageAspectFlagBits::eColor, mipLevels);

		context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkImageView>(*m_ColorImageView)),
								   vk::ObjectType::eImageView,
								   "Color Attachment Image View");

		const vk::Format depthFormat = context.FindSupportedFormat(
			{ vk::Format::eD32Sfloat },
			vk::ImageTiling::eOptimal,
			vk::FormatFeatureFlagBits::eDepthStencilAttachment
		);

		CreateImage(
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

		m_DepthImageView = CreateImageView(device, m_DepthImage, depthFormat, vk::ImageAspectFlagBits::eDepth, mipLevels);

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
}
