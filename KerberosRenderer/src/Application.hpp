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

		void Run() const;

		template<typename T> 
			requires std::is_base_of_v<Layer, T>
		void PushLayer();

		void FramebufferResized(uint32_t width, uint32_t height) const;

	private:
		GLFWwindow* window;
		std::unique_ptr<VulkanContext> vulkanContext;

		std::vector<std::unique_ptr<Layer>> layers;
	};

	template <typename T> 
		requires std::is_base_of_v<Layer, T>
	void Application::PushLayer() 
	{
		layers.emplace_back(std::make_unique<T>());
	}
}
