#pragma once

#include "VulkanContext.hpp"
#include "Layer.hpp"

#include <memory>

struct GLFWwindow;

namespace kbr
{
	class Application
	{
	public:
		Application();
		~Application();

		Application(const Application& other) = delete;
		Application(Application&& other) noexcept = default;
		Application& operator=(const Application& other) = delete;
		Application& operator=(Application&& other) noexcept = default;

		void Run();

		template<typename T> 
			requires std::is_base_of_v<Layer, T>
		void PushLayer();

		void FramebufferResized(uint32_t width, uint32_t height) const;

	private:
		GLFWwindow* m_Window;
		std::unique_ptr<VulkanContext> m_VulkanContext;

		std::vector<std::unique_ptr<Layer>> m_Layers;
		float m_LastFrameTime = 0.0f;
	};

	template <typename T> 
		requires std::is_base_of_v<Layer, T>
	void Application::PushLayer() 
	{
		auto layer = std::make_unique<T>();
		layer->OnAttach();

		m_Layers.emplace_back(std::move(layer));
	}
}
