#include "kbrpch.hpp"
#include "Renderer.hpp"

#include <glm/gtc/matrix_inverse.hpp>
#include <limits>

#include "MaterialRegistry.hpp"
#include "ModelLoader.hpp"
#include "SkyboxUtils.hpp"
#include "Buffer.hpp"
#include "VulkanContext.hpp"
#include "Shaders/Shader.hpp"
#include "GraphicsPipeline.hpp"
#include "RayTracingSceneCache.hpp"

namespace
{
	using namespace Kerberos;

	struct ShadowMap
	{
		vk::raii::Image Image = nullptr;
		vk::raii::DeviceMemory ImageMemory = nullptr;
		vk::raii::ImageView ImageView = nullptr;
		vk::raii::PipelineLayout PipelineLayout = nullptr;
		Ref<GraphicsPipeline> Pipeline = nullptr;

		// Settings
		uint32_t Size = 2048;
		bool EnablePCF = true;

		// Calculated at runtime
		glm::vec3 LightPosForCalculation{ 0.0f , 1.0f, 0.0f };
	};

	struct Skybox
	{
		Ref<Mesh> SkyboxMesh = nullptr;
		Ref<TextureCube> SkyboxTexture = nullptr;
		bool IsSkyboxDirty = true; // Flag to indicate if the skybox needs to be re-rendered
		bool ShowSkybox = true;
		// Generated at runtime
		Ref<Texture2D> LutBrdfTexture = nullptr;
		Ref<TextureCube> IrradianceCubeTexture = nullptr;
		Ref<TextureCube> PrefilteredCubeTexture = nullptr;
	};

	struct ImageData
	{
		vk::raii::Image Image = nullptr;
		vk::raii::DeviceMemory ImageMemory = nullptr;
		vk::raii::ImageView ImageView = nullptr;
	};

	struct PickingReadbackSlot
	{
		vk::raii::Buffer Buffer = nullptr;
		vk::raii::DeviceMemory Memory = nullptr;
		void* MappedData = nullptr;
		bool Pending = false;
		uint64_t TimelineValue = 0;
	};

	struct MousePickingReadback
	{
		std::array<PickingReadbackSlot, Renderer::MousePickingReadbackFrameLag> Slots{};
		uint32_t WriteIndex = 0;
		vk::raii::Semaphore TimelineSemaphore = nullptr;
		uint64_t TimelineValue = 0;
		uint64_t PendingTimelineSignalValue = 0;
		bool RequestPending = false;
		glm::uvec2 RequestedPixel{ 0, 0 };
		std::optional<uint32_t> LatestEntityID;
	};

	struct DescriptorSetLayouts
	{
		vk::raii::DescriptorSetLayout scene = nullptr;
		vk::raii::DescriptorSetLayout textures = nullptr;
	};

	struct SceneUniformData
	{
		glm::mat4 projection{ 0.f };
		glm::mat4 view{ 0.f };
		glm::mat4 lightSpaceMatrix{ 0.f };
		alignas(16) glm::vec3 ambientLightColor{ 0.1f, 0.1f, 0.1f };
		alignas(16) glm::vec3 camPos{ 0.f };
	};

	struct UniformDataParams
	{
		// Direction of the lights
		alignas(16) std::array<glm::vec4, 4> lights{};
		float exposure = 4.5f;
		float gamma = 2.2f;
	};

	struct PerObjectData
	{
		alignas(16) glm::mat4 model{ 0.f };
		alignas(16) glm::mat4 worldNormal{ 0.f };
		alignas(16) Material::UniformBlock material;
		alignas(16) uint32_t entityID = std::numeric_limits<uint32_t>::max();
		alignas(16) glm::vec3 _Padding{ 0.0f };
	};

	struct SkyboxData
	{
		glm::mat4 projection{ 0.f };
		glm::mat4 model{ 0.f };
	};

	struct UniformBufferObject
	{
		std::shared_ptr<UniformBuffer> scene;
		std::shared_ptr<UniformBuffer> params;
		std::shared_ptr<UniformBuffer> perObject;
		std::shared_ptr<UniformBuffer> skybox;
	};

	struct DescriptorSets
	{
		vk::raii::DescriptorSet scene = nullptr;
		vk::raii::DescriptorSet skybox = nullptr;
	};

	struct PendingSceneRender
	{
		Ref<Scene> Scene = nullptr;
		glm::mat4 View{ 1.0f };
		glm::mat4 Projection{ 1.0f };
		glm::vec3 CameraPosition{ 0.0f };
		bool IsValid = false;
	};

	enum class GPUTimestampQuery : uint32_t
	{
		FrameBegin = 0,
		ShadowBegin,
		ShadowEnd,
		OpaqueBegin,
		OpaqueEnd,
		TransparentBegin,
		TransparentEnd,
		FrameEnd,
		Count
	};

	struct RendererData
	{
		MaterialRegistry MaterialRegistry;
		DepthBias DepthBias;
		ShadowMap ShadowMap;
		Skybox Skybox;
		ImageData ColorImage;
		ImageData DepthImage;
		ImageData PickingImage;
		vk::ImageLayout PickingImageLayout = vk::ImageLayout::eUndefined;

		vk::raii::DescriptorPool DescriptorPool = nullptr;
		DescriptorSetLayouts DescriptorSetLayouts;

		vk::raii::PipelineLayout PBRPipelineLayout = nullptr;
		Ref<GraphicsPipeline> PBROpaquePipeline = nullptr;
		Ref<GraphicsPipeline> PBROpaquePipelinePCF = nullptr;
		Ref<GraphicsPipeline> PBRTransparentPipeline = nullptr;
		Ref<GraphicsPipeline> SkyboxPipeline = nullptr;
		Ref<GraphicsPipeline> NormalDebugPipeline = nullptr;
		Ref<GraphicsPipeline> PBRRayQueryShadowsPipeline = nullptr;
		Ref<GraphicsPipeline> PBRRayQuerySoftShadowsPipeline = nullptr;

		vk::raii::Sampler ColorSampler = nullptr;
		vk::raii::Sampler ShadowMapSampler = nullptr;

		SceneUniformData SceneUniformData{};
		UniformDataParams UniformDataParams{};
		PerObjectData PerObjectData{};
		SkyboxData SkyboxData{};

		std::array<UniformBufferObject, VulkanContext::MaxFramesInFlight> UniformBuffers{};

		std::array<DescriptorSets, VulkanContext::MaxFramesInFlight> DescriptorSets{};

		// Dynamic uniform buffer related members
		VkDeviceSize MinUniformBufferOffsetAlignment = 0;
		uint64_t DynamicAlignment = 0;

		vk::DescriptorSet ColorOutputDescriptorSet = nullptr;
		vk::DescriptorSet ShadowMapDescriptorSet = nullptr;

		PendingSceneRender PendingRender{};

		MousePickingReadback MousePickingReadback{};

		std::vector<vk::raii::QueryPool> GPUTimestampQueryPools;
		float GPUTimestampPeriodNanoseconds = 0.0f;
		bool SupportsGPUTimestamps = false;
		GPUTimings LatestGPUTimings{};

		RayTracingSceneCache RayTracingCache{};

		glm::vec2 OutputSize{ 1280.0f, 720.0f };

		// Settings
		bool DisplayDebugNormals = false;

		bool UseRayQueryBasedShadows = false;
		bool UseRayQueryBasedSoftShadows = false;
	};

}

namespace Kerberos
{

	static Owner<RendererData> s_Data = nullptr;

	void Renderer::Init()
	{
		KBR_CORE_ASSERT(s_Data == nullptr, "Renderer is already initialized!");
		KBR_CORE_INFO("Initializing Renderer...");

		s_Data = CreateOwner<RendererData>();

		KBR_CORE_INFO("Size of SceneUniformData: {} bytes", sizeof(SceneUniformData));
		KBR_CORE_INFO("Size of UniformDataParams: {} bytes", sizeof(UniformDataParams));
		KBR_CORE_INFO("Size of PerObjectData: {} bytes", sizeof(PerObjectData));
		KBR_CORE_INFO("Size of SkyboxData: {} bytes", sizeof(SkyboxData));
		KBR_CORE_INFO("Size of material UniformBlock: {} bytes", sizeof(Material::UniformBlock));

		CreateDefaultMaterials();

		// Setup initial directional light which we will use to generate the shadow map
		s_Data->UniformDataParams.lights[0] = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);

		CreateResources();

		s_Data->MaterialRegistry.SetupDescriptorSets(s_Data->DescriptorSetLayouts.textures);
	}

	void Renderer::Shutdown() 
	{
		KBR_CORE_ASSERT(s_Data, "Renderer not initialized!");

		VulkanContext::Get().WaitIdle();

		VulkanContext::DestroyImGuiDescriptorSet(s_Data->ColorOutputDescriptorSet);
		VulkanContext::DestroyImGuiDescriptorSet(s_Data->ShadowMapDescriptorSet);

		for (auto& slot : s_Data->MousePickingReadback.Slots)
		{
         if (slot.Memory != nullptr && slot.MappedData)
			{
				slot.Memory.unmapMemory();
				slot.MappedData = nullptr;
			}
		}

		s_Data.reset();
		s_Data = nullptr;
	}

	void Renderer::RenderSceneEditor(const Ref<Scene>& scene, const Camera& camera) 
	{
		RenderScene(scene, camera.GetViewMatrix(), camera.GetProjectionMatrix(), camera.GetPosition());
	}

	void Renderer::RenderScene(const Ref<Scene>& scene, const glm::mat4& view, const glm::mat4& projection,
		const glm::vec3& camPos) 
	{
		KBR_CORE_ASSERT(!s_Data->PendingRender.IsValid, "Scene has already been queued for rendering!");

		s_Data->PendingRender.Scene = scene;
		s_Data->PendingRender.View = view;
		s_Data->PendingRender.Projection = projection;
		s_Data->PendingRender.CameraPosition = camPos;
		s_Data->PendingRender.IsValid = scene != nullptr;
	}

	void Renderer::RecordQueuedSceneRender(const vk::raii::CommandBuffer& cmd)
	{
		KBR_CORE_ASSERT(s_Data->PendingRender.IsValid, "No pending scene render to record!");

		if (!s_Data->PendingRender.IsValid || !s_Data->PendingRender.Scene)
			return;

		auto& context = VulkanContext::Get();
		const uint32_t frameIndex = context.GetCurrentFrameIndex();

		s_Data->RayTracingCache.BuildAccelerationStructures(s_Data->PendingRender.Scene);

		const auto& tlas = s_Data->RayTracingCache.GetTLAS(frameIndex);
		const vk::WriteDescriptorSetAccelerationStructureKHR asInfo{
			.accelerationStructureCount = 1,
			.pAccelerationStructures = &tlas
		};
		const std::vector asWrite = {
			vk::WriteDescriptorSet{
				.pNext = &asInfo,
				.dstSet = *s_Data->DescriptorSets[frameIndex].scene,
				.dstBinding = 7,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = vk::DescriptorType::eAccelerationStructureKHR,
			}
		};
		context.GetDevice().updateDescriptorSets(asWrite, {});

		ResetQueryPool(cmd, frameIndex);

		WriteGPUTimestamp(cmd, frameIndex, static_cast<uint32_t>(GPUTimestampQuery::FrameBegin));

        const uint32_t currentImage = frameIndex;

		UpdateLights(currentImage);
		UpdateSceneUniformBuffers(currentImage,
            s_Data->PendingRender.View,
			s_Data->PendingRender.Projection,
			s_Data->PendingRender.CameraPosition);

		const Ref<Scene>& scene = s_Data->PendingRender.Scene;

		// Render shadow map
       {
			WriteGPUTimestamp(cmd, frameIndex, static_cast<uint32_t>(GPUTimestampQuery::ShadowBegin));

			vk::ImageMemoryBarrier2 barrier = {
			.srcStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
			.srcAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
			.dstStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
			.dstAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
			.oldLayout = vk::ImageLayout::eUndefined,
			.newLayout = vk::ImageLayout::eDepthAttachmentOptimal,
			.srcQueueFamilyIndex = vk::QueueFamilyIgnored,
			.dstQueueFamilyIndex = vk::QueueFamilyIgnored,
			.image = s_Data->ShadowMap.Image,
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
				.imageView = s_Data->ShadowMap.ImageView,
				.imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
				.loadOp = vk::AttachmentLoadOp::eClear,
				.storeOp = vk::AttachmentStoreOp::eStore,
				.clearValue = vk::ClearDepthStencilValue{.depth = 1.0f, .stencil = 0 }
			};

			const vk::Rect2D renderArea{
				.offset = vk::Offset2D{.x = 0, .y = 0 },
				.extent = vk::Extent2D{.width = s_Data->ShadowMap.Size, .height = s_Data->ShadowMap.Size }
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
								.width = static_cast<float>(s_Data->ShadowMap.Size),
								.height = static_cast<float>(s_Data->ShadowMap.Size),
								.minDepth = 0.0f,
								.maxDepth = 1.0f
							});
			cmd.setScissor(0, renderArea);

			cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *s_Data->ShadowMap.Pipeline->GetVulkanPipeline());

			cmd.setDepthBias(s_Data->DepthBias.ConstantFactor, s_Data->DepthBias.Clamp, s_Data->DepthBias.SlopeFactor);

			const auto meshView = scene->m_Registry.view<TransformComponent, StaticMeshComponent>();
			int i = 0;
			for (auto entity : meshView)
			{
				auto& transform = meshView.get<TransformComponent>(entity);
				auto& staticMesh = meshView.get<StaticMeshComponent>(entity);
				if (!staticMesh.Visible || !staticMesh.StaticMesh || !staticMesh.MeshMaterial || staticMesh.MeshMaterial->IsTransparent())
					continue;

				UpdatePerObjectUniformBuffer(currentImage, static_cast<uint32_t>(i), transform.GetTransform(), *staticMesh.MeshMaterial, std::numeric_limits<uint32_t>::max());
				uint32_t dynamicOffset = static_cast<uint32_t>(i * s_Data->DynamicAlignment);

				cmd.bindDescriptorSets(
					vk::PipelineBindPoint::eGraphics,
					*s_Data->PBRPipelineLayout,
					0,
					*s_Data->DescriptorSets[currentImage].scene,
					{ dynamicOffset });

				staticMesh.StaticMesh->Draw(cmd);

				++i;
			}

			cmd.endRendering();

			WriteGPUTimestamp(cmd, frameIndex, static_cast<uint32_t>(GPUTimestampQuery::ShadowEnd));

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
			.image = s_Data->ShadowMap.Image,
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

		// Transition picking image to color attachment optimal
		{
			const vk::PipelineStageFlags2 srcStageMask = s_Data->PickingImageLayout == vk::ImageLayout::eTransferSrcOptimal
				? vk::PipelineStageFlagBits2::eTransfer
				: vk::PipelineStageFlagBits2::eTopOfPipe;
			const vk::AccessFlags2 srcAccessMask = s_Data->PickingImageLayout == vk::ImageLayout::eTransferSrcOptimal
				? vk::AccessFlagBits2::eTransferRead
				: vk::AccessFlags2{};

			vk::ImageMemoryBarrier2 barrier = {
				.srcStageMask = srcStageMask,
				.srcAccessMask = srcAccessMask,
				.dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
				.dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
                .oldLayout = vk::ImageLayout::eUndefined, //s_Data->PickingImageLayout,
				.newLayout = vk::ImageLayout::eColorAttachmentOptimal,
				.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
				.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
				.image = s_Data->PickingImage.Image,
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
			s_Data->PickingImageLayout = vk::ImageLayout::eColorAttachmentOptimal;
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
			.image = s_Data->ColorImage.Image,
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
			.image = s_Data->DepthImage.Image,
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
			.width = s_Data->OutputSize.x,
			.height = s_Data->OutputSize.y,
			.minDepth = 0.0f,
			.maxDepth = 1.0f
		};

		const vk::Rect2D renderArea{
				.offset = vk::Offset2D{.x = 0, .y = 0 },
				.extent = vk::Extent2D{.width = static_cast<uint32_t>(s_Data->OutputSize.x), .height = static_cast<uint32_t>(s_Data->OutputSize.y) }
		};

		// Render opaque objects
       {
			WriteGPUTimestamp(cmd, frameIndex, static_cast<uint32_t>(GPUTimestampQuery::OpaqueBegin));

			vk::RenderingAttachmentInfo colorAttachmentInfo{
				.imageView = s_Data->ColorImage.ImageView,
				.imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
				.loadOp = vk::AttachmentLoadOp::eClear,
				.storeOp = vk::AttachmentStoreOp::eStore,
				.clearValue = vk::ClearColorValue{ std::array{0.0f, 0.0f, 0.0f, 1.0f} }
			};

			vk::RenderingAttachmentInfo pickingAttachmentInfo{
				.imageView = s_Data->PickingImage.ImageView,
				.imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
				.loadOp = vk::AttachmentLoadOp::eClear,
				.storeOp = vk::AttachmentStoreOp::eStore,
				.clearValue = vk::ClearColorValue{ std::array{std::numeric_limits<uint32_t>::max(), 0u, 0u, 0u} }
			};

			const std::array<vk::RenderingAttachmentInfo, 2> colorAttachments = {
				colorAttachmentInfo,
				pickingAttachmentInfo
			};

			vk::RenderingAttachmentInfo depthAttachmentInfo{
				.imageView = s_Data->DepthImage.ImageView,
				.imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
				.loadOp = vk::AttachmentLoadOp::eClear,
				.storeOp = vk::AttachmentStoreOp::eDontCare,
				.clearValue = vk::ClearDepthStencilValue{.depth = 1.0f, .stencil = 0 }
			};

			const vk::RenderingInfo renderingInfo{
				.renderArea = renderArea,
				.layerCount = 1,
			    .colorAttachmentCount = static_cast<uint32_t>(colorAttachments.size()),
				.pColorAttachments = colorAttachments.data(),
				.pDepthAttachment = &depthAttachmentInfo
			};

			cmd.beginRendering(renderingInfo);

			cmd.setViewport(0, viewport);

			cmd.setScissor(0, renderArea);

			if (s_Data->Skybox.ShowSkybox && s_Data->Skybox.SkyboxMesh)
			{
				s_Data->SkyboxPipeline->Bind(cmd);
				cmd.bindDescriptorSets(
					vk::PipelineBindPoint::eGraphics,
					*s_Data->PBRPipelineLayout,
					0,
					*s_Data->DescriptorSets[currentImage].skybox,
					{ 0 });

				s_Data->Skybox.SkyboxMesh->Draw(cmd);
			}

			if (s_Data->UseRayQueryBasedShadows)
			{
				const auto& pipeline = GetUseRayQueryBasedSoftShadows() ? s_Data->PBRRayQuerySoftShadowsPipeline : s_Data->PBRRayQueryShadowsPipeline;
				pipeline->Bind(cmd);
			}
			else
			{
				const auto& opaquePipeline = GetIsPCFEnabledForShadowMap() ? s_Data->PBROpaquePipelinePCF : s_Data->PBROpaquePipeline;
				opaquePipeline->Bind(cmd);
			}

			{
				const auto meshView = scene->m_Registry.view<TransformComponent, StaticMeshComponent>();
				int i = 0;
				for (const auto entity : meshView)
				{
					auto& transform = meshView.get<TransformComponent>(entity);
					auto& staticMesh = meshView.get<StaticMeshComponent>(entity);
					if (!staticMesh.Visible || !staticMesh.StaticMesh || !staticMesh.MeshMaterial)
						continue;

					// TODO: Remove this once we have a proper material system
					staticMesh.MeshMaterial = s_Data->MaterialRegistry.Get("White");

					UpdatePerObjectUniformBuffer(currentImage, static_cast<uint32_t>(i), transform.GetTransform(), *staticMesh.MeshMaterial, static_cast<uint32_t>(entity));
					uint32_t dynamicOffset = static_cast<uint32_t>(i * s_Data->DynamicAlignment);

					cmd.bindDescriptorSets(
						vk::PipelineBindPoint::eGraphics,
						*s_Data->PBRPipelineLayout,
						0,
						{ s_Data->DescriptorSets[currentImage].scene, staticMesh.MeshMaterial->DescriptorSet },
						{ dynamicOffset });

					staticMesh.StaticMesh->Draw(cmd);

					++i;
				}
			}

			if (s_Data->DisplayDebugNormals)
			{
				s_Data->NormalDebugPipeline->Bind(cmd);

				const auto meshView = scene->m_Registry.view<TransformComponent, StaticMeshComponent>();
				int i = 0;
				for (const auto entity : meshView)
				{
					auto& transform = meshView.get<TransformComponent>(entity);
					auto& meshComp = meshView.get<StaticMeshComponent>(entity);
					if (!meshComp.Visible || !meshComp.StaticMesh || !meshComp.MeshMaterial)
						continue;

					UpdatePerObjectUniformBuffer(currentImage, static_cast<uint32_t>(i), transform.GetTransform(), *meshComp.MeshMaterial, std::numeric_limits<uint32_t>::max());
					uint32_t dynamicOffset = static_cast<uint32_t>(i * s_Data->DynamicAlignment);

					cmd.bindDescriptorSets(
						vk::PipelineBindPoint::eGraphics,
						*s_Data->PBRPipelineLayout,
						0,
						{ s_Data->DescriptorSets[currentImage].scene },
						{ dynamicOffset });

					meshComp.StaticMesh->Draw(cmd);

					++i;
				}
			}

			cmd.endRendering();

			WriteGPUTimestamp(cmd, frameIndex, static_cast<uint32_t>(GPUTimestampQuery::OpaqueEnd));

			KBR_CORE_TRACE("Opaque pass done!");
		}

		// Render transparent objects
       {
			WriteGPUTimestamp(cmd, frameIndex, static_cast<uint32_t>(GPUTimestampQuery::TransparentBegin));

			vk::RenderingAttachmentInfo colorAttachmentInfo{
				.imageView = s_Data->ColorImage.ImageView,
				.imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
				.loadOp = vk::AttachmentLoadOp::eLoad,
				.storeOp = vk::AttachmentStoreOp::eStore,
			};

			vk::RenderingAttachmentInfo pickingAttachmentInfo{
				.imageView = s_Data->PickingImage.ImageView,
				.imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
				.loadOp = vk::AttachmentLoadOp::eLoad,
				.storeOp = vk::AttachmentStoreOp::eStore,
			};

			const std::array<vk::RenderingAttachmentInfo, 2> colorAttachments = {
				colorAttachmentInfo,
				pickingAttachmentInfo
			};

			vk::RenderingAttachmentInfo depthAttachmentInfo{
				.imageView = s_Data->DepthImage.ImageView,
				.imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
				.loadOp = vk::AttachmentLoadOp::eLoad,
				.storeOp = vk::AttachmentStoreOp::eDontCare,
			};

			const vk::RenderingInfo renderingInfo{
				.renderArea = renderArea,
				.layerCount = 1,
			    .colorAttachmentCount = static_cast<uint32_t>(colorAttachments.size()),
				.pColorAttachments = colorAttachments.data(),
				.pDepthAttachment = &depthAttachmentInfo
			};

			cmd.beginRendering(renderingInfo);

			cmd.setViewport(0, viewport);

			cmd.setScissor(0, renderArea);

			s_Data->PBRTransparentPipeline->Bind(cmd);

			//{
			//	const auto meshView = scene->m_Registry.view<TransformComponent, StaticMeshComponent>();
			//	int i = 0;
			//	for (const auto entity : meshView)
			//	{
			//		auto& [transform, staticMesh] = meshView.get<TransformComponent, StaticMeshComponent>(entity);
			//		if (!staticMesh.Visible || !staticMesh.StaticMesh || !staticMesh.MeshMaterial || !staticMesh.MeshMaterial->IsTransparent())
			//			continue;

			//		UpdatePerObjectUniformBuffer(currentImage, static_cast<uint32_t>(i), transform.GetTransform(), *staticMesh.MeshMaterial);
			//		uint32_t dynamicOffset = static_cast<uint32_t>(i * s_Data->DynamicAlignment);

			//		cmd.bindDescriptorSets(
			//			vk::PipelineBindPoint::eGraphics,
			//			*s_Data->PBRPipelineLayout,
			//			0,
			//			{ s_Data->DescriptorSets[currentImage].scene, staticMesh.MeshMaterial->DescriptorSet },
			//			{ dynamicOffset });

			//		staticMesh.StaticMesh->Draw(cmd);

			//		++i;
			//	}
			//}

			cmd.endRendering();

			WriteGPUTimestamp(cmd, frameIndex, static_cast<uint32_t>(GPUTimestampQuery::TransparentEnd));

			KBR_CORE_TRACE("Transparent pass done!");
		}

		HandleMousePickingReadback(cmd);

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
			.image = s_Data->ColorImage.Image,
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

		WriteGPUTimestamp(cmd, frameIndex, static_cast<uint32_t>(GPUTimestampQuery::FrameEnd));
		s_Data->PendingRender.IsValid = false;

		ResolveGPUTimings(frameIndex);
	}

	void Renderer::RenderSceneRuntime(const Ref<Scene>& scene, const Camera& mainCamera,
	                                  const glm::mat4& mainCameraTransform) 
	{
		const glm::vec3 camPos = mainCameraTransform[3];
		RenderScene(scene, mainCamera.GetProjectionMatrix(), mainCamera.GetViewMatrix(), camPos);
	}

	void Renderer::CreateDefaultMaterials() 
	{
		KBR_CORE_ASSERT(s_Data, "Renderer not initialized!");

		s_Data->MaterialRegistry.Add("Gold", CreateRef<Material>("Gold", glm::vec3(1.0f, 0.765557f, 0.336057f), 0.1f, 1.0f));
		s_Data->MaterialRegistry.Add("Copper", CreateRef<Material>("Copper", glm::vec3(0.955008f, 0.637427f, 0.538163f), 0.1f, 1.0f));
		s_Data->MaterialRegistry.Add("Chromium", CreateRef<Material>("Chromium", glm::vec3(0.549585f, 0.556114f, 0.554256f), 0.1f, 1.0f));
		s_Data->MaterialRegistry.Add("Nickel", CreateRef<Material>("Nickel", glm::vec3(0.659777f, 0.608679f, 0.525649f), 0.1f, 1.0f));
		s_Data->MaterialRegistry.Add("Titanium", CreateRef<Material>("Titanium", glm::vec3(0.541931f, 0.496791f, 0.449419f), 0.1f, 1.0f));
		s_Data->MaterialRegistry.Add("Cobalt", CreateRef<Material>("Cobalt", glm::vec3(0.662124f, 0.654864f, 0.633732f), 0.1f, 1.0f));
		s_Data->MaterialRegistry.Add("Platinum", CreateRef<Material>("Platinum", glm::vec3(0.672411f, 0.637331f, 0.585456f), 0.1f, 1.0f));
		s_Data->MaterialRegistry.Add("White", CreateRef<Material>("White", glm::vec3(1.0f), 1.0f, 0.0f));
		s_Data->MaterialRegistry.Add("Red", CreateRef<Material>("Red", glm::vec3(1.0f, 0.0f, 0.0f), 0.1f, 1.0f));
		s_Data->MaterialRegistry.Add("Blue", CreateRef<Material>("Blue", glm::vec3(0.0f, 0.0f, 1.0f), 0.1f, 1.0f));
		s_Data->MaterialRegistry.Add("Black", CreateRef<Material>("Black", glm::vec3(0.0f), 0.1f, 1.0f));
	}

	void Renderer::CreateResources()
	{
		KBR_CORE_ASSERT(s_Data, "Renderer not initialized!");

		// TODO: Set default skybox texture if none is set by the user
		/*s_Data->Skybox.SkyboxTexture = TextureCube::FromFile(
			"assets/textures/hdr/pisa_cube.ktx",
			vk::Format::eR16G16B16A16Sfloat,
			vk::ImageUsageFlagBits::eSampled
		);*/
		s_Data->Skybox.SkyboxTexture = TextureCube::FromFile("Assets/Textures/hdr/pisa_cube.ktx");

		CreateSkyboxResources();
		
		PrepareUniformBuffers();

		auto& context = VulkanContext::Get();
		const auto& device = context.GetDevice();

		const vk::SemaphoreTypeCreateInfo timelineSemaphoreTypeInfo{
			.semaphoreType = vk::SemaphoreType::eTimeline,
			.initialValue = 0
		};
		const vk::SemaphoreCreateInfo timelineSemaphoreCreateInfo{
			.pNext = &timelineSemaphoreTypeInfo
		};
		s_Data->MousePickingReadback.TimelineSemaphore = vk::raii::Semaphore(device, timelineSemaphoreCreateInfo);
		context.SetObjectDebugName(s_Data->MousePickingReadback.TimelineSemaphore, "Mouse Picking Timeline Semaphore");

		const auto queueFamilyInfo = context.GetQueueFamilyInfo();
		const auto queueFamilyProperties = context.GetPhysicalDevice().getQueueFamilyProperties();

		s_Data->SupportsGPUTimestamps = false;
		if (queueFamilyInfo.graphics < queueFamilyProperties.size())
		{
			s_Data->SupportsGPUTimestamps = queueFamilyProperties[queueFamilyInfo.graphics].timestampValidBits > 0;
		}

		s_Data->GPUTimestampPeriodNanoseconds = context.GetProperties().properties.limits.timestampPeriod;

		if (s_Data->SupportsGPUTimestamps)
		{
            s_Data->GPUTimestampQueryPools.clear();
			s_Data->GPUTimestampQueryPools.reserve(context.GetMaxFramesInFlight());

            constexpr vk::QueryPoolCreateInfo queryPoolInfo{
				.flags = vk::QueryPoolCreateFlagBits::eResetKHR,
				.queryType = vk::QueryType::eTimestamp,
				.queryCount = static_cast<uint32_t>(GPUTimestampQuery::Count)
			};

			for (uint32_t i = 0; i < context.GetMaxFramesInFlight(); ++i)
			{
				s_Data->GPUTimestampQueryPools.emplace_back(device, queryPoolInfo);
				context.SetObjectDebugName(s_Data->GPUTimestampQueryPools.back(), "Renderer GPU Timestamp Query Pool[" + std::to_string(i) + "]");
			}
		}

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
				.maxLod = vk::LodClampNone,
				.borderColor = vk::BorderColor::eIntOpaqueBlack,
				.unnormalizedCoordinates = vk::False
			};
			s_Data->ColorSampler = vk::raii::Sampler{ device, samplerInfo };

			context.SetObjectDebugName(s_Data->ColorSampler, "Color Texture Sampler");

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
			s_Data->ShadowMapSampler = vk::raii::Sampler{ device, shadowSamplerInfo };

			context.SetObjectDebugName(s_Data->ShadowMapSampler, "Shadow Map Sampler");
		}

		// Create shared pipeline states

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
						s_Data->ShadowMap.Size,
						s_Data->ShadowMap.Size,
						shadowMapMipLevels,
						vk::SampleCountFlagBits::e1,
						shadowMapFormat,
						vk::ImageTiling::eOptimal,
						vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled,
						vk::MemoryPropertyFlagBits::eDeviceLocal,
						s_Data->ShadowMap.Image,
						s_Data->ShadowMap.ImageMemory);

			context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkImage>(*s_Data->ShadowMap.Image)),
									   vk::ObjectType::eImage,
									   "Shadow Map Image");
			context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkDeviceMemory>(*s_Data->ShadowMap.ImageMemory)),
									   vk::ObjectType::eDeviceMemory,
									   "Shadow Map Image Memory");

			s_Data->ShadowMap.ImageView = CreateImageView(device,
												   s_Data->ShadowMap.Image,
												   shadowMapFormat,
												   vk::ImageAspectFlagBits::eDepth,
												   shadowMapMipLevels);

			context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkImageView>(*s_Data->ShadowMap.ImageView)),
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
				.pSetLayouts = &*s_Data->DescriptorSetLayouts.scene,
				.pushConstantRangeCount = 0,
				.pPushConstantRanges = nullptr
			};

			s_Data->ShadowMap.PipelineLayout = vk::raii::PipelineLayout{ device, pipelineLayoutInfo };
			context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkPipelineLayout>(*s_Data->ShadowMap.PipelineLayout)),
									   vk::ObjectType::ePipelineLayout,
									   "Shadow Map Pipeline Layout");

			// Create shader for shadow mapping
			Ref<Shader> shadowMapShader = CreateRef<Shader>("shadowmap", "ShadowMap");

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

			std::vector shadowMapDynamicState = {
				vk::DynamicState::eViewport,
				vk::DynamicState::eScissor,
				vk::DynamicState::eDepthBias,
			};

			const auto bindingDesc = Vertex::GetBindingDescription();
			const auto attributeDescs = Vertex::GetAttributeDescriptions();

			GraphicsPipelineSpecification shadowPipelineSpec{};
			shadowPipelineSpec.Name = "Shadow Map Pipeline";
			shadowPipelineSpec.Shader = shadowMapShader;
			shadowPipelineSpec.PipelineLayout = *s_Data->ShadowMap.PipelineLayout;
			shadowPipelineSpec.BindingDescription = bindingDesc;
			shadowPipelineSpec.InputAttributeDescriptions = { attributeDescs.begin(), attributeDescs.end() };
			shadowPipelineSpec.SampleCount = vk::SampleCountFlagBits::e1;
			shadowPipelineSpec.CullMode = CullMode::Front;
			shadowPipelineSpec.EnableDepthClamp = true;
			shadowPipelineSpec.EnableDepthBias = true;
			shadowPipelineSpec.EnableDepthTest = true;
			shadowPipelineSpec.EnableDepthWrite = true;
			shadowPipelineSpec.DepthTestFunc = DepthTestFunc::LessOrEqual;
			shadowPipelineSpec.DepthAttachmentFormat = shadowMapFormat;
			shadowPipelineSpec.DynamicStates = shadowMapDynamicState;

			s_Data->ShadowMap.Pipeline = CreateRef<GraphicsPipeline>(shadowPipelineSpec);
		}

		// Create the opaque and transparent pipeline resources
		{
			std::vector dynamicStates = {
				vk::DynamicState::eViewport,
				vk::DynamicState::eScissor
			};

			const vk::PipelineDynamicStateCreateInfo dynamicStateInfo{
				.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
				.pDynamicStates = dynamicStates.data()
			};

			const vk::Format colorFormat = context.FindSupportedFormat(
				{ vk::Format::eR32G32B32A32Sfloat, vk::Format::eR32G32B32A32Uint },
				vk::ImageTiling::eOptimal,
				vk::FormatFeatureFlagBits::eColorAttachment | vk::FormatFeatureFlagBits::eColorAttachmentBlend | vk::FormatFeatureFlagBits::eSampledImage
			);
			const vk::Format pickingFormat = context.FindSupportedFormat(
				{ vk::Format::eR32Uint },
				vk::ImageTiling::eOptimal,
				vk::FormatFeatureFlagBits::eColorAttachment | vk::FormatFeatureFlagBits::eTransferSrc
			);

			constexpr uint32_t initialImageWidth = 1920;
			constexpr uint32_t initialImageHeight = 1080;
			constexpr uint32_t mipLevels = 1;

			CreateImage(device,
						initialImageWidth,
						initialImageHeight,
						mipLevels,
						vk::SampleCountFlagBits::e1,
						colorFormat,
						vk::ImageTiling::eOptimal,
						vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
						vk::MemoryPropertyFlagBits::eDeviceLocal,
						s_Data->ColorImage.Image,
						s_Data->ColorImage.ImageMemory);

			context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkImage>(*s_Data->ColorImage.Image)),
									   vk::ObjectType::eImage,
									   "Color Attachment Image");

			context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkDeviceMemory>(*s_Data->ColorImage.ImageMemory)),
									   vk::ObjectType::eDeviceMemory,
									   "Color Attachment Image Memory");

			s_Data->ColorImage.ImageView = CreateImageView(device, s_Data->ColorImage.Image, colorFormat, vk::ImageAspectFlagBits::eColor, mipLevels);

			context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkImageView>(*s_Data->ColorImage.ImageView)),
									   vk::ObjectType::eImageView,
									   "Color Attachment Image View");

			CreateImage(device,
						initialImageWidth,
						initialImageHeight,
						mipLevels,
						vk::SampleCountFlagBits::e1,
						pickingFormat,
						vk::ImageTiling::eOptimal,
						vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc,
						vk::MemoryPropertyFlagBits::eDeviceLocal,
						s_Data->PickingImage.Image,
						s_Data->PickingImage.ImageMemory);

			context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkImage>(*s_Data->PickingImage.Image)),
									   vk::ObjectType::eImage,
									   "Picking Attachment Image");

			context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkDeviceMemory>(*s_Data->PickingImage.ImageMemory)),
									   vk::ObjectType::eDeviceMemory,
									   "Picking Attachment Image Memory");

			s_Data->PickingImage.ImageView = CreateImageView(device, s_Data->PickingImage.Image, pickingFormat, vk::ImageAspectFlagBits::eColor, mipLevels);

			context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkImageView>(*s_Data->PickingImage.ImageView)),
									   vk::ObjectType::eImageView,
									   "Picking Attachment Image View");
			s_Data->PickingImageLayout = vk::ImageLayout::eUndefined;

			const vk::Format depthFormat = context.FindSupportedFormat(
				{ vk::Format::eD32Sfloat },
				vk::ImageTiling::eOptimal,
				vk::FormatFeatureFlagBits::eDepthStencilAttachment
			);

			CreateImage(
				device,
				initialImageWidth,
				initialImageHeight,
				mipLevels,
				vk::SampleCountFlagBits::e1,
				depthFormat,
				vk::ImageTiling::eOptimal,
				vk::ImageUsageFlagBits::eDepthStencilAttachment,
				vk::MemoryPropertyFlagBits::eDeviceLocal,
				s_Data->DepthImage.Image,
				s_Data->DepthImage.ImageMemory);

			context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkImage>(*s_Data->DepthImage.Image)),
									   vk::ObjectType::eImage,
									   "Depth Attachment Image");

			context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkDeviceMemory>(*s_Data->DepthImage.ImageMemory)),
									   vk::ObjectType::eDeviceMemory,
									   "Depth Attachment Image Memory");

			s_Data->DepthImage.ImageView = CreateImageView(device, s_Data->DepthImage.Image, depthFormat, vk::ImageAspectFlagBits::eDepth, mipLevels);

			context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkImageView>(*s_Data->DepthImage.ImageView)),
									   vk::ObjectType::eImageView,
									   "Depth Attachment Image View");

			const std::array<vk::DescriptorSetLayout, 2> setLayouts = {
				s_Data->DescriptorSetLayouts.scene,
				s_Data->DescriptorSetLayouts.textures
			};

			vk::PipelineLayoutCreateInfo pipelineLayoutInfo{
				.setLayoutCount = static_cast<uint32_t>(setLayouts.size()),
				.pSetLayouts = setLayouts.data(),
				.pushConstantRangeCount = 0,
				.pPushConstantRanges = nullptr
			};

			s_Data->PBRPipelineLayout = vk::raii::PipelineLayout{ device, pipelineLayoutInfo };

			context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkPipelineLayout>(*s_Data->PBRPipelineLayout)),
									   vk::ObjectType::ePipelineLayout,
									   "PBR Pipeline Layout");

			const auto bindingDesc = Vertex::GetBindingDescription();
			const auto attributeDescs = Vertex::GetAttributeDescriptions();

			Ref<Shader> pbrShader = CreateRef<Shader>("pbrtextured", "PBR");

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

			GraphicsPipelineSpecification opaquePipelineSpec{};
			opaquePipelineSpec.Name = "PBR Opaque Pipeline";
			opaquePipelineSpec.Shader = pbrShader;
			opaquePipelineSpec.PipelineLayout = *s_Data->PBRPipelineLayout;
			opaquePipelineSpec.BindingDescription = bindingDesc;
			opaquePipelineSpec.InputAttributeDescriptions = { attributeDescs.begin(), attributeDescs.end() };
			opaquePipelineSpec.SampleCount = vk::SampleCountFlagBits::e1;
			opaquePipelineSpec.CullMode = CullMode::Back;
			opaquePipelineSpec.EnableDepthClamp = false;
			opaquePipelineSpec.EnableDepthBias = false;
			opaquePipelineSpec.EnableDepthTest = true;
			opaquePipelineSpec.EnableDepthWrite = true;
			opaquePipelineSpec.DepthTestFunc = DepthTestFunc::LessOrEqual;
			opaquePipelineSpec.BlendModes = { BlendMode::None, BlendMode::None };
			opaquePipelineSpec.ColorAttachmentFormats = { colorFormat, pickingFormat };
			opaquePipelineSpec.DepthAttachmentFormat = depthFormat;
			opaquePipelineSpec.DynamicStates = dynamicStates;
			opaquePipelineSpec.SpecializationMapEntries = { { vk::ShaderStageFlagBits::eFragment, specializationInfo } };

			s_Data->PBROpaquePipeline = CreateRef<GraphicsPipeline>(opaquePipelineSpec);

			enablePCF = 1;
			opaquePipelineSpec.Name = "PBR Opaque Pipeline with PCF Shadows";
			s_Data->PBROpaquePipelinePCF = CreateRef<GraphicsPipeline>(opaquePipelineSpec);

			Ref<Shader> pbrRayQueryShadowsShader = CreateRef<Shader>("pbr_ray_query_shadows", "PBR Ray Query Shadows");

			uint32_t enableRayQuerySoftShadows = 0;
			vk::SpecializationMapEntry rayQuerySoftShadowsSpecializationMapEntry{
				.constantID = 0,
				.offset = 0,
				.size = sizeof(uint32_t)
			};
			vk::SpecializationInfo rayQuerySoftShadowsSpecializationInfo{
				.mapEntryCount = 1,
				.pMapEntries = &rayQuerySoftShadowsSpecializationMapEntry,
				.dataSize = sizeof(uint32_t),
				.pData = &enableRayQuerySoftShadows
			};

			opaquePipelineSpec.Name = "PBR Ray Query Shadows Pipeline";
			opaquePipelineSpec.Shader = pbrRayQueryShadowsShader;
			opaquePipelineSpec.SpecializationMapEntries = { { vk::ShaderStageFlagBits::eFragment, rayQuerySoftShadowsSpecializationInfo } };
			s_Data->PBRRayQueryShadowsPipeline = CreateRef<GraphicsPipeline>(opaquePipelineSpec);

			enableRayQuerySoftShadows = 1;
			opaquePipelineSpec.Name = "PBR Ray Query Soft hadows Pipeline";
			s_Data->PBRRayQuerySoftShadowsPipeline = CreateRef<GraphicsPipeline>(opaquePipelineSpec);

			Ref<Shader> normalDebugShader = CreateRef<Shader>("normaldebug", "NormalDebug");

			opaquePipelineSpec.Name = "Normal Debug Pipeline";
			opaquePipelineSpec.Shader = normalDebugShader;
			opaquePipelineSpec.SpecializationMapEntries = {};
			s_Data->NormalDebugPipeline = CreateRef<GraphicsPipeline>(opaquePipelineSpec);

			GraphicsPipelineSpecification transparentPipelineSpec{};
			transparentPipelineSpec.Name = "PBR Transparent Pipeline";
			transparentPipelineSpec.Shader = pbrShader;
			transparentPipelineSpec.PipelineLayout = *s_Data->PBRPipelineLayout;
			transparentPipelineSpec.BindingDescription = bindingDesc;
			transparentPipelineSpec.InputAttributeDescriptions = { attributeDescs.begin(), attributeDescs.end() };
			transparentPipelineSpec.SampleCount = vk::SampleCountFlagBits::e1;
			transparentPipelineSpec.CullMode = CullMode::None;
			transparentPipelineSpec.EnableDepthClamp = false;
			transparentPipelineSpec.EnableDepthBias = false;
			transparentPipelineSpec.EnableDepthTest = true;
			transparentPipelineSpec.EnableDepthWrite = false;
			transparentPipelineSpec.DepthTestFunc = DepthTestFunc::Less;
			transparentPipelineSpec.BlendModes = { BlendMode::AlphaBlend, BlendMode::None };
			transparentPipelineSpec.ColorAttachmentFormats = { colorFormat, pickingFormat };
			transparentPipelineSpec.DepthAttachmentFormat = depthFormat;
			transparentPipelineSpec.DynamicStates = dynamicStates;

			s_Data->PBRTransparentPipeline = CreateRef<GraphicsPipeline>(transparentPipelineSpec);

			Ref<Shader> skyboxShader = CreateRef<Shader>("skybox", "Skybox");

			GraphicsPipelineSpecification skyboxPipelineSpec{};
			skyboxPipelineSpec.Name = "Skybox Pipeline";
			skyboxPipelineSpec.Shader = skyboxShader;
			skyboxPipelineSpec.PipelineLayout = *s_Data->PBRPipelineLayout;
			skyboxPipelineSpec.BindingDescription = bindingDesc;
			skyboxPipelineSpec.InputAttributeDescriptions = { attributeDescs.begin(), attributeDescs.end() };
			skyboxPipelineSpec.SampleCount = vk::SampleCountFlagBits::e1;
			skyboxPipelineSpec.CullMode = CullMode::Front;
			skyboxPipelineSpec.EnableDepthClamp = false;
			skyboxPipelineSpec.EnableDepthBias = false;
			skyboxPipelineSpec.EnableDepthTest = true;
			skyboxPipelineSpec.EnableDepthWrite = false;
			skyboxPipelineSpec.DepthTestFunc = DepthTestFunc::LessOrEqual;
			skyboxPipelineSpec.BlendModes = { BlendMode::None, BlendMode::None };
			skyboxPipelineSpec.ColorAttachmentFormats = { colorFormat, pickingFormat };
			skyboxPipelineSpec.DepthAttachmentFormat = depthFormat;
			skyboxPipelineSpec.DynamicStates = dynamicStates;

			s_Data->SkyboxPipeline = CreateRef<GraphicsPipeline>(skyboxPipelineSpec);
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
				.image = s_Data->ColorImage.Image,
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
									   "EditorLayer Single Time Command Buffer for Color Image Layout Transition");
			cmd.pipelineBarrier2(dependencyInfo);
			context.EndSingleTimeCommands(cmd);
		}

		// Create descriptor set for the output image for ImGui rendering
		{
			s_Data->ColorOutputDescriptorSet = VulkanContext::GenerateImGuiDescriptorSet(s_Data->ColorSampler, s_Data->ColorImage.ImageView);

			context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkDescriptorSet>(s_Data->ColorOutputDescriptorSet)),
									   vk::ObjectType::eDescriptorSet,
									   "Color Output Descriptor Set for ImGui");

			s_Data->ShadowMapDescriptorSet = VulkanContext::GenerateImGuiDescriptorSet(s_Data->ColorSampler, s_Data->ShadowMap.ImageView);
			context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkDescriptorSet>(s_Data->ShadowMapDescriptorSet)),
									   vk::ObjectType::eDescriptorSet,
									   "Shadow Map Descriptor Set for ImGui");
		}

		for (uint32_t i = 0; i < Renderer::MousePickingReadbackFrameLag; ++i)
		{
			auto& slot = s_Data->MousePickingReadback.Slots[i];
			CreateBuffer(device,
						 sizeof(uint32_t),
						 vk::BufferUsageFlagBits::eTransferDst,
						 vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
						 slot.Buffer,
						 slot.Memory);

			slot.MappedData = slot.Memory.mapMemory(0, sizeof(uint32_t));

		}

		//m_OutputSize = m_ViewportSize;
	}

	void Renderer::ResizeResources(const uint32_t width, const uint32_t height) 
	{
		KBR_CORE_ASSERT(s_Data, "Renderer not initialized!");

		// Resize the color and depth image, the shadowmap image can keep its size
		auto& context = VulkanContext::Get();
		const auto& device = context.GetDevice();

		constexpr uint32_t mipLevels = 1;
		const vk::Format colorFormat = context.FindSupportedFormat(
			{ vk::Format::eR32G32B32A32Sfloat, vk::Format::eR32G32B32A32Uint },
			vk::ImageTiling::eOptimal,
			vk::FormatFeatureFlagBits::eColorAttachment | vk::FormatFeatureFlagBits::eColorAttachmentBlend | vk::FormatFeatureFlagBits::eSampledImage
		);
		const vk::Format pickingFormat = context.FindSupportedFormat(
			{ vk::Format::eR32Uint },
			vk::ImageTiling::eOptimal,
			vk::FormatFeatureFlagBits::eColorAttachment | vk::FormatFeatureFlagBits::eTransferSrc
		);

		device.waitIdle();

		// Destroy old resources
		VulkanContext::DestroyImGuiDescriptorSet(s_Data->ColorOutputDescriptorSet);

		s_Data->ColorImage.ImageView.clear();
		s_Data->ColorImage.Image.clear();
		s_Data->ColorImage.ImageMemory.clear();
        s_Data->PickingImage.ImageView.clear();
		s_Data->PickingImage.Image.clear();
		s_Data->PickingImage.ImageMemory.clear();
		s_Data->DepthImage.ImageView.clear();
		s_Data->DepthImage.Image.clear();
		s_Data->DepthImage.ImageMemory.clear();

		// Recreate resources with new size
		CreateImage(device,
					width,
					height,
					mipLevels,
					vk::SampleCountFlagBits::e1,
					colorFormat,
					vk::ImageTiling::eOptimal,
					vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
					vk::MemoryPropertyFlagBits::eDeviceLocal,
					s_Data->ColorImage.Image,
					s_Data->ColorImage.ImageMemory);

		context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkImage>(*s_Data->ColorImage.Image)),
								   vk::ObjectType::eImage,
								   "Color Attachment Image");

		context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkDeviceMemory>(*s_Data->ColorImage.ImageMemory)),
								   vk::ObjectType::eDeviceMemory,
								   "Color Attachment Image Memory");

		s_Data->ColorImage.ImageView = CreateImageView(device, s_Data->ColorImage.Image, colorFormat, vk::ImageAspectFlagBits::eColor, mipLevels);
		context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkImageView>(*s_Data->ColorImage.ImageView)),
								   vk::ObjectType::eImageView,
								   "Color Attachment Image View");

		CreateImage(device,
					width,
					height,
					mipLevels,
					vk::SampleCountFlagBits::e1,
					pickingFormat,
					vk::ImageTiling::eOptimal,
					vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc,
					vk::MemoryPropertyFlagBits::eDeviceLocal,
					s_Data->PickingImage.Image,
					s_Data->PickingImage.ImageMemory);

		context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkImage>(*s_Data->PickingImage.Image)),
								   vk::ObjectType::eImage,
								   "Picking Attachment Image");

		context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkDeviceMemory>(*s_Data->PickingImage.ImageMemory)),
								   vk::ObjectType::eDeviceMemory,
								   "Picking Attachment Image Memory");

		s_Data->PickingImage.ImageView = CreateImageView(device, s_Data->PickingImage.Image, pickingFormat, vk::ImageAspectFlagBits::eColor, mipLevels);
		context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkImageView>(*s_Data->PickingImage.ImageView)),
								   vk::ObjectType::eImageView,
								   "Picking Attachment Image View");

		const vk::Format depthFormat = context.FindSupportedFormat(
			{ vk::Format::eD32Sfloat },
			vk::ImageTiling::eOptimal,
			vk::FormatFeatureFlagBits::eDepthStencilAttachment
		);

		CreateImage(
			device,
			width,
			height,
			mipLevels,
			vk::SampleCountFlagBits::e1,
			depthFormat,
			vk::ImageTiling::eOptimal,
			vk::ImageUsageFlagBits::eDepthStencilAttachment,
			vk::MemoryPropertyFlagBits::eDeviceLocal,
			s_Data->DepthImage.Image,
			s_Data->DepthImage.ImageMemory);

		context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkImage>(*s_Data->DepthImage.Image)),
								   vk::ObjectType::eImage,
								   "Depth Attachment Image");

		context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkDeviceMemory>(*s_Data->DepthImage.ImageMemory)),
								   vk::ObjectType::eDeviceMemory,
								   "Depth Attachment Image Memory");

		s_Data->DepthImage.ImageView = CreateImageView(device, s_Data->DepthImage.Image, depthFormat, vk::ImageAspectFlagBits::eDepth, mipLevels);

		context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkImageView>(*s_Data->DepthImage.ImageView)),
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
				.image = s_Data->ColorImage.Image,
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
									   "EditorLayer Single Time Command Buffer for Color Image Layout Transition");
			cmd.pipelineBarrier2(dependencyInfo);
			context.EndSingleTimeCommands(cmd);
		}

		// Recreate descriptor set for the output image for ImGui rendering
		{
			KBR_CORE_ASSERT(s_Data->ColorSampler != nullptr && s_Data->ColorImage.ImageView != nullptr, "Sampler and image view has to be initialized to create an ImGui descriptor set");

			s_Data->ColorOutputDescriptorSet = VulkanContext::GenerateImGuiDescriptorSet(s_Data->ColorSampler, s_Data->ColorImage.ImageView);
			context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkDescriptorSet>(s_Data->ColorOutputDescriptorSet)),
									   vk::ObjectType::eDescriptorSet,
									   "Color Output Descriptor Set for ImGui");
		}

		s_Data->OutputSize = { static_cast<float>(width), static_cast<float>(height) };
	}

	void Renderer::RecompileShaders() 
	{
		const auto& context = VulkanContext::Get();
		context.WaitIdle();

		if (const auto& shadowMapPipeline = s_Data->ShadowMap.Pipeline)
			shadowMapPipeline->Recompile();
		if (const auto& pbrOpaquePipeline = s_Data->PBROpaquePipeline)
			pbrOpaquePipeline->Recompile();
		if (const auto& pbrOpaquePipelinePCF = s_Data->PBROpaquePipelinePCF)
			pbrOpaquePipelinePCF->Recompile();
		if (const auto& pbrRayQueryShadowsPipeline = s_Data->PBRRayQueryShadowsPipeline)
			pbrRayQueryShadowsPipeline->Recompile();
		if (const auto& pbrRayQuerySoftShadowsPipeline = s_Data->PBRRayQuerySoftShadowsPipeline)
			pbrRayQuerySoftShadowsPipeline->Recompile();
		if (const auto& normalDebugPipeline = s_Data->NormalDebugPipeline)
			normalDebugPipeline->Recompile();
		if (const auto& transparentPipeline = s_Data->PBRTransparentPipeline)
			transparentPipeline->Recompile();
		if (const auto& skyboxPipeline = s_Data->SkyboxPipeline)
			skyboxPipeline->Recompile();
	}

	glm::vec3 Renderer::GetLightPositionForShadowMapCalculation() 
	{
		KBR_CORE_ASSERT(s_Data, "Renderer not initialized!");

		return s_Data->ShadowMap.LightPosForCalculation;
	}

	DepthBias& Renderer::GetShadowMapDepthBiasSettings() 
	{
		KBR_CORE_ASSERT(s_Data, "Renderer not initialized!");

		return s_Data->DepthBias;
	}

	bool& Renderer::GetIsPCFEnabledForShadowMap() 
	{
		KBR_CORE_ASSERT(s_Data, "Renderer not initialized!");

		return s_Data->ShadowMap.EnablePCF;
	}

	bool& Renderer::GetDisplayDebugNormals() 
	{
		KBR_CORE_ASSERT(s_Data, "Renderer not initialized!");

		return s_Data->DisplayDebugNormals;
	}

	bool& Renderer::GetDisplaySkybox() 
	{
		KBR_CORE_ASSERT(s_Data, "Renderer not initialized!");

		return s_Data->Skybox.ShowSkybox;
	}

	bool& Renderer::GetUseRayQueryBasedShadows() 
	{
		KBR_CORE_ASSERT(s_Data, "Renderer not initialized!");

		return s_Data->UseRayQueryBasedShadows;
	}

	bool& Renderer::GetUseRayQueryBasedSoftShadows()
	{
		KBR_CORE_ASSERT(s_Data, "Renderer not initialized!");

		return s_Data->UseRayQueryBasedSoftShadows;
	}

	float& Renderer::GetGamma() 
	{
		KBR_CORE_ASSERT(s_Data, "Renderer not initialized!");

		return s_Data->UniformDataParams.gamma;
	}

	float& Renderer::GetExposure()
	{
		KBR_CORE_ASSERT(s_Data, "Renderer not initialized!");

		return s_Data->UniformDataParams.exposure;
	}

	glm::vec2 Renderer::GetOutputImageSize() 
	{
		KBR_CORE_ASSERT(s_Data, "Renderer not initialized!");

		return s_Data->OutputSize;
	}

	uint64_t Renderer::GetCompositedOutputImageID() 
	{
		KBR_CORE_ASSERT(s_Data->ColorOutputDescriptorSet, "ImGui descriptor set is not created for composited color output image!");

		return reinterpret_cast<uint64_t>(static_cast<VkDescriptorSet>(s_Data->ColorOutputDescriptorSet));
	}

	uint64_t Renderer::GetShadowMapDepthImageID() 
	{
		KBR_CORE_ASSERT(s_Data->ShadowMapDescriptorSet, "ImGui descriptor set is not created for shadow map depth image!");

		return reinterpret_cast<uint64_t>(static_cast<VkDescriptorSet>(s_Data->ShadowMapDescriptorSet));
	}

	void Renderer::RequestMousePickingPixel(const uint32_t x, const uint32_t y)
	{
		KBR_CORE_ASSERT(s_Data, "Renderer not initialized!");

		if (x >= static_cast<uint32_t>(s_Data->OutputSize.x) || y >= static_cast<uint32_t>(s_Data->OutputSize.y))
			return;

		s_Data->MousePickingReadback.RequestedPixel = { x, y };
		s_Data->MousePickingReadback.RequestPending = true;
	}

	std::optional<uint32_t> Renderer::GetMousePickingEntityID()
	{
		KBR_CORE_ASSERT(s_Data, "Renderer not initialized!");

		return s_Data->MousePickingReadback.LatestEntityID;
	}

	bool Renderer::ConsumePendingMousePickingTimelineSignal(vk::Semaphore& semaphore, uint64_t& value)
	{
		KBR_CORE_ASSERT(s_Data, "Renderer not initialized!");

		if (s_Data->MousePickingReadback.PendingTimelineSignalValue == 0 || s_Data->MousePickingReadback.TimelineSemaphore == nullptr)
			return false;

		semaphore = s_Data->MousePickingReadback.TimelineSemaphore;
		value = s_Data->MousePickingReadback.PendingTimelineSignalValue;
		s_Data->MousePickingReadback.PendingTimelineSignalValue = 0;
		return true;
	}

	GPUTimings Renderer::GetLatestGPUTimings()
	{
		KBR_CORE_ASSERT(s_Data, "Renderer not initialized!");

		return s_Data->LatestGPUTimings;
	}

	void Renderer::WriteGPUTimestamp(const vk::raii::CommandBuffer& cmd, const uint32_t frameIndex, const uint32_t index)
	{
		if (!s_Data->SupportsGPUTimestamps || frameIndex >= s_Data->GPUTimestampQueryPools.size() || s_Data->GPUTimestampQueryPools[frameIndex] == nullptr)
			return;

       cmd.writeTimestamp2(vk::PipelineStageFlagBits2::eBottomOfPipe, s_Data->GPUTimestampQueryPools[frameIndex], index);
	}

	void Renderer::ResolveGPUTimings(const uint32_t frameIndex)
	{
		if (!s_Data->SupportsGPUTimestamps || frameIndex >= s_Data->GPUTimestampQueryPools.size() || s_Data->GPUTimestampQueryPools[frameIndex] == nullptr)
		{
			s_Data->LatestGPUTimings.IsValid = false;
			return;
		}

		std::array<uint64_t, static_cast<size_t>(GPUTimestampQuery::Count)> timestamps{};
		const auto& device = VulkanContext::Get().GetDevice();
		const vk::Result result = static_cast<vk::Device>(device).getQueryPoolResults(
          s_Data->GPUTimestampQueryPools[frameIndex],
			0,
			static_cast<uint32_t>(timestamps.size()),
			timestamps.size() * sizeof(uint64_t),
			timestamps.data(),
			sizeof(uint64_t),
			vk::QueryResultFlagBits::e64);

		if (result != vk::Result::eSuccess)
		{
			s_Data->LatestGPUTimings.IsValid = false;
			return;
		}

		auto toMilliseconds = [&](const GPUTimestampQuery begin, const GPUTimestampQuery end) -> float
		{
			const auto beginTicks = timestamps[static_cast<size_t>(begin)];
			const auto endTicks = timestamps[static_cast<size_t>(end)];
			if (endTicks <= beginTicks)
				return 0.0f;

			const double deltaTicks = static_cast<double>(endTicks - beginTicks);
			const double nanoseconds = deltaTicks * static_cast<double>(s_Data->GPUTimestampPeriodNanoseconds);
			return static_cast<float>(nanoseconds * 1e-6);
		};

		s_Data->LatestGPUTimings.FrameMilliseconds = toMilliseconds(GPUTimestampQuery::FrameBegin, GPUTimestampQuery::FrameEnd);
		s_Data->LatestGPUTimings.ShadowPassMilliseconds = toMilliseconds(GPUTimestampQuery::ShadowBegin, GPUTimestampQuery::ShadowEnd);
		s_Data->LatestGPUTimings.OpaquePassMilliseconds = toMilliseconds(GPUTimestampQuery::OpaqueBegin, GPUTimestampQuery::OpaqueEnd);
		s_Data->LatestGPUTimings.TransparentPassMilliseconds = toMilliseconds(GPUTimestampQuery::TransparentBegin, GPUTimestampQuery::TransparentEnd);
		s_Data->LatestGPUTimings.IsValid = true;
	}

	void Renderer::ResetQueryPool(const vk::raii::CommandBuffer& cmd, const uint32_t frameIndex)
	{
		if (!s_Data->SupportsGPUTimestamps)
			return;

		KBR_CORE_ASSERT(frameIndex < s_Data->GPUTimestampQueryPools.size(), "Current frame index exceeds GPU Timestamp Query Pools size!");
		KBR_CORE_ASSERT(s_Data->GPUTimestampQueryPools[frameIndex] != nullptr, "GPU Timestamp Query Pool for current frame is null!");

		cmd.resetQueryPool(s_Data->GPUTimestampQueryPools[frameIndex], 0, static_cast<uint32_t>(GPUTimestampQuery::Count));
	}

	void Renderer::UpdateLights(const uint32_t currentImage) 
	{
		KBR_CORE_ASSERT(s_Data, "Renderer not initialized!");

		s_Data->UniformDataParams.lights[1] = glm::vec4{ 0.0f };
		s_Data->UniformDataParams.lights[2] = glm::vec4{ 0.0f };

		std::memcpy(s_Data->UniformBuffers[currentImage].params->GetMappedData(), &s_Data->UniformDataParams, sizeof(UniformDataParams));
	}

	void Renderer::UpdateSceneUniformBuffers(const uint32_t currentImage, const Camera* mainCamera) 
	{
		KBR_CORE_ASSERT(s_Data, "Renderer not initialized!");

		const glm::mat4& projection = mainCamera->GetProjectionMatrix();
		const glm::mat4& view = mainCamera->GetViewMatrix();

		s_Data->SceneUniformData.projection = projection;
		s_Data->SceneUniformData.view = view;
		s_Data->SceneUniformData.lightSpaceMatrix = CalculateLightSpaceMatrix();
		s_Data->SceneUniformData.camPos = mainCamera->GetPosition();
		std::memcpy(s_Data->UniformBuffers[currentImage].scene->GetMappedData(), &s_Data->SceneUniformData, sizeof(SceneUniformData));

		const glm::mat4 skyboxModel = glm::mat4(glm::mat3(view));
		s_Data->SkyboxData.model = skyboxModel;
		s_Data->SkyboxData.projection = projection;
		std::memcpy(s_Data->UniformBuffers[currentImage].skybox->GetMappedData(), &s_Data->SkyboxData, sizeof(SkyboxData));
	}

	void Renderer::UpdateSceneUniformBuffers(const uint32_t currentImage, const glm::mat4& view, const glm::mat4& projection,
		const glm::vec3& camPos) 
	{
		KBR_CORE_ASSERT(s_Data, "Renderer not initialized!");

		s_Data->SceneUniformData.projection = projection;
		s_Data->SceneUniformData.view = view;
		s_Data->SceneUniformData.lightSpaceMatrix = CalculateLightSpaceMatrix();
		s_Data->SceneUniformData.camPos = camPos;
		std::memcpy(s_Data->UniformBuffers[currentImage].scene->GetMappedData(), &s_Data->SceneUniformData, sizeof(SceneUniformData));

		const glm::mat4 skyboxModel = glm::mat4(glm::mat3(view));
		s_Data->SkyboxData.model = skyboxModel;
		s_Data->SkyboxData.projection = projection;
		std::memcpy(s_Data->UniformBuffers[currentImage].skybox->GetMappedData(), &s_Data->SkyboxData, sizeof(SkyboxData));
	}

    void Renderer::UpdatePerObjectUniformBuffer(const uint32_t currentImage, const uint32_t objectIndex, const glm::mat4& model,
												const Material& material, const uint32_t entityID) 
	{
		KBR_CORE_ASSERT(s_Data, "Renderer not initialized!");

		s_Data->PerObjectData = {
			.model = model,
			.worldNormal = glm::inverseTranspose(model),
		    .material = material.Params,
			.entityID = entityID
		};

		char* data = static_cast<char*>(s_Data->UniformBuffers[currentImage].perObject->GetMappedData());
		data += static_cast<size_t>(objectIndex) * s_Data->DynamicAlignment;

		std::memcpy(data, &s_Data->PerObjectData, sizeof(PerObjectData));
	}

	glm::mat4 Renderer::CalculateLightSpaceMatrix() 
	{
		KBR_CORE_ASSERT(s_Data, "Renderer not initialized!");

		constexpr float nearPlane = 0.1f;
		constexpr float farPlane = 100.0f;
		constexpr float orthoSize = 20.0f;
		glm::mat4 lightProjection = glm::ortho(-orthoSize, orthoSize, -orthoSize, orthoSize, nearPlane, farPlane);
		lightProjection[1][1] *= -1.0f;

		constexpr glm::vec3 sceneCenter = glm::vec3(0.0f, 0.0f, 0.0f);
		constexpr float lightDistance = 80.0f;

		/*const glm::vec3 lightDirRaw = glm::vec3(m_UniformDataParams.lights[0]);
		const glm::vec3 lightDir = glm::length2(lightDirRaw) > std::numeric_limits<float>::epsilon()
			? glm::normalize(lightDirRaw)
			: glm::vec3(0.0f, 1.0f, 0.0f);*/

		const glm::vec3 lightDir = glm::normalize(glm::vec3(s_Data->UniformDataParams.lights[0]));

		const glm::vec3 lightPos = sceneCenter + lightDir * lightDistance;
		s_Data->ShadowMap.LightPosForCalculation = lightPos;
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

	void Renderer::HandleMousePickingReadback(const vk::raii::CommandBuffer& cmd) 
	{
		const auto& device = VulkanContext::Get().GetDevice();
		const uint64_t completedTimelineValue = s_Data->MousePickingReadback.TimelineSemaphore != nullptr
			? static_cast<vk::Device>(device).getSemaphoreCounterValue(s_Data->MousePickingReadback.TimelineSemaphore)
			: 0;

		for (auto& readSlot : s_Data->MousePickingReadback.Slots)
		{
			if (readSlot.Pending && completedTimelineValue >= readSlot.TimelineValue)
			{
				const auto pickedEntity = *static_cast<const uint32_t*>(readSlot.MappedData);
				s_Data->MousePickingReadback.LatestEntityID = pickedEntity;
				readSlot.Pending = false;
			}
		}

		if (s_Data->MousePickingReadback.RequestPending)
		{
			auto& writeSlot = s_Data->MousePickingReadback.Slots[s_Data->MousePickingReadback.WriteIndex];

			vk::ImageMemoryBarrier2 toTransferSrcBarrier = {
				.srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
				.srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
				.dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
				.dstAccessMask = vk::AccessFlagBits2::eTransferRead,
				.oldLayout = vk::ImageLayout::eColorAttachmentOptimal,
				.newLayout = vk::ImageLayout::eTransferSrcOptimal,
				.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
				.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
				.image = s_Data->PickingImage.Image,
				.subresourceRange = {
					.aspectMask = vk::ImageAspectFlagBits::eColor,
					.baseMipLevel = 0,
					.levelCount = 1,
					.baseArrayLayer = 0,
					.layerCount = 1
				}
			};

			const vk::DependencyInfo toTransferDependencyInfo = {
				.dependencyFlags = {},
				.imageMemoryBarrierCount = 1,
				.pImageMemoryBarriers = &toTransferSrcBarrier
			};
			cmd.pipelineBarrier2(toTransferDependencyInfo);
			s_Data->PickingImageLayout = vk::ImageLayout::eTransferSrcOptimal;

            const uint32_t outputWidth = static_cast<uint32_t>(s_Data->OutputSize.x);
			const uint32_t outputHeight = static_cast<uint32_t>(s_Data->OutputSize.y);

			if (outputWidth == 0 || outputHeight == 0 ||
				s_Data->MousePickingReadback.RequestedPixel.x >= outputWidth ||
				s_Data->MousePickingReadback.RequestedPixel.y >= outputHeight)
			{
				s_Data->MousePickingReadback.RequestPending = false;
				s_Data->MousePickingReadback.LatestEntityID = std::numeric_limits<uint32_t>::max();
				return;
			}

			const vk::BufferImageCopy copyRegion{
				.bufferOffset = 0,
				.bufferRowLength = 0,
				.bufferImageHeight = 0,
				.imageSubresource = {
					.aspectMask = vk::ImageAspectFlagBits::eColor,
					.mipLevel = 0,
					.baseArrayLayer = 0,
					.layerCount = 1
				},
				.imageOffset = {
					.x = static_cast<int32_t>(s_Data->MousePickingReadback.RequestedPixel.x),
					.y = static_cast<int32_t>(s_Data->MousePickingReadback.RequestedPixel.y),
					.z = 0
				},
				.imageExtent = {
					.width = 1,
					.height = 1,
					.depth = 1
				}
			};

			cmd.copyImageToBuffer(
				s_Data->PickingImage.Image,
				vk::ImageLayout::eTransferSrcOptimal,
				writeSlot.Buffer,
				copyRegion);

			writeSlot.Pending = true;
            writeSlot.TimelineValue = ++s_Data->MousePickingReadback.TimelineValue;
			s_Data->MousePickingReadback.PendingTimelineSignalValue = writeSlot.TimelineValue;
			s_Data->MousePickingReadback.WriteIndex = (s_Data->MousePickingReadback.WriteIndex + 1) % Renderer::MousePickingReadbackFrameLag;
			s_Data->MousePickingReadback.RequestPending = false;
		}
	}

	void Renderer::PrepareUniformBuffers()
	{
		const auto& context = VulkanContext::Get();
		const auto properties = context.GetProperties().properties;

		s_Data->MinUniformBufferOffsetAlignment = properties.limits.minUniformBufferOffsetAlignment;

		s_Data->DynamicAlignment = sizeof(PerObjectData);
		if (s_Data->MinUniformBufferOffsetAlignment > 0)
		{
			s_Data->DynamicAlignment = (s_Data->DynamicAlignment + s_Data->MinUniformBufferOffsetAlignment - 1) & ~(s_Data->MinUniformBufferOffsetAlignment - 1);
		}

		for (auto& [scene, params, perObject, skybox] : s_Data->UniformBuffers)
		{
			scene = std::make_shared<UniformBuffer>(sizeof(SceneUniformData));

			params = std::make_shared<UniformBuffer>(sizeof(UniformDataParams));

			// TODO: Allocate a large enough buffer for a maximum number of objects, and allocate a bigger one if needed.
			constexpr size_t maxObjects = 1000;
			perObject = std::make_shared<UniformBuffer>(s_Data->DynamicAlignment * maxObjects);

			skybox = std::make_shared<UniformBuffer>(sizeof(SkyboxData));
		}
	}

	void Renderer::SetupDescriptors()
	{
		auto& context = VulkanContext::Get();
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
			},
			vk::DescriptorPoolSize{
				.type = vk::DescriptorType::eAccelerationStructureKHR,
				.descriptorCount = 2
			}
		};

		vk::DescriptorPoolCreateInfo poolInfo{
			.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
			.maxSets = 10,
			.poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
			.pPoolSizes = poolSizes.data()
		};

		s_Data->DescriptorPool = vk::raii::DescriptorPool{ device, poolInfo };
		context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkDescriptorPool>(*s_Data->DescriptorPool)),
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
			vk::DescriptorSetLayoutBinding{ // TLAS
				.binding = 7,
				.descriptorType = vk::DescriptorType::eAccelerationStructureKHR,
				.descriptorCount = 1,
				.stageFlags = vk::ShaderStageFlagBits::eFragment,
				.pImmutableSamplers = nullptr
			},
		};

		const vk::DescriptorSetLayoutCreateInfo layoutInfo{
			.bindingCount = static_cast<uint32_t>(bindings.size()),
			.pBindings = bindings.data()
		};


		s_Data->DescriptorSetLayouts.scene = vk::raii::DescriptorSetLayout{ device, layoutInfo };
		context.SetObjectDebugName(s_Data->DescriptorSetLayouts.scene, "PBR Descriptor Set Layout");

		std::vector<vk::DescriptorSetLayoutBinding> textureBindings = {
			vk::DescriptorSetLayoutBinding{ // Albedo map
				.binding = 0,
				.descriptorType = vk::DescriptorType::eCombinedImageSampler,
				.descriptorCount = 1,
				.stageFlags = vk::ShaderStageFlagBits::eFragment,
			},
			vk::DescriptorSetLayoutBinding{ // Normal map
				.binding = 1,
				.descriptorType = vk::DescriptorType::eCombinedImageSampler,
				.descriptorCount = 1,
				.stageFlags = vk::ShaderStageFlagBits::eFragment,
			},
			//vk::DescriptorSetLayoutBinding{ // Ambient occlusion map
			//	.binding = 2,
			//	.descriptorType = vk::DescriptorType::eCombinedImageSampler,
			//	.descriptorCount = 1,
			//	.stageFlags = vk::ShaderStageFlagBits::eFragment,
			//},
			//vk::DescriptorSetLayoutBinding{ // Metallic map
			//	.binding = 3,
			//	.descriptorType = vk::DescriptorType::eCombinedImageSampler,
			//	.descriptorCount = 1,
			//	.stageFlags = vk::ShaderStageFlagBits::eFragment,
			//},
			//vk::DescriptorSetLayoutBinding{ // Roughness map
			//	.binding = 4,
			//	.descriptorType = vk::DescriptorType::eCombinedImageSampler,
			//	.descriptorCount = 1,
			//	.stageFlags = vk::ShaderStageFlagBits::eFragment,
			//},
		};

		const vk::DescriptorSetLayoutCreateInfo textureLayoutInfo{
			.bindingCount = static_cast<uint32_t>(textureBindings.size()),
			.pBindings = textureBindings.data()
		};

		s_Data->DescriptorSetLayouts.textures = vk::raii::DescriptorSetLayout{ device, textureLayoutInfo };
		context.SetObjectDebugName(s_Data->DescriptorSetLayouts.textures, "Texture Descriptor Set Layout");

     const std::vector<vk::DescriptorSetLayout> sceneSetLayouts(
			s_Data->DescriptorSets.size(),
			*s_Data->DescriptorSetLayouts.scene);

		vk::DescriptorSetAllocateInfo allocInfo{
			.descriptorPool = *s_Data->DescriptorPool,
           .descriptorSetCount = static_cast<uint32_t>(s_Data->DescriptorSets.size()),
			.pSetLayouts = sceneSetLayouts.data()
		};

		std::vector<vk::raii::DescriptorSet> sceneDescriptorSets = device.allocateDescriptorSets(allocInfo);
		std::vector<vk::raii::DescriptorSet> skyboxDescriptorSets = device.allocateDescriptorSets(allocInfo);
		for (uint32_t i = 0; i < s_Data->UniformBuffers.size(); i++)
		{
			s_Data->DescriptorSets[i].scene = std::move(sceneDescriptorSets[i]);
			context.SetObjectDebugName(s_Data->DescriptorSets[i].scene, "PBR Descriptor Set[" + std::to_string(i) + "]");

			const vk::DescriptorBufferInfo sceneBufferInfo{
				.buffer = *s_Data->UniformBuffers[i].scene->GetBuffer(),
				.offset = 0,
				.range = sizeof(SceneUniformData)
			};

			const vk::DescriptorBufferInfo paramsBufferInfo{
				.buffer = *s_Data->UniformBuffers[i].params->GetBuffer(),
				.offset = 0,
				.range = sizeof(UniformDataParams)
			};

			const vk::DescriptorBufferInfo perObjectBufferInfo{
				.buffer = *s_Data->UniformBuffers[i].perObject->GetBuffer(),
				.offset = 0,
				.range = sizeof(PerObjectData)
			};

			const vk::DescriptorImageInfo shadowMapImageInfo{
				.sampler = *s_Data->ShadowMapSampler,
				.imageView = *s_Data->ShadowMap.ImageView,
				.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
			};

			const vk::DescriptorImageInfo skyboxImageInfo{
				.sampler = *s_Data->Skybox.SkyboxTexture->GetSampler(),
				.imageView = *s_Data->Skybox.SkyboxTexture->GetImageView(),
				.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
			};

			const vk::DescriptorBufferInfo skyboxBufferInfo{
				.buffer = *s_Data->UniformBuffers[i].skybox->GetBuffer(),
				.offset = 0,
				.range = sizeof(SkyboxData)
			};

			const auto& tlas = s_Data->RayTracingCache.GetTLAS(i);
			const vk::WriteDescriptorSetAccelerationStructureKHR asInfo{
				.accelerationStructureCount = 1,
				.pAccelerationStructures = &tlas
			};

			const std::vector descriptorWrites = {
				vk::WriteDescriptorSet{
					.dstSet = *s_Data->DescriptorSets[i].scene,
					.dstBinding = 0,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = vk::DescriptorType::eUniformBuffer,
					.pBufferInfo = &sceneBufferInfo
				},
				vk::WriteDescriptorSet{
					.dstSet = *s_Data->DescriptorSets[i].scene,
					.dstBinding = 1,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = vk::DescriptorType::eUniformBuffer,
					.pBufferInfo = &paramsBufferInfo
				},
				vk::WriteDescriptorSet{
					.dstSet = *s_Data->DescriptorSets[i].scene,
					.dstBinding = 2,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = vk::DescriptorType::eUniformBufferDynamic,
					.pBufferInfo = &perObjectBufferInfo
				},
				vk::WriteDescriptorSet{
					.dstSet = *s_Data->DescriptorSets[i].scene,
					.dstBinding = 3,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = vk::DescriptorType::eCombinedImageSampler,
					.pImageInfo = &shadowMapImageInfo
				},
				vk::WriteDescriptorSet{
					.dstSet = *s_Data->DescriptorSets[i].scene,
					.dstBinding = 4,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = vk::DescriptorType::eCombinedImageSampler,
					.pImageInfo = &s_Data->Skybox.IrradianceCubeTexture->GetDescriptorInfo()
				},
				vk::WriteDescriptorSet{
					.dstSet = *s_Data->DescriptorSets[i].scene,
					.dstBinding = 5,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = vk::DescriptorType::eCombinedImageSampler,
					.pImageInfo = &s_Data->Skybox.LutBrdfTexture->GetDescriptorInfo()
				},
				vk::WriteDescriptorSet{
					.dstSet = *s_Data->DescriptorSets[i].scene,
					.dstBinding = 6,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = vk::DescriptorType::eCombinedImageSampler,
					.pImageInfo = &s_Data->Skybox.PrefilteredCubeTexture->GetDescriptorInfo()
				},
				vk::WriteDescriptorSet{
					.pNext = &asInfo,
					.dstSet = *s_Data->DescriptorSets[i].scene,
					.dstBinding = 7,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = vk::DescriptorType::eAccelerationStructureKHR,
				}
			};

			device.updateDescriptorSets(descriptorWrites, {});

			s_Data->DescriptorSets[i].skybox = std::move(skyboxDescriptorSets[i]);
			context.SetObjectDebugName(s_Data->DescriptorSets[i].skybox, "Skybox Descriptor Set[" + std::to_string(i) + "]");

			const std::vector skyboxDescriptorWrites = {
				vk::WriteDescriptorSet{
					.dstSet = *s_Data->DescriptorSets[i].skybox,
					.dstBinding = 0,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = vk::DescriptorType::eUniformBuffer,
					.pBufferInfo = &skyboxBufferInfo
				},
				vk::WriteDescriptorSet{
					.dstSet = *s_Data->DescriptorSets[i].skybox,
					.dstBinding = 1,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = vk::DescriptorType::eUniformBuffer,
					.pBufferInfo = &paramsBufferInfo
				},
				vk::WriteDescriptorSet{
					.dstSet = *s_Data->DescriptorSets[i].skybox,
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

	void Renderer::CreateSkyboxResources() 
	{
		KBR_CORE_ASSERT(s_Data->Skybox.SkyboxTexture != nullptr, "Skybox texture has to be set before creating skybox resources");

		// TODO: Set default skybox texture if none is set by the user

		if (s_Data->Skybox.SkyboxMesh == nullptr)
		{
			//The project has not been initialized this far
			//s_Data->Skybox.SkyboxMesh = AssetManager::GetDefaultCubeMesh();
			s_Data->Skybox.SkyboxMesh = CreateRef<Mesh>(ModelLoader::LoadModel("Assets/Models/cube.gltf", None));
		}
		if (s_Data->Skybox.LutBrdfTexture == nullptr)
		{
			s_Data->Skybox.LutBrdfTexture = CreateRef<Texture2D>();
			SkyboxUtils::GenerateBRDFLUT(*s_Data->Skybox.LutBrdfTexture);
		}

		s_Data->Skybox.IrradianceCubeTexture = CreateRef<TextureCube>();
		s_Data->Skybox.PrefilteredCubeTexture = CreateRef<TextureCube>();

		SkyboxUtils::GenerateIrradianceCube(*s_Data->Skybox.IrradianceCubeTexture, s_Data->Skybox.SkyboxTexture->descriptor, *s_Data->Skybox.SkyboxMesh);
		SkyboxUtils::GeneratePrefilteredEnvMap(*s_Data->Skybox.PrefilteredCubeTexture, s_Data->Skybox.SkyboxTexture->descriptor, *s_Data->Skybox.SkyboxMesh);
	}
}
