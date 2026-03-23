#pragma once

#include "Scene/Scene.hpp"
#include "Scene/Camera/EditorCamera.hpp"

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

	private:
		static void UpdateLights(uint32_t currentImage);
		static void UpdateSceneUniformBuffers(uint32_t currentImage, const Camera* mainCamera);
		static void UpdateSceneUniformBuffers(uint32_t currentImage, const glm::mat4& view, const glm::mat4& projection, const glm::vec3& camPos);
		static void UpdatePerObjectUniformBuffer(uint32_t currentImage, uint32_t objectIndex, const glm::mat4& model, const Material& material);

		static glm::mat4 CalculateLightSpaceMatrix();

		static void CreateDefaultMaterials();
		static void CreateResources();

		static void PrepareUniformBuffers();
		static void SetupDescriptors();

		static void CreateSkyboxResources();
	};
}
