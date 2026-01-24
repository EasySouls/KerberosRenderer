#pragma once

#include "Layer.hpp"
#include "Vulkan.hpp"

#include "Mesh.hpp"
#include "Camera.hpp"
#include "Buffer.hpp"
#include "Textures.hpp"

#include <glm/vec3.hpp>

#include <string>
#include <vector>
#include <array>
#include <memory>
#include <optional>

namespace Game
{
	struct Material
	{
		// Parameter block used as push constant block
		struct PushBlock
		{
			glm::vec3 albedo;
			float roughness;
			float metallic;
		} params{};

		std::string name;

		Material() = default;

		Material(std::string n, const glm::vec3 c, const float r, const float m)
			: name(std::move(n))
		{
			params.roughness = r;
			params.metallic = m;
			params.albedo = c;
		}
	};

	class GameLayer : public kbr::Layer
	{
	public:
		GameLayer();

		void OnAttach() override;
		void OnDetach() override;

		void OnUpdate(float deltaTime) override;
		void OnEvent(std::shared_ptr<kbr::Event> event) override;
		void OnImGuiRender() override;

	private:
		void UpdateLights(float time, uint32_t currentImage);
		void UpdateSceneUniformBuffers(uint32_t currentImage);
		void UpdatePerObjectUniformBuffer(uint32_t currentImage, const glm::vec3& objectPosition, const glm::mat4& model, const Material& material);

		void PrepareUniformBuffers();
		void SetupDescriptors();

		glm::mat4 CalculateLightSpaceMatrix() const;

		void CreateVulkanResources();
		void ResizeResources();

	private:
		float m_Time = 0.0f;
		float m_Fps = 0.0f;

		kbr::Camera m_Camera;
		// Size of the ImGui viewport.
		glm::vec2 m_ViewportSize{ 0.f };
		// Size of the output images.
		glm::vec2 m_OutputSize{ 0.f };

		std::vector<Material> m_Materials;
		std::vector<kbr::Mesh> m_Meshes;
		std::optional<kbr::Mesh> m_SkyboxMesh;
		int m_SelectedMaterialIndex = 0;

		// Vulkan resources
		uint32_t m_ShadowMapSize = 2048;
		vk::raii::Image m_ShadowMapImage = nullptr;
		vk::raii::DeviceMemory m_ShadowMapImageMemory = nullptr;
		vk::raii::ImageView m_ShadowMapImageView = nullptr;
		vk::raii::PipelineLayout m_ShadowMapPipelineLayout = nullptr;
		vk::raii::Pipeline m_ShadowMapPipeline = nullptr;

		kbr::TextureCube m_SkyboxTexture;

		vk::raii::Image m_ColorImage = nullptr;
		vk::raii::DeviceMemory m_ColorImageMemory = nullptr;
		vk::raii::ImageView m_ColorImageView = nullptr;

		vk::raii::Image m_DepthImage = nullptr;
		vk::raii::DeviceMemory m_DepthImageMemory = nullptr;
		vk::raii::ImageView m_DepthImageView = nullptr;

		vk::raii::DescriptorPool m_DescriptorPool = nullptr;
		vk::raii::DescriptorSetLayout m_PBRDescriptorSetLayout = nullptr;

		vk::raii::PipelineLayout m_PBRPipelineLayout = nullptr;
		vk::raii::Pipeline m_PBROpaquePipeline = nullptr;
		vk::raii::Pipeline m_PBRTransparentPipeline = nullptr;

		vk::raii::Sampler m_ColorSampler = nullptr;


		struct SceneUniformData
		{
			glm::mat4 projection{ 0.f };
			glm::mat4 view{ 0.f };
			glm::mat4 viewProjection{ 0.f };
			glm::mat4 lightSpaceMatrix{ 0.f };
			glm::vec3 ambientLightColor{ 0.1f, 0.1f, 0.1f };
			glm::vec3 camPos{ 0.f };
		};
		SceneUniformData m_SceneUniformData{};

		struct UniformDataParams
		{
			alignas(16) std::array<glm::vec4, 4> lights{};
			float exposure = 4.5f;
			float gamma = 2.2f;
		};
		UniformDataParams m_UniformDataParams{};

		struct PerObjectData
		{
			alignas(16) glm::vec3 position{0.f};
			alignas(16) glm::mat4 model{ 0.f };
			alignas(16) glm::mat4 worldNormal{ 0.f };
			alignas(16) Material::PushBlock material;
		};
		PerObjectData m_PerObjectUniformData{};

		struct UniformBufferObject
		{
			std::shared_ptr<kbr::UniformBuffer> scene;
			std::shared_ptr<kbr::UniformBuffer> params;
			std::shared_ptr<kbr::UniformBuffer> perObject;
		};
		// TODO: This should hold multiple UBOs for multiple frames in flight
		std::array<UniformBufferObject, 1> m_UniformBuffers;

		// TODO: This should hold multiple descriptor sets for multiple frames in flight
		std::array<vk::raii::DescriptorSet, 1> m_DescriptorSets{nullptr};

		VkDescriptorSet m_ColorOutputDescriptorSet = VK_NULL_HANDLE;

		glm::vec3 m_ObjectPosition{ 0.f };
		glm::vec3 m_ObjectRotation{ 0.f };
		glm::vec3 m_ObjectScale{ 5.f };
	};

}
