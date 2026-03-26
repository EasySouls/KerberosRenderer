#pragma once

#include "Scene/Scene.hpp"
#include "Scene/Camera/EditorCamera.hpp"
#include "Vulkan.hpp"

#include <optional>

namespace Kerberos
{
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
		float ShadowPassMilliseconds = 0.0f;
		float OpaquePassMilliseconds = 0.0f;
		float TransparentPassMilliseconds = 0.0f;
		bool  IsValid = false;
	};

	class Renderer
	{
	public:
		static void Init();
		static void Shutdown();

		static void RenderSceneEditor(const Ref<Scene>& scene, const Camera& camera);
		static void RenderScene(const Ref<Scene>& scene, const glm::mat4& view, const glm::mat4& projection, const glm::vec3& camPos);
		static void RenderSceneRuntime(const Ref<Scene>& scene, const Camera& mainCamera, const glm::mat4& mainCameraTransform);

		static void ResizeResources(uint32_t width, uint32_t height);

		static glm::vec3 GetLightPositionForShadowMapCalculation();
		static DepthBias& GetShadowMapDepthBiasSettings();
		static bool& GetIsPCFEnabledForShadowMap();
		static bool& GetDisplayDebugNormals();
		static bool& GetDisplaySkybox();
		static float& GetGamma();
		static float& GetExposure();

		static glm::vec2 GetOutputImageSize();

		static uint64_t GetCompositedOutputImageID();
		static uint64_t GetShadowMapDepthImageID();
		static void RequestMousePickingPixel(uint32_t x, uint32_t y);
		static std::optional<uint32_t> GetMousePickingEntityID();
		static GPUTimings GetLatestGPUTimings();

	public:
		static constexpr uint32_t MousePickingReadbackFrameLag = 3;

	private:
		static void UpdateLights(uint32_t currentImage);
		static void UpdateSceneUniformBuffers(uint32_t currentImage, const Camera* mainCamera);
		static void UpdateSceneUniformBuffers(uint32_t currentImage, const glm::mat4& view, const glm::mat4& projection, const glm::vec3& camPos);
        static void UpdatePerObjectUniformBuffer(uint32_t currentImage, uint32_t objectIndex, const glm::mat4& model, const Material& material, uint32_t entityID);
		static void WriteGPUTimestamp(const vk::raii::CommandBuffer& cmd, uint32_t index);
		static void ResolveGPUTimings();

		static glm::mat4 CalculateLightSpaceMatrix();

		static void HandleMousePickingReadback(const vk::raii::CommandBuffer& cmd);

		static void CreateDefaultMaterials();
		static void CreateResources();

		static void PrepareUniformBuffers();
		static void SetupDescriptors();

		static void CreateSkyboxResources();
	};
}
