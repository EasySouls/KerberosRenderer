#pragma once

#include "Layer.hpp"
#include "Vulkan.hpp"

#include "Mesh.hpp"
#include "Camera.hpp"
#include "Buffer.hpp"

#include <glm/vec3.hpp>

#include <string>
#include <vector>
#include <array>
#include <memory>

namespace Game
{
	struct Material
	{
		// Parameter block used as push constant block
		struct PushBlock
		{
			float roughness;
			float metallic;
			float r, g, b;
		} params{};

		std::string name;

		Material() = default;

		Material(std::string n, const glm::vec3 c, const float r, const float m)
			: name(std::move(n))
		{
			params.roughness = r;
			params.metallic = m;
			params.r = c.r;
			params.g = c.g;
			params.b = c.b;
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
		glm::vec2 m_ViewportSize{ 0.f };

		std::vector<Material> m_Materials;
		std::vector<kbr::Mesh> m_Meshes;

		// Vulkan resources
		uint32_t m_ShadowMapSize = 2048;
		vk::raii::Image m_ShadowMapImage = nullptr;
		vk::raii::DeviceMemory m_ShadowMapImageMemory = nullptr;
		vk::raii::ImageView m_ShadowMapImageView = nullptr;
		vk::raii::PipelineLayout m_ShadowMapPipelineLayout = nullptr;
		vk::raii::Pipeline m_ShadowMapPipeline = nullptr;

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

		glm::vec2 m_OutputSize{ 0.f };

		struct SceneUniformData
		{
			glm::mat4 projection{ 0.f };
			glm::mat4 view{ 0.f };
			glm::mat4 lightSpaceMatrix{ 0.f };
			glm::vec3 camPos{ 0.f };
		};
		SceneUniformData m_SceneUniformData{};

		struct UniformDataParams
		{
			glm::vec4 lights[4];
		};
		UniformDataParams m_UniformDataParams{};

		struct PerObjectData
		{
			glm::vec3 position{0.f};
			glm::mat4 model{ 0.f };
			glm::mat4 worldNormal{ 0.f };
			Material::PushBlock material;
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
	};

}
