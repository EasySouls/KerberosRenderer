#pragma once

#include "Layer.hpp"

#include "Vulkan.hpp"

#include <glm/vec3.hpp>

#include <string>
#include <vector>

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
		void OnEvent() override;
		void OnImGuiRender() override;

	private:
		float m_Time = 0.0f;
		float m_Fps = 0.0f;

		std::vector<Material> m_Materials;

		// Vulkan resources
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

		vk::raii::PipelineLayout m_PBRPipelineLayout = nullptr;
		vk::raii::Pipeline m_PBROpaquePipeline = nullptr;
		vk::raii::Pipeline m_PBRTransparentPipeline = nullptr;
	};

}