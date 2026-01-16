#pragma once

#include "Layer.hpp"

#include "Vulkan.hpp"

namespace Game
{

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

		// Vulkan resources
		vk::raii::Image shadowMapImage = nullptr;
		vk::raii::DeviceMemory shadowMapImageMemory = nullptr;
		vk::raii::ImageView shadowMapImageView = nullptr;
		vk::raii::PipelineLayout shadowMapPipelineLayout = nullptr;
		vk::raii::Pipeline shadowMapPipeline = nullptr;
	};

}