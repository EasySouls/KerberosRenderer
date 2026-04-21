#include "kbrpch.hpp"
#include "Renderer.hpp"

#include "MaterialRegistry.hpp"
#include "ModelLoader.hpp"
#include "SkyboxUtils.hpp"
#include "Buffer.hpp"
#include "VulkanContext.hpp"
#include "Shaders/Shader.hpp"
#include "GraphicsPipeline.hpp"
#include "RayTracingSceneCache.hpp"
#include "Utils.hpp"
#include "Scene/Components/PhysicsComponents.hpp"

#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>
#include <limits>
#include <numbers>
#include <cmath>

namespace
{
	using namespace Kerberos;
	constexpr glm::vec3 ColliderDebugColor(0.0f, 1.0f, 0.0f);
	constexpr uint32_t ColliderDebugMaxVertices = 65536;

	glm::mat4 GetWorldTransformWithoutScale(const TransformComponent& transform)
	{
		const glm::vec3 position = glm::vec3(transform.WorldTransform[3]);

		glm::mat3 rotation = glm::mat3(transform.WorldTransform);
		for (int i = 0; i < 3; ++i)
		{
			const float length = glm::length(rotation[i]);
			rotation[i] = length > 1e-6f ? rotation[i] / length : glm::vec3(0.0f);
		}

		glm::mat4 rotationMatrix(1.0f);
		rotationMatrix[0] = glm::vec4(rotation[0], 0.0f);
		rotationMatrix[1] = glm::vec4(rotation[1], 0.0f);
		rotationMatrix[2] = glm::vec4(rotation[2], 0.0f);
		rotationMatrix[3] = glm::vec4(position, 1.0f);
		return rotationMatrix;
	}

	LineVertex MakeLineVertex(const glm::vec3& position)
	{
		return { .Position = position, .Color = ColliderDebugColor };
	}

	void AddLine(std::vector<LineVertex>& vertices, const glm::vec3& a, const glm::vec3& b)
	{
		vertices.push_back(MakeLineVertex(a));
		vertices.push_back(MakeLineVertex(b));
	}

	void AddBoxLines(std::vector<LineVertex>& vertices, const glm::mat4& transform, const glm::vec3& halfExtents)
	{
		const std::array<glm::vec3, 8> corners = {
			glm::vec3(-halfExtents.x, -halfExtents.y, -halfExtents.z), glm::vec3(halfExtents.x, -halfExtents.y, -halfExtents.z),
			glm::vec3(halfExtents.x, halfExtents.y, -halfExtents.z),   glm::vec3(-halfExtents.x, halfExtents.y, -halfExtents.z),
			glm::vec3(-halfExtents.x, -halfExtents.y, halfExtents.z),  glm::vec3(halfExtents.x, -halfExtents.y, halfExtents.z),
			glm::vec3(halfExtents.x, halfExtents.y, halfExtents.z),    glm::vec3(-halfExtents.x, halfExtents.y, halfExtents.z)
		};
		std::array<glm::vec3, 8> worldCorners{};
		for (uint32_t i = 0; i < corners.size(); ++i)
		{
			worldCorners[i] = glm::vec3(transform * glm::vec4(corners[i], 1.0f));
		}

		constexpr std::array<std::pair<uint32_t, uint32_t>, 12> edges = {
			std::pair{0, 1}, std::pair{1, 2}, std::pair{2, 3}, std::pair{3, 0},
			std::pair{4, 5}, std::pair{5, 6}, std::pair{6, 7}, std::pair{7, 4},
			std::pair{0, 4}, std::pair{1, 5}, std::pair{2, 6}, std::pair{3, 7}
		};
		for (const auto& [a, b] : edges)
		{
			AddLine(vertices, worldCorners[a], worldCorners[b]);
		}
	}

	void AddCircleLines(std::vector<LineVertex>& vertices, const glm::mat4& transform, const int axisA, const int axisB, const float radius, const uint32_t segments = 32)
	{
		glm::vec3 previous(0.0f);
		bool hasPrevious = false;

		for (uint32_t i = 0; i <= segments; ++i)
		{
			constexpr float twoPi = std::numbers::pi_v<float> * 2.0f;
			const float t = static_cast<float>(i) / static_cast<float>(segments);
			const float angle = twoPi * t;
			glm::vec3 local(0.0f);
			local[axisA] = std::cos(angle) * radius;
			local[axisB] = std::sin(angle) * radius;
			const glm::vec3 current = glm::vec3(transform * glm::vec4(local, 1.0f));
			if (hasPrevious)
			{
				AddLine(vertices, previous, current);
			}
			previous = current;
			hasPrevious = true;
		}
	}

	void AddCapsuleLines(std::vector<LineVertex>& vertices, const glm::mat4& transform, const float radius, const float halfHeight)
	{
		const glm::mat4 topCenter = transform * glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, halfHeight, 0.0f));
		const glm::mat4 bottomCenter = transform * glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -halfHeight, 0.0f));

		AddCircleLines(vertices, topCenter, 0, 2, radius);
		AddCircleLines(vertices, bottomCenter, 0, 2, radius);

		const std::array<glm::vec3, 4> sidePoints = {
			glm::vec3(radius, 0.0f, 0.0f), glm::vec3(-radius, 0.0f, 0.0f),
			glm::vec3(0.0f, 0.0f, radius), glm::vec3(0.0f, 0.0f, -radius)
		};
		for (const glm::vec3& point : sidePoints)
		{
			const glm::vec3 top = glm::vec3(topCenter * glm::vec4(point, 1.0f));
			const glm::vec3 bottom = glm::vec3(bottomCenter * glm::vec4(point, 1.0f));
			AddLine(vertices, top, bottom);
		}
	}

	struct ShadowMap
	{
		constexpr static int CascadeCount = 4;

		vk::raii::Image Image = nullptr;
		vk::raii::DeviceMemory ImageMemory = nullptr;
		vk::raii::ImageView ReadImageView = nullptr;
		std::array<vk::raii::ImageView, ShadowMap::CascadeCount> WriteImageViews{ nullptr, nullptr, nullptr, nullptr };
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
		glm::mat4 lightSpaceMatrices[ShadowMap::CascadeCount]{ 0.f };
		glm::vec4 cascadeSplits{ 0.f };
		alignas(16) glm::vec3 camPos{ 0.f };
		uint32_t lightCount = 0;
	};

	struct GlobalLighting
	{
		alignas(16) glm::vec4 sunLight{ 0.0f, 0.0f, 0.0f, 0.0f };
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
		Ref<UniformBuffer> scene;
		Ref<UniformBuffer> globalLighting;
		Ref<UniformBuffer> perObject;
		Ref<UniformBuffer> skybox;
	};

	struct StorageBuffers
	{
		Ref<StorageBuffer> Lights;
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
		std::function<std::pair<std::vector<glm::mat4>, glm::vec4>(const glm::vec3&, const std::function<glm::vec4(float)>&)> CalculateLightSpaceMatricesFunc;
		bool IsValid = false;
	};

	enum class GPUTimestampQuery : uint32_t
	{
		FrameBegin = 0,
		DepthPrePassBegin,
		DepthPrePassEnd,
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
		struct ColliderLineBuffer
		{
			vk::raii::Buffer Buffer = nullptr;
			vk::raii::DeviceMemory Memory = nullptr;
			void* MappedData = nullptr;
		};

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
		Ref<GraphicsPipeline> DepthPrePassPipeline = nullptr;
		Ref<GraphicsPipeline> PBROpaquePipeline = nullptr;
		Ref<GraphicsPipeline> PBROpaquePipelinePCF = nullptr;
		Ref<GraphicsPipeline> PBRTransparentPipeline = nullptr;
		Ref<GraphicsPipeline> SkyboxPipeline = nullptr;
		Ref<GraphicsPipeline> NormalDebugPipeline = nullptr;
		Ref<GraphicsPipeline> ColliderLinesPipeline = nullptr;
		Ref<GraphicsPipeline> PBRRayQueryShadowsPipeline = nullptr;
		Ref<GraphicsPipeline> PBRRayQuerySoftShadowsPipeline = nullptr;

		vk::raii::Sampler ColorSampler = nullptr;
		vk::raii::Sampler ShadowMapSampler = nullptr;

		SceneUniformData SceneUniformData{};
		GlobalLighting GlobalLightingData{};
		PerObjectData PerObjectData{};
		SkyboxData SkyboxData{};

		std::array<UniformBufferObject, VulkanContext::MaxFramesInFlight> UniformBuffers{};
		std::array<StorageBuffers, VulkanContext::MaxFramesInFlight> StorageBuffers{};

		std::array<DescriptorSets, VulkanContext::MaxFramesInFlight> DescriptorSets{};
		std::array<ColliderLineBuffer, VulkanContext::MaxFramesInFlight> ColliderLineBuffers{};

		// Dynamic uniform buffer related members
		VkDeviceSize MinUniformBufferOffsetAlignment = 0;
		uint64_t DynamicAlignment = 0;

		vk::DescriptorSet ColorOutputDescriptorSet = nullptr;
		std::array<vk::DescriptorSet, ShadowMap::CascadeCount> ShadowMapDescriptorSet = { nullptr };

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
		bool DisplayPhysicsColliders = false;

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
		KBR_CORE_INFO("Size of GlobalLighting: {} bytes", sizeof(GlobalLighting));
		KBR_CORE_INFO("Size of PerObjectData: {} bytes", sizeof(PerObjectData));
		KBR_CORE_INFO("Size of SkyboxData: {} bytes", sizeof(SkyboxData));
		KBR_CORE_INFO("Size of material UniformBlock: {} bytes", sizeof(Material::UniformBlock));

		CreateDefaultMaterials();

		// Setup initial directional light which we will use to generate the shadow map
		s_Data->GlobalLightingData.sunLight = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);

		CreateResources();

		s_Data->MaterialRegistry.SetupDescriptorSets(s_Data->DescriptorSetLayouts.textures);
	}

	void Renderer::Shutdown() 
	{
		KBR_CORE_ASSERT(s_Data, "Renderer not initialized!");

		VulkanContext::Get().WaitIdle();

		VulkanContext::DestroyImGuiDescriptorSet(s_Data->ColorOutputDescriptorSet);
		for (auto& descriptorSet : s_Data->ShadowMapDescriptorSet)
		{
			VulkanContext::DestroyImGuiDescriptorSet(descriptorSet);
		}

		for (auto& slot : s_Data->MousePickingReadback.Slots)
		{
         if (slot.Memory != nullptr && slot.MappedData)
			{
				slot.Memory.unmapMemory();
				slot.MappedData = nullptr;
			}
		}

		for (auto& [Buffer, Memory, MappedData] : s_Data->ColliderLineBuffers)
		{
			if (Memory != nullptr && MappedData)
			{
				Memory.unmapMemory();
				MappedData = nullptr;
			}
		}

		s_Data.reset();
		s_Data = nullptr;
	}

	void Renderer::RenderSceneEditor(const Ref<Scene>& scene, const Camera& camera) 
	{
		RenderScene(scene, 
					camera.GetViewMatrix(),
					camera.GetProjectionMatrix(), 
					camera.GetPosition(),
					[&camera](const glm::vec3& lightDir, const std::function<glm::vec4(float)>& getCascadeSplits) { return camera.GetLightSpaceMatrices(lightDir, getCascadeSplits); });
	}

	void Renderer::RenderSceneRuntime(const Ref<Scene>& scene, const Camera& mainCamera,
									  const glm::mat4& mainCameraTransform)
	{
		const glm::vec3 camPos = mainCameraTransform[3];
		RenderScene(scene, 
					mainCamera.GetViewMatrix(), 
					mainCamera.GetProjectionMatrix(),
					camPos, 
					[&mainCamera](const glm::vec3& lightDir, const std::function<glm::vec4(float)>& getCascadeSplits) { return mainCamera.GetLightSpaceMatrices(lightDir, getCascadeSplits); });
	}

	void Renderer::RenderScene(const Ref<Scene>& scene, const glm::mat4& view, const glm::mat4& projection,
		const glm::vec3& camPos, const std::function<std::pair<std::vector<glm::mat4>, glm::vec4>(const glm::vec3&, const std::function<glm::vec4(float)>&)>& calculateLightSpaceMatricesFunc)
	{
		KBR_CORE_ASSERT(!s_Data->PendingRender.IsValid, "Scene has already been queued for rendering!");

		s_Data->PendingRender.Scene = scene;
		s_Data->PendingRender.View = view;
		s_Data->PendingRender.Projection = projection;
		s_Data->PendingRender.CameraPosition = camPos;
		s_Data->PendingRender.CalculateLightSpaceMatricesFunc = calculateLightSpaceMatricesFunc;
		s_Data->PendingRender.IsValid = scene != nullptr;
	}

	void Renderer::RecordQueuedSceneRender(const vk::raii::CommandBuffer& cmd)
	{
		KBR_CORE_ASSERT(s_Data->PendingRender.IsValid, "No pending scene render to record!");

		if (!s_Data->PendingRender.IsValid || !s_Data->PendingRender.Scene)
			return;

		auto& context = VulkanContext::Get();
		const uint32_t frameIndex = context.GetCurrentFrameIndex();

		if (IsUsingAccelerationStructures())
		{
			s_Data->RayTracingCache.BuildAccelerationStructures(s_Data->PendingRender.Scene, cmd, frameIndex);

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
		}

		auto [renderObjects, uniqueMaterials] = GetRenderObjectsAndUniqueMaterialsFromScene(*s_Data->PendingRender.Scene.get());
		const auto colliderLineVertices = s_Data->DisplayPhysicsColliders
			? GetColliderLineVerticesFromScene(*s_Data->PendingRender.Scene.get())
			: std::vector<LineVertex>{};
		s_Data->MaterialRegistry.UpdateDescriptorSetsForMaterials(uniqueMaterials);

		ResetQueryPool(cmd, frameIndex);

		WriteGPUTimestamp(cmd, frameIndex, static_cast<uint32_t>(GPUTimestampQuery::FrameBegin));

        const uint32_t currentImage = frameIndex;

		const DirectionalLight sunlight = s_Data->PendingRender.Scene->GetSunlight();
		s_Data->GlobalLightingData.sunLight = glm::vec4(sunlight.Direction, sunlight.Intensity);

		const auto getCascadeSplits = [](const float farPlane) -> glm::vec4
		{
			return { farPlane / 25.0f, farPlane / 10.0f, farPlane / 2.0f, farPlane };
		};

		const auto [lightSpaceMatrices, cascadeSplits] = s_Data->PendingRender.CalculateLightSpaceMatricesFunc(s_Data->GlobalLightingData.sunLight, getCascadeSplits);

		const std::vector<GPULight> gpuLights = GetLightsFromScene(*s_Data->PendingRender.Scene.get());
		const uint32_t lightCount = static_cast<uint32_t>(gpuLights.size());

		UpdateLights(currentImage, gpuLights);
		UpdateSceneUniformBuffers(currentImage,
            s_Data->PendingRender.View,
			s_Data->PendingRender.Projection,
			s_Data->PendingRender.CameraPosition,
			lightSpaceMatrices,
			cascadeSplits,
			lightCount);

		const Ref<Scene>& scene = s_Data->PendingRender.Scene;

		// Depth Pre-pass
		{
			WriteGPUTimestamp(cmd, frameIndex, static_cast<uint32_t>(GPUTimestampQuery::DepthPrePassBegin));

			vk::ImageMemoryBarrier2 barrier = {
				.srcStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
				.srcAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
				.dstStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
				.dstAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
				.oldLayout = vk::ImageLayout::eUndefined,
				.newLayout = vk::ImageLayout::eDepthAttachmentOptimal,
				.srcQueueFamilyIndex = vk::QueueFamilyIgnored,
				.dstQueueFamilyIndex = vk::QueueFamilyIgnored,
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

			vk::RenderingAttachmentInfo depthAttachmentInfo{
				.imageView = s_Data->DepthImage.ImageView,
				.imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
				.loadOp = vk::AttachmentLoadOp::eClear,
				.storeOp = vk::AttachmentStoreOp::eStore,
				.clearValue = vk::ClearDepthStencilValue{.depth = 1.0f, .stencil = 0 }
			};

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

			const vk::RenderingInfo depthPrePassRenderingInfo{
				.renderArea = renderArea,
				.layerCount = 1,
				.colorAttachmentCount = 0,
				.pColorAttachments = nullptr,
				.pDepthAttachment = &depthAttachmentInfo
			};

			cmd.beginRendering(depthPrePassRenderingInfo);
			cmd.setViewport(0, viewport);
			cmd.setScissor(0, renderArea);

			cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *s_Data->DepthPrePassPipeline->GetVulkanPipeline());

			const auto meshView = scene->m_Registry.view<TransformComponent, StaticMeshComponent>();
			int i = 0;
			for (const auto entity : meshView)
			{
				auto& transform = meshView.get<TransformComponent>(entity);
				auto& staticMesh = meshView.get<StaticMeshComponent>(entity);
				if (!staticMesh.Visible || !staticMesh.StaticMesh /* || !staticMesh.MeshMaterial*/)
					continue;

				// TODO: Remove this once we have a proper material system
				Ref<Material> material = staticMesh.MeshMaterial;
				if (material == nullptr)
					material = s_Data->MaterialRegistry.Get("DebugPink");

				UpdatePerObjectUniformBuffer(currentImage, static_cast<uint32_t>(i), transform.GetTransform(), *material, static_cast<uint32_t>(entity));
				uint32_t dynamicOffset = static_cast<uint32_t>(i * s_Data->DynamicAlignment);

				cmd.bindDescriptorSets(
					vk::PipelineBindPoint::eGraphics,
					*s_Data->PBRPipelineLayout,
					0,
					{ s_Data->DescriptorSets[currentImage].scene, material->DescriptorSets[currentImage] },
					{ dynamicOffset });

				staticMesh.StaticMesh->Draw(cmd);

				++i;
			}

			cmd.endRendering();

			WriteGPUTimestamp(cmd, frameIndex, static_cast<uint32_t>(GPUTimestampQuery::DepthPrePassEnd));
		}

		// Render shadow map
	   {
			WriteGPUTimestamp(cmd, frameIndex, static_cast<uint32_t>(GPUTimestampQuery::ShadowBegin));

			vk::ImageMemoryBarrier2 barrier = {
			.srcStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
			.srcAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
			.dstStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
			.dstAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
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
				.layerCount = ShadowMap::CascadeCount
			}
			};

			const vk::DependencyInfo dependencyInfo = {
				.dependencyFlags = {},
				.imageMemoryBarrierCount = 1,
				.pImageMemoryBarriers = &barrier
			};

			cmd.pipelineBarrier2(dependencyInfo);

			cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *s_Data->ShadowMap.Pipeline->GetVulkanPipeline());
			cmd.setDepthBias(s_Data->DepthBias.ConstantFactor, s_Data->DepthBias.Clamp, s_Data->DepthBias.SlopeFactor);

			const vk::Rect2D renderArea{
				.offset = vk::Offset2D{.x = 0, .y = 0 },
				.extent = vk::Extent2D{.width = s_Data->ShadowMap.Size, .height = s_Data->ShadowMap.Size }
			};

			for (uint32_t cascadeIndex = 0; cascadeIndex < ShadowMap::CascadeCount; ++cascadeIndex)
			{
				vk::RenderingAttachmentInfo shadowMapDepthAttachmentInfo{
				.imageView = s_Data->ShadowMap.WriteImageViews[cascadeIndex],
				.imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
				.loadOp = vk::AttachmentLoadOp::eClear,
				.storeOp = vk::AttachmentStoreOp::eStore,
				.clearValue = vk::ClearDepthStencilValue{.depth = 1.0f, .stencil = 0 }
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

				cmd.pushConstants<uint32_t>(*s_Data->ShadowMap.PipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, { cascadeIndex });

				const auto meshView = scene->m_Registry.view<TransformComponent, StaticMeshComponent>();
				uint32_t i = 0;
				for (const auto entity : meshView)
				{
					auto& transform = meshView.get<TransformComponent>(entity);
					auto& staticMesh = meshView.get<StaticMeshComponent>(entity);
					if (!staticMesh.Visible || !staticMesh.StaticMesh /* || !staticMesh.MeshMaterial*/)
						continue;

					// TODO: Remove this once we have a proper material system
					Ref<Material> material = staticMesh.MeshMaterial;
					if (material == nullptr)
						material = s_Data->MaterialRegistry.Get("DebugPink");

					UpdatePerObjectUniformBuffer(currentImage, static_cast<uint32_t>(i), transform.GetTransform(), *material, static_cast<uint32_t>(entity));
					uint32_t dynamicOffset = static_cast<uint32_t>(i * s_Data->DynamicAlignment);

					cmd.bindDescriptorSets(
						vk::PipelineBindPoint::eGraphics,
						*s_Data->ShadowMap.PipelineLayout,
						0,
						{ s_Data->DescriptorSets[currentImage].scene },
						{ dynamicOffset });

					staticMesh.StaticMesh->Draw(cmd);

					++i;
				}

				cmd.endRendering();
			}

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
				.layerCount = ShadowMap::CascadeCount
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
				: vk::PipelineStageFlagBits2::eColorAttachmentOutput;
			const vk::AccessFlags2 srcAccessMask = s_Data->PickingImageLayout == vk::ImageLayout::eTransferSrcOptimal
				? vk::AccessFlagBits2::eTransferRead
				: vk::AccessFlagBits2::eColorAttachmentWrite;

			vk::ImageMemoryBarrier2 barrier = {
				.srcStageMask = srcStageMask,
				.srcAccessMask = srcAccessMask,
				.dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
				.dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
				.oldLayout = vk::ImageLayout::eUndefined,
				//.oldLayout = s_Data->PickingImageLayout == vk::ImageLayout::eUndefined ? vk::ImageLayout::eUndefined : s_Data->PickingImageLayout,
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
			.srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
			.srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
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
			.srcStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
			.srcAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
			.dstStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
			.dstAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
			.oldLayout = vk::ImageLayout::eDepthAttachmentOptimal,
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
				//const auto meshView = scene->m_Registry.view<TransformComponent, StaticMeshComponent>();
				//int i = 0;
				//for (const auto entity : meshView)
				//{
				//	auto& transform = meshView.get<TransformComponent>(entity);
				//	auto& staticMesh = meshView.get<StaticMeshComponent>(entity);
				//	if (!staticMesh.Visible || !staticMesh.StaticMesh /* || !staticMesh.MeshMaterial*/)
				//		continue;

				//	// TODO: Remove this once we have a proper material system
				//	Ref<Material> material = staticMesh.MeshMaterial;
				//	if (material == nullptr)
				//		material = s_Data->MaterialRegistry.Get("DebugPink");

				//	UpdatePerObjectUniformBuffer(currentImage, static_cast<uint32_t>(i), transform.GetTransform(), *material, static_cast<uint32_t>(entity));
				//	uint32_t dynamicOffset = static_cast<uint32_t>(i * s_Data->DynamicAlignment);

				//	cmd.bindDescriptorSets(
				//		vk::PipelineBindPoint::eGraphics,
				//		*s_Data->PBRPipelineLayout,
				//		0,
				//		{ s_Data->DescriptorSets[currentImage].scene, material->DescriptorSets[currentImage] },
				//		{ dynamicOffset });

				//	staticMesh.StaticMesh->Draw(cmd);

				//	++i;
				//}

				for (uint32_t i = 0; i < renderObjects.size(); ++i)
				{
					const auto& [Transform, Mesh, Material, EntityID] = renderObjects[i];

					Ref<Kerberos::Material> material = Material;
					if (material == nullptr)
						material = s_Data->MaterialRegistry.Get("DebugPink");

					UpdatePerObjectUniformBuffer(currentImage, i, Transform, *material, EntityID);
					uint32_t dynamicOffset = static_cast<uint32_t>(i * s_Data->DynamicAlignment);

					cmd.bindDescriptorSets(
						vk::PipelineBindPoint::eGraphics,
						*s_Data->PBRPipelineLayout,
						0,
						{ s_Data->DescriptorSets[currentImage].scene, material->DescriptorSets[currentImage] },
						{ dynamicOffset });

					Mesh->Draw(cmd);
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
					if (!meshComp.Visible || !meshComp.StaticMesh /* || !meshComp.MeshMaterial*/)
						continue;

					Ref<Material> material = meshComp.MeshMaterial;
					if (material == nullptr)
						material = s_Data->MaterialRegistry.Get("DebugPink");

					UpdatePerObjectUniformBuffer(currentImage, static_cast<uint32_t>(i), transform.GetTransform(), *material, std::numeric_limits<uint32_t>::max());
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
			//			{ s_Data->DescriptorSets[currentImage].scene, staticMesh.Meshmaterial->DescriptorSets[currentImage] },
			//			{ dynamicOffset });

			//		staticMesh.StaticMesh->Draw(cmd);

			//		++i;
			//	}
			//}

			cmd.endRendering();

			WriteGPUTimestamp(cmd, frameIndex, static_cast<uint32_t>(GPUTimestampQuery::TransparentEnd));

			KBR_CORE_TRACE("Transparent pass done!");
		}

		if (s_Data->DisplayPhysicsColliders && !colliderLineVertices.empty())
		{
			constexpr uint32_t maxVertexCount = ColliderDebugMaxVertices;
			const uint32_t vertexCount = std::min<uint32_t>(static_cast<uint32_t>(colliderLineVertices.size()), maxVertexCount);
			if (vertexCount > 0)
			{
				auto& lineBuffer = s_Data->ColliderLineBuffers[currentImage];
				std::memcpy(lineBuffer.MappedData, colliderLineVertices.data(), sizeof(LineVertex) * vertexCount);

				vk::RenderingAttachmentInfo colorAttachmentInfo{
					.imageView = s_Data->ColorImage.ImageView,
					.imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
					.loadOp = vk::AttachmentLoadOp::eLoad,
					.storeOp = vk::AttachmentStoreOp::eStore,
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
					.colorAttachmentCount = 1,
					.pColorAttachments = &colorAttachmentInfo,
					.pDepthAttachment = &depthAttachmentInfo
				};

				cmd.beginRendering(renderingInfo);
				cmd.setViewport(0, viewport);
				cmd.setScissor(0, renderArea);

				s_Data->ColliderLinesPipeline->Bind(cmd);
				cmd.bindDescriptorSets(
					vk::PipelineBindPoint::eGraphics,
					*s_Data->PBRPipelineLayout,
					0,
					{ s_Data->DescriptorSets[currentImage].scene },
					{ 0 });
				cmd.bindVertexBuffers(0, *lineBuffer.Buffer, { 0 });
				cmd.draw(vertexCount, 1, 0, 0);

				cmd.endRendering();
			}
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
		s_Data->MaterialRegistry.Add("DebugPink", CreateRef<Material>("DebugPink", glm::vec3(1.0f, 0.0f, 1.0f), 1.0f, 0.1f));
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
		PrepareStorageBuffers();

		auto& context = VulkanContext::Get();
		const auto& device = context.GetDevice();

		constexpr vk::DeviceSize colliderLineBufferSize = sizeof(LineVertex) * ColliderDebugMaxVertices;
		for (auto& [Buffer, Memory, MappedData] : s_Data->ColliderLineBuffers)
		{
			CreateBuffer(device,
				colliderLineBufferSize,
				vk::BufferUsageFlagBits::eVertexBuffer,
				vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
				Buffer,
				Memory);
			MappedData = Memory.mapMemory(0, colliderLineBufferSize);
		}

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
				.flags = {}, // vk::QueryPoolCreateFlagBits::eResetKHR,
				.queryType = vk::QueryType::eTimestamp,
				.queryCount = static_cast<uint32_t>(GPUTimestampQuery::Count)
			};

			context.Submit(VulkanContext::OperationType::Graphics, [&](const vk::raii::CommandBuffer& cmd)
			{
				for (uint32_t i = 0; i < context.GetMaxFramesInFlight(); ++i)
				{
					s_Data->GPUTimestampQueryPools.emplace_back(device, queryPoolInfo);
					context.SetObjectDebugName(s_Data->GPUTimestampQueryPools.back(), "Renderer GPU Timestamp Query Pool[" + std::to_string(i) + "]");
					// Reset query pool at the beginning so that we can immediately start using it without waiting for the first render to reset it
					cmd.resetQueryPool(s_Data->GPUTimestampQueryPools.back(), 0, static_cast<uint32_t>(GPUTimestampQuery::Count));
				}
			});
		}

		// Create samplers
		{
			const vk::SamplerCreateInfo samplerInfo{
				.magFilter = vk::Filter::eLinear,
				.minFilter = vk::Filter::eLinear,
				.mipmapMode = vk::SamplerMipmapMode::eLinear,
				.addressModeU = vk::SamplerAddressMode::eRepeat,
				.addressModeV = vk::SamplerAddressMode::eRepeat,
				.addressModeW = vk::SamplerAddressMode::eRepeat,
				.mipLodBias = 0.0f,
				.anisotropyEnable = vk::True,
				.maxAnisotropy = context.GetMaxAnisotropy(),
				.compareEnable = vk::False,
				.compareOp = vk::CompareOp::eAlways,
				.minLod = 0.0f,
				.maxLod = vk::LodClampNone,
				.borderColor = vk::BorderColor::eIntOpaqueBlack,
				.unnormalizedCoordinates = vk::False
			};
			s_Data->ColorSampler = vk::raii::Sampler{ device, samplerInfo };

			context.SetObjectDebugName(s_Data->ColorSampler, "Color Texture Sampler");

			const vk::SamplerCreateInfo shadowSamplerInfo{
				.magFilter = vk::Filter::eLinear,
				.minFilter = vk::Filter::eLinear,
				.mipmapMode = vk::SamplerMipmapMode::eLinear,
				.addressModeU = vk::SamplerAddressMode::eClampToBorder,
				.addressModeV = vk::SamplerAddressMode::eClampToBorder,
				.addressModeW = vk::SamplerAddressMode::eClampToBorder,
				.mipLodBias = 0.0f,
				.anisotropyEnable = vk::True,
				.maxAnisotropy = context.GetMaxAnisotropy(),
				.compareEnable = vk::True,
				.compareOp = vk::CompareOp::eGreaterOrEqual,//eLessOrEqual,
				.minLod = 0.0f,
				.maxLod = 1.0f,
				.borderColor = vk::BorderColor::eFloatOpaqueWhite,
				.unnormalizedCoordinates = vk::False
			};
			s_Data->ShadowMapSampler = vk::raii::Sampler{ device, shadowSamplerInfo };

			context.SetObjectDebugName(s_Data->ShadowMapSampler, "Shadow Map Sampler");
		}

		// Create the shadow map resources
		{
			// Create shadow map image
			const vk::Format shadowMapFormat = context.FindSupportedFormat(
				{ vk::Format::eD32Sfloat },
				vk::ImageTiling::eOptimal,
				vk::FormatFeatureFlagBits::eDepthStencilAttachment | vk::FormatFeatureFlagBits::eSampledImage
			);

			constexpr uint32_t shadowMapMipLevels = 1;

			vk::ImageCreateInfo imageInfo{
				//.flags = vk::ImageCreateFlagBits::e2DArrayCompatible,
				.imageType = vk::ImageType::e2D,
				.format = shadowMapFormat,
				.extent = {
					.width = s_Data->ShadowMap.Size,
					.height = s_Data->ShadowMap.Size,
					.depth = 1
				},
				.mipLevels = shadowMapMipLevels,
				.arrayLayers = ShadowMap::CascadeCount,
				.samples = vk::SampleCountFlagBits::e1,
				.tiling = vk::ImageTiling::eOptimal,
				.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled,
			};

			s_Data->ShadowMap.Image = vk::raii::Image(device, imageInfo);

			const vk::MemoryRequirements memRequirements = s_Data->ShadowMap.Image.getMemoryRequirements();
			const vk::MemoryAllocateInfo allocInfo{
				.allocationSize = memRequirements.size,
				.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal)
			};

			s_Data->ShadowMap.ImageMemory = vk::raii::DeviceMemory(device, allocInfo);
			s_Data->ShadowMap.Image.bindMemory(*s_Data->ShadowMap.ImageMemory, 0);

			context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkImage>(*s_Data->ShadowMap.Image)),
									   vk::ObjectType::eImage,
									   "Shadow Map Image");
			context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkDeviceMemory>(*s_Data->ShadowMap.ImageMemory)),
									   vk::ObjectType::eDeviceMemory,
									   "Shadow Map Image Memory");

			const vk::ImageViewCreateInfo readViewInfo{
				.image = *s_Data->ShadowMap.Image,
				.viewType = vk::ImageViewType::e2DArray,
				.format = shadowMapFormat,
				.subresourceRange = {
					.aspectMask = vk::ImageAspectFlagBits::eDepth,
					.baseMipLevel = 0,
					.levelCount = shadowMapMipLevels,
					.baseArrayLayer = 0,
					.layerCount = ShadowMap::CascadeCount
				}
			};

			s_Data->ShadowMap.ReadImageView = vk::raii::ImageView(device, readViewInfo);

			context.SetObjectDebugName(s_Data->ShadowMap.ReadImageView,"Shadow Map Image View");

			for (uint32_t i = 0; i < ShadowMap::CascadeCount; ++i)
			{
				const vk::ImageViewCreateInfo attachmentViewInfo{
					.image = *s_Data->ShadowMap.Image,
					.viewType = vk::ImageViewType::e2D,
					.format = shadowMapFormat,
					.subresourceRange = {
						.aspectMask = vk::ImageAspectFlagBits::eDepth,
						.baseMipLevel = 0,
						.levelCount = shadowMapMipLevels,
						.baseArrayLayer = i,
						.layerCount = 1
					}
				};
				s_Data->ShadowMap.WriteImageViews[i] = vk::raii::ImageView(device, attachmentViewInfo);
				context.SetObjectDebugName(s_Data->ShadowMap.WriteImageViews[i],"Shadow Map Write Image View [" + std::to_string(i) + "]");
			}

			// We do this here, because the descriptors will use the shadow map image view,
			// but has to happen before we create the pipeline
			SetupDescriptors();

			// Create shadow map image layout transition
			/*context.TransitionImageLayout(shadowMapImage,
										  vk::ImageLayout::eUndefined,
										  vk::ImageLayout::eDepthStencilAttachmentOptimal,
										  shadowMapMipLevels);*/

			constexpr vk::PushConstantRange pushConstantRange{
				.stageFlags = vk::ShaderStageFlagBits::eVertex,
				.offset = 0,
				.size = sizeof(uint32_t)
			};

			vk::PipelineLayoutCreateInfo pipelineLayoutInfo{
				.setLayoutCount = 1,
				.pSetLayouts = &*s_Data->DescriptorSetLayouts.scene,
				.pushConstantRangeCount = 1,
				.pPushConstantRanges = &pushConstantRange
			};

			s_Data->ShadowMap.PipelineLayout = vk::raii::PipelineLayout{ device, pipelineLayoutInfo };
			context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkPipelineLayout>(*s_Data->ShadowMap.PipelineLayout)),
									   vk::ObjectType::ePipelineLayout,
									   "Shadow Map Pipeline Layout");

			// Create shader for shadow mapping
			//Ref<Shader> shadowMapShader = CreateRef<Shader>("shadowmap", "ShadowMap");
			Ref<Shader> shadowMapShader = CreateRef<Shader>("shadowmap_csm", "ShadowMapCSM");

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

			Ref<Shader> depthPrepassShader = CreateRef<Shader>("depthprepass", "Depth Pre-Pass");

			GraphicsPipelineSpecification depthPrepassPipelineSpec{};
			depthPrepassPipelineSpec.Name = "Depth Pre-Pass Pipeline";
			depthPrepassPipelineSpec.Shader = depthPrepassShader;
			depthPrepassPipelineSpec.PipelineLayout = *s_Data->PBRPipelineLayout;
			depthPrepassPipelineSpec.BindingDescription = bindingDesc;
			depthPrepassPipelineSpec.InputAttributeDescriptions = { attributeDescs.begin(), attributeDescs.end() };
			depthPrepassPipelineSpec.SampleCount = vk::SampleCountFlagBits::e1;
			depthPrepassPipelineSpec.CullMode = CullMode::Back;
			depthPrepassPipelineSpec.EnableDepthClamp = false;
			depthPrepassPipelineSpec.EnableDepthBias = false;
			depthPrepassPipelineSpec.EnableDepthTest = true;
			depthPrepassPipelineSpec.EnableDepthWrite = true;
			depthPrepassPipelineSpec.DepthTestFunc = DepthTestFunc::LessOrEqual;
			depthPrepassPipelineSpec.BlendModes = {};
			depthPrepassPipelineSpec.ColorAttachmentFormats = {};
			depthPrepassPipelineSpec.DepthAttachmentFormat = depthFormat;
			depthPrepassPipelineSpec.DynamicStates = dynamicStates;

			s_Data->DepthPrePassPipeline = CreateRef<GraphicsPipeline>(depthPrepassPipelineSpec);

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
			opaquePipelineSpec.DepthTestFunc = DepthTestFunc::Equal;
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

			Ref<Shader> colliderLinesShader = CreateRef<Shader>("collider_lines", "Collider Lines");
			const auto lineBindingDesc = LineVertex::GetBindingDescription();
			const auto lineAttributeDescs = LineVertex::GetAttributeDescriptions();

			GraphicsPipelineSpecification colliderPipelineSpec{};
			colliderPipelineSpec.Name = "Collider Lines Pipeline";
			colliderPipelineSpec.Shader = colliderLinesShader;
			colliderPipelineSpec.PipelineLayout = *s_Data->PBRPipelineLayout;
			colliderPipelineSpec.BindingDescription = lineBindingDesc;
			colliderPipelineSpec.InputAttributeDescriptions = { lineAttributeDescs.begin(), lineAttributeDescs.end() };
			colliderPipelineSpec.SampleCount = vk::SampleCountFlagBits::e1;
			colliderPipelineSpec.CullMode = CullMode::None;
			colliderPipelineSpec.EnableDepthClamp = false;
			colliderPipelineSpec.EnableDepthBias = false;
			colliderPipelineSpec.EnableDepthTest = true;
			colliderPipelineSpec.EnableDepthWrite = false;
			colliderPipelineSpec.DepthTestFunc = DepthTestFunc::LessOrEqual;
			colliderPipelineSpec.Topology = PrimitiveTopology::LineList;
			colliderPipelineSpec.BlendModes = { BlendMode::None };
			colliderPipelineSpec.ColorAttachmentFormats = { colorFormat };
			colliderPipelineSpec.DepthAttachmentFormat = depthFormat;
			colliderPipelineSpec.DynamicStates = dynamicStates;
			s_Data->ColliderLinesPipeline = CreateRef<GraphicsPipeline>(colliderPipelineSpec);

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

			for (uint32_t i = 0; i < ShadowMap::CascadeCount; ++i)
			{
				s_Data->ShadowMapDescriptorSet[i] = VulkanContext::GenerateImGuiDescriptorSet(s_Data->ColorSampler, s_Data->ShadowMap.WriteImageViews[i]);
				context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkDescriptorSet>(s_Data->ShadowMapDescriptorSet[i])),
										   vk::ObjectType::eDescriptorSet,
										   "Shadow Map Descriptor Set for ImGui");
			}
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
		VulkanContext::Get().WaitIdle();

		if (const auto& shadowMapPipeline = s_Data->ShadowMap.Pipeline)
			shadowMapPipeline->Recompile();
		if (const auto& depthPrePassPipeline = s_Data->DepthPrePassPipeline)
			depthPrePassPipeline->Recompile();
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
		if (const auto& colliderLinesPipeline = s_Data->ColliderLinesPipeline)
			colliderLinesPipeline->Recompile();
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

	bool& Renderer::GetDisplayPhysicsColliders()
	{
		KBR_CORE_ASSERT(s_Data, "Renderer not initialized!");

		return s_Data->DisplayPhysicsColliders;
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

		return s_Data->GlobalLightingData.gamma;
	}

	float& Renderer::GetExposure()
	{
		KBR_CORE_ASSERT(s_Data, "Renderer not initialized!");

		return s_Data->GlobalLightingData.exposure;
	}

	uint32_t Renderer::GetShadowMapCascadeCount() 
	{
		return ShadowMap::CascadeCount;
	}

	uint32_t Renderer::GetShadowMapResolution() 
	{
		return s_Data->ShadowMap.Size;
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

	uint64_t Renderer::GetShadowMapDepthImageID(uint32_t index) 
	{
		KBR_CORE_ASSERT(index < s_Data->ShadowMapDescriptorSet.size(), "Out of bounds index for shadow map depth image!");
		KBR_CORE_ASSERT(s_Data->ShadowMapDescriptorSet[index], std::format("ImGui descriptor set is not created for shadow map depth image {}!", index));

		return reinterpret_cast<uint64_t>(static_cast<VkDescriptorSet>(s_Data->ShadowMapDescriptorSet[index]));
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
		s_Data->LatestGPUTimings.DepthPrePassMilliseconds = toMilliseconds(GPUTimestampQuery::DepthPrePassBegin, GPUTimestampQuery::DepthPrePassEnd);
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

	void Renderer::UpdateLights(const uint32_t currentImage, const std::vector<GPULight>& sceneLights) 
	{
		KBR_CORE_ASSERT(s_Data, "Renderer not initialized!");

		std::memcpy(s_Data->UniformBuffers[currentImage].globalLighting->GetMappedData(), &s_Data->GlobalLightingData, sizeof(GlobalLighting));

		std::memcpy(s_Data->StorageBuffers[currentImage].Lights->GetMappedData(), sceneLights.data(), sceneLights.size() * sizeof(GPULight));
	}

	void Renderer::UpdateSceneUniformBuffers(const uint32_t currentImage, const Camera* mainCamera, const std::vector<glm::mat4>& lightSpaceMatrices,
											 const glm::vec4& cascadeSplits, const uint32_t lightCount)
	{
		KBR_CORE_ASSERT(s_Data, "Renderer not initialized!");

		const glm::mat4& projection = mainCamera->GetProjectionMatrix();
		const glm::mat4& view = mainCamera->GetViewMatrix();
		const glm::vec3 camPos = mainCamera->GetPosition();

		UpdateSceneUniformBuffers(currentImage, view, projection, camPos, lightSpaceMatrices, cascadeSplits, lightCount);
	}

	void Renderer::UpdateSceneUniformBuffers(const uint32_t currentImage, const glm::mat4& view, const glm::mat4& projection,
												const glm::vec3& camPos, const std::vector<glm::mat4>& lightSpaceMatrices,
												const glm::vec4& cascadeSplits, const uint32_t lightCount)
	{
		KBR_CORE_ASSERT(s_Data, "Renderer not initialized!");

		s_Data->SceneUniformData.projection = projection;
		s_Data->SceneUniformData.view = view;
		s_Data->SceneUniformData.camPos = camPos;
		s_Data->SceneUniformData.cascadeSplits = cascadeSplits;
		s_Data->SceneUniformData.lightCount = lightCount;

		for (size_t i = 0; i < s_Data->SceneUniformData.lightSpaceMatrices->length(); ++i)
		{
			s_Data->SceneUniformData.lightSpaceMatrices[i] = lightSpaceMatrices[i];
		}

		if (s_Data->SceneUniformData.lightSpaceMatrices->length() != lightSpaceMatrices.size())
		{
			KBR_CORE_ERROR("Number of light space matrices exceeds the maximum supported count of {}", s_Data->SceneUniformData.lightSpaceMatrices->length());
		}

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

    std::vector<GPULight> Renderer::GetLightsFromScene(const Scene& scene) 
	{
		std::vector<GPULight> sceneLights;

		const auto pointLightView = scene.m_Registry.view<PointLightComponent>();
		for (const auto entity : pointLightView) {
			auto& pl = pointLightView.get<PointLightComponent>(entity);
			if (!pl.IsEnabled) continue;

			GPULight gpuLight{};
			gpuLight.Type = static_cast<uint32_t>(LightType::Point);
			gpuLight.Position = pl.Light.Position;
			gpuLight.Color = pl.Light.Color;
			gpuLight.Intensity = pl.Light.Intensity;
			gpuLight.Range = 50.0f; // Calculate based on attenuation parameters
			sceneLights.push_back(gpuLight);
		}

		const auto spotLightView = scene.m_Registry.view<SpotLightComponent>();
		for (const auto entity : spotLightView) {
			auto& sl = spotLightView.get<SpotLightComponent>(entity);
			if (!sl.IsEnabled) continue;

			GPULight gpuLight{};
			gpuLight.Type = static_cast<uint32_t>(LightType::Spot);
			gpuLight.Position = sl.Light.Position;
			gpuLight.Direction = sl.Light.Direction;
			gpuLight.Color = sl.Light.Color;
			gpuLight.Intensity = sl.Light.Intensity;
			gpuLight.Range = 50.0f; // Calculate based on attenuation parameters
			gpuLight.InnerConeCos = glm::cos(sl.Light.CutOffAngleRadians);
			gpuLight.OuterConeCos = glm::cos(sl.Light.OuterCutOffAngleRadians);
			sceneLights.push_back(gpuLight);
		}

		// TODO: Add area light when they are implemented

		return sceneLights;
    }

    std::pair<std::vector<RenderObject>, std::set<Ref<Material>>> Renderer::GetRenderObjectsAndUniqueMaterialsFromScene(
	    const Scene& scene) 
	{
		static uint32_t renderObjectCountFromLastFrame = 0;

		std::vector<RenderObject> renderObjects;
		std::set<Ref<Material>> uniqueMaterials;
		renderObjects.reserve(renderObjectCountFromLastFrame);

		const auto meshView = scene.m_Registry.view<TransformComponent, StaticMeshComponent>();
		for (const auto entity : meshView)
		{
			auto& transform = meshView.get<TransformComponent>(entity);
			auto& staticMesh = meshView.get<StaticMeshComponent>(entity);
			if (!staticMesh.Visible || !staticMesh.StaticMesh /* || !staticMesh.MeshMaterial*/)
				continue;

			RenderObject renderObject{};
			renderObject.Transform = transform.GetTransform();
			renderObject.Mesh = staticMesh.StaticMesh;
			renderObject.Material = staticMesh.MeshMaterial;
			renderObject.EntityID = static_cast<uint32_t>(entity);
			renderObjects.push_back(renderObject);
			if (renderObject.Material)
				uniqueMaterials.insert(renderObject.Material);
		}

		renderObjectCountFromLastFrame = renderObjects.size();

		return { renderObjects, uniqueMaterials };
    }

	std::vector<LineVertex> Renderer::GetColliderLineVerticesFromScene(const Scene& scene)
	{
		std::vector<LineVertex> vertices;
		vertices.reserve(4096);

		const auto boxView = scene.m_Registry.view<TransformComponent, BoxCollider3DComponent>();
		for (const auto entity : boxView)
		{
			const auto& transform = boxView.get<TransformComponent>(entity);
			const auto& collider = boxView.get<BoxCollider3DComponent>(entity);
			const glm::mat4 base = GetWorldTransformWithoutScale(transform) * glm::translate(glm::mat4(1.0f), collider.Offset);
			AddBoxLines(vertices, base, collider.Size);
		}

		const auto sphereView = scene.m_Registry.view<TransformComponent, SphereCollider3DComponent>();
		for (const auto entity : sphereView)
		{
			const auto& transform = sphereView.get<TransformComponent>(entity);
			const auto& collider = sphereView.get<SphereCollider3DComponent>(entity);
			const glm::mat4 base = GetWorldTransformWithoutScale(transform) * glm::translate(glm::mat4(1.0f), collider.Offset);
			AddCircleLines(vertices, base, 0, 1, collider.Radius);
			AddCircleLines(vertices, base, 1, 2, collider.Radius);
			AddCircleLines(vertices, base, 2, 0, collider.Radius);
		}

		const auto capsuleView = scene.m_Registry.view<TransformComponent, CapsuleCollider3DComponent>();
		for (const auto entity : capsuleView)
		{
			const auto& transform = capsuleView.get<TransformComponent>(entity);
			const auto& collider = capsuleView.get<CapsuleCollider3DComponent>(entity);
			const glm::mat4 base = GetWorldTransformWithoutScale(transform) * glm::translate(glm::mat4(1.0f), collider.Offset);
			AddCapsuleLines(vertices, base, collider.Radius, collider.Height * 0.5f);
		}

		const auto meshColliderView = scene.m_Registry.view<TransformComponent, MeshCollider3DComponent>();
		for (const auto entity : meshColliderView)
		{
			const auto& transform = meshColliderView.get<TransformComponent>(entity);
			const auto& collider = meshColliderView.get<MeshCollider3DComponent>(entity);

			Ref<Mesh> mesh = collider.Mesh;
			if (!mesh && scene.m_Registry.all_of<StaticMeshComponent>(entity))
			{
				mesh = scene.m_Registry.get<StaticMeshComponent>(entity).StaticMesh;
			}
			if (!mesh || mesh->GetVertices().empty())
			{
				continue;
			}

			glm::vec3 minPoint(std::numeric_limits<float>::max());
			glm::vec3 maxPoint(-std::numeric_limits<float>::max());
			for (const auto& vertex : mesh->GetVertices())
			{
				minPoint = glm::min(minPoint, vertex.Position);
				maxPoint = glm::max(maxPoint, vertex.Position);
			}
			const glm::vec3 halfExtents = (maxPoint - minPoint) * 0.5f;
			const glm::vec3 center = (maxPoint + minPoint) * 0.5f;

			const glm::mat4 base = GetWorldTransformWithoutScale(transform)
				* glm::translate(glm::mat4(1.0f), collider.Offset + center);
			AddBoxLines(vertices, base, halfExtents);
		}

		return vertices;
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

		const glm::vec3 lightDir = glm::normalize(glm::vec3(s_Data->GlobalLightingData.sunLight));

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

	bool Renderer::IsUsingAccelerationStructures()
	{
		return s_Data->UseRayQueryBasedShadows || s_Data->UseRayQueryBasedSoftShadows;
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

		for (auto& [scene, globalLighting, perObject, skybox] : s_Data->UniformBuffers)
		{
			scene = CreateRef<UniformBuffer>(sizeof(SceneUniformData));

			globalLighting = CreateRef<UniformBuffer>(sizeof(GlobalLighting));

			// TODO: Allocate a large enough buffer for a maximum number of objects, and allocate a bigger one if needed.
			constexpr size_t maxObjects = 1000;
			perObject = CreateRef<UniformBuffer>(s_Data->DynamicAlignment * maxObjects);

			skybox = CreateRef<UniformBuffer>(sizeof(SkyboxData));
		}
	}

	void Renderer::PrepareStorageBuffers()
	{
		for (auto& [lights] : s_Data->StorageBuffers)
		{
			constexpr uint32_t lightBufferSize = sizeof(GPULight) * MaxLights;
			lights = CreateRef<StorageBuffer>(lightBufferSize);
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
			},
			vk::DescriptorPoolSize{
				.type = vk::DescriptorType::eStorageBuffer,
				.descriptorCount = 10
			},
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
			vk::DescriptorSetLayoutBinding{ // Light storage buffer
				.binding = 8,
				.descriptorType = vk::DescriptorType::eStorageBuffer,
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
			vk::DescriptorSetLayoutBinding{ // Roughness map
				.binding = 2,
				.descriptorType = vk::DescriptorType::eCombinedImageSampler,
				.descriptorCount = 1,
				.stageFlags = vk::ShaderStageFlagBits::eFragment,
			},
			vk::DescriptorSetLayoutBinding{ // Metallic map
				.binding = 3,
				.descriptorType = vk::DescriptorType::eCombinedImageSampler,
				.descriptorCount = 1,
				.stageFlags = vk::ShaderStageFlagBits::eFragment,
			},
			vk::DescriptorSetLayoutBinding{ // Ambient occlusion map
				.binding = 4,
				.descriptorType = vk::DescriptorType::eCombinedImageSampler,
				.descriptorCount = 1,
				.stageFlags = vk::ShaderStageFlagBits::eFragment,
			},
		};

		const std::vector<vk::DescriptorBindingFlags> textureBindingFlags(
			textureBindings.size(), 
			vk::DescriptorBindingFlagBits::ePartiallyBound | vk::DescriptorBindingFlagBits::eUpdateAfterBind
		);

		const vk::DescriptorSetLayoutBindingFlagsCreateInfo textureLayoutBindingFlagsInfo{
			.bindingCount = static_cast<uint32_t>(textureBindingFlags.size()),
			.pBindingFlags = textureBindingFlags.data()
		};

		const vk::DescriptorSetLayoutCreateInfo textureLayoutInfo{
			.pNext = &textureLayoutBindingFlagsInfo,
			.flags = vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool,
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
				.buffer = *s_Data->UniformBuffers[i].globalLighting->GetBuffer(),
				.offset = 0,
				.range = sizeof(GlobalLighting)
			};

			const vk::DescriptorBufferInfo perObjectBufferInfo{
				.buffer = *s_Data->UniformBuffers[i].perObject->GetBuffer(),
				.offset = 0,
				.range = sizeof(PerObjectData)
			};

			const vk::DescriptorImageInfo shadowMapImageInfo{
				.sampler = *s_Data->ShadowMapSampler,
				.imageView = *s_Data->ShadowMap.ReadImageView,
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

			const vk::DescriptorBufferInfo lightStorageBufferInfo{
				.buffer = *s_Data->StorageBuffers[i].Lights->GetBuffer(),
				.offset = 0,
				.range = sizeof(GPULight) * MaxLights
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
					.dstSet = *s_Data->DescriptorSets[i].scene,
					.dstBinding = 8,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = vk::DescriptorType::eStorageBuffer,
					.pBufferInfo = &lightStorageBufferInfo
				},
				// The TLAS descriptor will be updated later when the TLAS is built for the first time, since it requires a valid acceleration structure handle.
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
