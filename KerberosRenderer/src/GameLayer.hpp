#pragma once

#include "Layer.hpp"
#include "Vulkan.hpp"

#include "Mesh.hpp"
#include "Buffer.hpp"
#include "Textures.hpp"
#include "Renderer/Material.hpp"
#include "Scene/Node.hpp"
#include "Scene/Camera/Camera.hpp"
#include "Renderer/MaterialRegistry.hpp"

#include <string>
#include <vector>
#include <array>
#include <memory>
#include <optional>
#include <unordered_map>

namespace Game
{
	class GameLayer : public kbr::Layer
	{
	public:
		GameLayer();
		~GameLayer() override;

		void OnAttach() override;
		void OnDetach() override;

		void OnUpdate(float deltaTime) override;
		void OnEvent(std::shared_ptr<kbr::Event> event) override;
		void OnImGuiRender() override;

	private:
		void UpdateLights(float time, uint32_t currentImage);
		void UpdateSceneUniformBuffers(uint32_t currentImage);
		void UpdatePerObjectUniformBuffer(uint32_t currentImage, uint32_t objectIndex, const glm::mat4& model, const kbr::Material& material);

		void PrepareUniformBuffers();
		void SetupDescriptors();

		glm::mat4 CalculateLightSpaceMatrix();

		void CreateVulkanResources();
		void ResizeResources();

		void OnKeyPressed(const std::shared_ptr<kbr::Event>& event) const;

	private:
		float m_Time = 0.0f;
		float m_Fps = 0.0f;

		bool m_DisplaySkybox = true;
		bool m_DisplayDebugNormals = false;
		bool m_EnablePCF = true;

		glm::vec3 m_LightPosForShadowMapCalculation{ 0.0f };

		std::unique_ptr<kbr::Camera> m_Camera = nullptr;
		// Size of the ImGui viewport.
		glm::vec2 m_ViewportSize{ 0.f };
		// Size of the output images.
		glm::vec2 m_OutputSize{ 0.f };

		std::unordered_map<std::string, std::shared_ptr<kbr::Mesh>> m_Meshes;
		std::optional<kbr::Mesh> m_SkyboxMesh;
		std::vector<std::shared_ptr<kbr::Texture2D>> m_Textures;
		int m_SelectedMaterialIndex = 0;

		std::vector<kbr::Node*> m_SceneNodes;

		kbr::MaterialRegistry m_MaterialRegistry;

		// Vulkan resources
		uint32_t m_ShadowMapSize = 2048;
		vk::raii::Image m_ShadowMapImage = nullptr;
		vk::raii::DeviceMemory m_ShadowMapImageMemory = nullptr;
		vk::raii::ImageView m_ShadowMapImageView = nullptr;
		vk::raii::PipelineLayout m_ShadowMapPipelineLayout = nullptr;
		vk::raii::Pipeline m_ShadowMapPipeline = nullptr;

		kbr::TextureCube m_SkyboxTexture;
		// Generated at runtime
		kbr::Texture2D m_LutBrdfTexture;
		kbr::TextureCube m_IrradianceCubeTexture;
		kbr::TextureCube m_PrefilteredCubeTexture;

		vk::raii::Image m_ColorImage = nullptr;
		vk::raii::DeviceMemory m_ColorImageMemory = nullptr;
		vk::raii::ImageView m_ColorImageView = nullptr;

		vk::raii::Image m_DepthImage = nullptr;
		vk::raii::DeviceMemory m_DepthImageMemory = nullptr;
		vk::raii::ImageView m_DepthImageView = nullptr;

		vk::raii::DescriptorPool m_DescriptorPool = nullptr;
		struct DescriptorSetLayouts
		{
			vk::raii::DescriptorSetLayout scene = nullptr;
			vk::raii::DescriptorSetLayout textures = nullptr;
		} m_DescriptorSetLayouts;

		vk::raii::PipelineLayout m_PBRPipelineLayout = nullptr;
		vk::raii::Pipeline m_PBROpaquePipeline = nullptr;
		vk::raii::Pipeline m_PBROpaquePipelinePCF = nullptr;
		vk::raii::Pipeline m_PBRTransparentPipeline = nullptr;
		vk::raii::Pipeline m_SkyboxPipeline = nullptr;
		vk::raii::Pipeline m_NormalDebugPipeline = nullptr;

		vk::raii::Sampler m_ColorSampler = nullptr;
		vk::raii::Sampler m_ShadowMapSampler = nullptr;

		struct SceneUniformData
		{
			glm::mat4 projection{ 0.f };
			glm::mat4 view{ 0.f };
			glm::mat4 lightSpaceMatrix{ 0.f };
			alignas(16) glm::vec3 ambientLightColor{ 0.1f, 0.1f, 0.1f };
			alignas(16) glm::vec3 camPos{ 0.f };
		};
		SceneUniformData m_SceneUniformData{};

		struct UniformDataParams
		{
			// Direction of the lights
			alignas(16) std::array<glm::vec4, 4> lights{};
			float exposure = 4.5f;
			float gamma = 2.2f;
		};
		UniformDataParams m_UniformDataParams{};

		struct PerObjectData
		{
			alignas(16) glm::mat4 model{ 0.f };
			alignas(16) glm::mat4 worldNormal{ 0.f };
			alignas(16) kbr::Material::UniformBlock material;
		};
		PerObjectData m_PerObjectUniformData{};

		struct SkyboxData
		{
			glm::mat4 projection{ 0.f };
			glm::mat4 model{ 0.f };
		};
		SkyboxData m_SkyboxData{};

		struct UniformBufferObject
		{
			std::shared_ptr<kbr::UniformBuffer> scene;
			std::shared_ptr<kbr::UniformBuffer> params;
			std::shared_ptr<kbr::UniformBuffer> perObject;
			std::shared_ptr<kbr::UniformBuffer> skybox;
		};
		// TODO: This should hold multiple UBOs for multiple frames in flight
		std::array<UniformBufferObject, 1> m_UniformBuffers;

		struct DescriptorSets
		{
			vk::raii::DescriptorSet scene = nullptr;
			vk::raii::DescriptorSet skybox = nullptr;
		};

		// TODO: This should hold multiple descriptor sets for multiple frames in flight
		std::array<DescriptorSets, 1> m_DescriptorSets{};

		VkDescriptorSet m_ColorOutputDescriptorSet = VK_NULL_HANDLE;
		VkDescriptorSet m_ShadowMapDescriptorSet = VK_NULL_HANDLE;

		// Dynamic uniform buffer related members
		VkDeviceSize m_MinUniformBufferOffsetAlignment = 0;
		uint32_t m_DynamicAlignment = 0;
		constexpr static size_t MaxObjects = 1000;
	};

}
