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
		bool  IsValid = false;
	};

	struct RenderObject
	{
		glm::mat4 Transform{};
		Ref<Mesh> Mesh = nullptr;
		Ref<Material> Material{};
		uint32_t EntityID = 0;
	};

	enum class AntiAliasingMode
	{
		None = 0,
		FXAA = 1,
		TAA = 2
	};

	enum class TonemappingOperator
	{
		Uncharted = 0,
		Reinhard = 1,
		ACES = 2,
		ACESFitted = 3
	};

	enum class BloomMode
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

	public:
		static constexpr uint32_t MousePickingReadbackFrameLag = 3;
		static constexpr uint32_t MaxLights = 512;

	private:
		static void UpdateLights(uint32_t currentImage, const std::vector<GPULight>& sceneLights);

		static void UpdateSceneUniformBuffers(uint32_t currentImage, 
											  const Camera* mainCamera, 
											  const std::vector<glm::mat4>& lightSpaceMatrices,
											  const glm::vec4& cascadeSplits,
											  uint32_t lightCount);

		static void UpdateSceneUniformBuffers(uint32_t currentImage, 
											  const glm::mat4& view, 
											  const glm::mat4& projection, 
											  const glm::vec3& camPos,
											  const std::vector<glm::mat4>& lightSpaceMatrices,
											  const glm::vec4& cascadeSplits,
											  uint32_t lightCount);

		static void UpdatePerObjectUniformBuffer(uint32_t currentImage, uint32_t objectIndex, const glm::mat4& model, const Material& material, uint32_t entityID);

		static std::vector<GPULight> GetLightsFromScene(const Scene& scene);
		static std::pair<std::vector<RenderObject>, std::set<Ref<Material>>> GetRenderObjectsAndUniqueMaterialsFromScene(const Scene& scene);
		static std::vector<LineVertex> GetColliderLineVerticesFromScene(const Scene& scene);

		static void ApplyPostProcessing(const vk::raii::CommandBuffer& cmd, uint32_t currentImage);
		static void ApplyBloom(const vk::raii::CommandBuffer& cmd, uint32_t currentImage);
		
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
	};
}
