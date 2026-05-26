#pragma once

#include "Scene/Scene.hpp"
#include "Scene/Camera/EditorCamera.hpp"
#include "Vulkan.hpp"

#include <optional>
#include <functional>
#include <vector>
#include <set>
#include <string_view>

namespace Kerberos
{
	struct GPULight;
	struct LineVertex;

	struct DepthBias
	{
		float ConstantFactor = 1.25f;
		float SlopeFactor = 1.75f;
		float Clamp = 0.0f;
	};

	struct RendererSettings
	{

	};

	struct GPUTimings
	{
		float FrameMilliseconds = 0.0f;
		float DepthPrePassMilliseconds = 0.0f;
		float ShadowPassMilliseconds = 0.0f;
		float OpaquePassMilliseconds = 0.0f;
		float TransparentPassMilliseconds = 0.0f;
		float TransparencyResolvePassMilliseconds = 0.0f;
		float BloomPassMilliseconds = 0.0f;
		float AmbientOcclusionPassMilliseconds = 0.0f;
		float TonemappingPassMilliseconds = 0.0f;
		float AntialiasingPassMilliseconds = 0.0f;
		bool  IsValid = false;
	};

	struct RenderStatistics
	{
		uint32_t RenderObjectCount = 0;
		uint32_t UniqueMaterialCount = 0;
		uint32_t VertexCount = 0;
		uint32_t IndexCount = 0;
		uint32_t FaceCount = 0;
		uint32_t ColliderLineVertexCount = 0;
		bool IsValid = false;
	};

	struct RenderObject
	{
		glm::mat4 Transform{};
		Ref<Mesh> Mesh = nullptr;
		Ref<Material> Material{};
		uint32_t EntityID = 0;
	};

	struct GTAOConstants
	{
		glm::mat4 projectionMatrix{ 0.0f };
		glm::mat4 invProjectionMatrix{ 0.0f };
		glm::vec2 viewportSize{ 0.0f };
		float radius = 1.0f;			// World space radius of AO
		float falloff = 1.0f;			// Distance falloff
		float sampleCount = 8.0f;		// Steps per direction (usually 4-8)
		float directionCount = 4.0f;	// Number of directions (usually 2-4)
		float temporalIndex = 1.0f;		// For jittering over time
	};

	enum class AntiAliasingMode
	{
		None = 0,
		FXAA = 1,
		SMAA = 2,
		TAA = 3
	};

	enum class TonemappingOperator : std::uint8_t
	{
		Uncharted = 0,
		Reinhard = 1,
		ACES = 2,
		ACESFitted = 3
	};

	enum class BloomMode : std::uint8_t
	{
		Legacy = 0,
		BrightPassPrefilter = 1
	};

	class Renderer
	{
	public:
		static void Init();
		static void Shutdown();

		static void RenderSceneEditor(const Ref<Scene>& scene, const Camera& camera);
		static void RenderSceneRuntime(const Ref<Scene>& scene, const Camera& mainCamera, const glm::mat4& mainCameraTransform);
		static void RenderScene(const Ref<Scene>& scene,
								const glm::mat4& view, const glm::mat4& projection,
								const glm::vec3& camPos,
								const std::function<std::pair<std::vector<glm::mat4>, glm::vec4>(const glm::vec3&, const std::function<glm::vec4(float)>&)>& calculateLightSpaceMatricesFunc);
		static void RecordQueuedSceneRender(const vk::raii::CommandBuffer& cmd);

		static void ResizeResources(uint32_t width, uint32_t height);

		static void RecompileShaders();

		static glm::vec3 GetLightPositionForShadowMapCalculation();
		static DepthBias& GetShadowMapDepthBiasSettings();
		static bool& GetIsPCFEnabledForShadowMap();
		static bool& GetDisplayDebugNormals();
		static bool& GetDisplayPhysicsColliders();
		static bool& GetDisplaySkybox();
		static bool& GetUseRayQueryBasedShadows();
		static bool& GetUseRayQueryBasedSoftShadows();
		static bool& GetUseGTAO();
		static bool& GetUseBlurForGTAO();
		static GTAOConstants& GetGTAOConstants();
		static float& GetGamma();
		static float& GetExposure();
		static uint32_t GetShadowMapCascadeCount();
		static uint32_t GetShadowMapResolution();
		static AntiAliasingMode& GetAntiAliasingMode();
		static uint32_t GetBloomMipLevels();
		static void SetBloomMipLevels(uint32_t levels);
		static float& GetBloomIntensity();
		static BloomMode& GetBloomMode();
		static float& GetBloomThreshold();
		static float& GetBloomKnee();
		static float& GetBloomMaxBrightness();
		static TonemappingOperator& GetTonemappingOperator();

		static glm::vec2 GetOutputImageSize();

		static uint64_t GetCompositedOutputImageID();
		static uint64_t GetShadowMapDepthImageID(uint32_t index);
		static void RequestMousePickingPixel(uint32_t x, uint32_t y);
		static std::optional<uint32_t> GetMousePickingEntityID();
        static bool ConsumePendingMousePickingTimelineSignal(vk::Semaphore& semaphore, uint64_t& value);
		static GPUTimings GetLatestGPUTimings();
		static RenderStatistics GetLatestRenderStatistics();

	public:
		static constexpr uint32_t MousePickingReadbackFrameLag = 3;
		static constexpr uint32_t MaxLights = 512;

	private:
		static void UpdateLights(uint32_t currentImage, const std::vector<GPULight>& sceneLights);

		static void UpdateSceneUniformBuffers(uint32_t currentImage, 
											  const Camera* mainCamera, 
											  uint32_t temporalIndex,
											  const std::vector<glm::mat4>& lightSpaceMatrices,
											  const glm::vec4& cascadeSplits,
											  uint32_t lightCount);

		static void UpdateSceneUniformBuffers(uint32_t currentImage, 
											  const glm::mat4& view, 
											  const glm::mat4& projection, 
											  const glm::vec3& camPos,
											  uint32_t temporalIndex,
											  const std::vector<glm::mat4>& lightSpaceMatrices,
											  const glm::vec4& cascadeSplits,
											  uint32_t lightCount);

		static void UpdatePerObjectUniformBuffer(uint32_t currentImage, uint32_t objectIndex, const glm::mat4& model, const Material& material, uint32_t entityID);

		static std::vector<GPULight> GetLightsFromScene(const Scene& scene);
		static std::pair<std::vector<RenderObject>, std::set<Ref<Material>>> GetRenderObjectsAndUniqueMaterialsFromScene(const Scene& scene);
		static std::vector<LineVertex> GetColliderLineVerticesFromScene(const Scene& scene);

		static void ApplyTonemapping(const vk::raii::CommandBuffer& cmd, uint32_t frameIndex);
		
		static void ApplyAntiAliasing(const vk::raii::CommandBuffer& cmd, uint32_t frameIndex);
		static void ApplyFXAA(const vk::raii::CommandBuffer& cmd, uint32_t frameIndex, const vk::Rect2D& renderArea, const vk::Viewport& viewport);
		static void ApplyNoOpPostProcessing(const vk::raii::CommandBuffer& cmd, uint32_t frameIndex, const vk::Rect2D& renderArea, const vk::Viewport& viewport);
		static void ApplySMAA(const vk::raii::CommandBuffer& cmd, uint32_t frameIndex, const vk::Rect2D& renderArea, const vk::Viewport& viewport);
		
		static void ApplyBloom(const vk::raii::CommandBuffer& cmd, uint32_t frameIndex);
		
		static void WriteGPUTimestamp(const vk::raii::CommandBuffer& cmd, uint32_t frameIndex, uint32_t index);
		static void ResolveGPUTimings(uint32_t frameIndex);
		static void ResetQueryPool(const vk::raii::CommandBuffer& cmd, uint32_t frameIndex);

		static glm::mat4 CalculateLightSpaceMatrix();

		static void HandleMousePickingReadback(const vk::raii::CommandBuffer& cmd);

		static bool IsUsingAccelerationStructures();
		static void BeginRenderPassDebugLabel(const vk::raii::CommandBuffer& cmd, std::string_view labelName);
		static void EndRenderPassDebugLabel(const vk::raii::CommandBuffer& cmd);

		static void CreateDefaultMaterials();
		static void CreateResources();

		static void PrepareUniformBuffers();
		static void PrepareStorageBuffers();
		static void SetupDescriptors();

		static void CreateSkyboxResources();
		static void CreateTransparencyResources(uint32_t width, uint32_t height);
		static void SetupTransparencyDescriptors();
		static void CreateGTAOImage(uint32_t width, uint32_t height);
		static void SetupGTAODescriptors();
		static void CreateFXAAImage(uint32_t width, uint32_t height);
		static void SetupFXAADescriptors();

		static void CreateBloomImage(uint32_t width, uint32_t height);
		static void SetupBloomDescriptors();
		static void CreateBloomResources(uint32_t width, uint32_t height);

		static void CreateTonemappedImage(uint32_t width, uint32_t height);
		static void SetupTonemappingResolveDescriptors();

		static void CreateSMAATextures();
		static void CreateSMAADescriptorSetAndPipelineLayouts();
		static void CreateSMAAImages(uint32_t width, uint32_t height);
		static void SetupSMAADescriptors();
	};
}
