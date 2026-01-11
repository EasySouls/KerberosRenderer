#include "Application.hpp"

#include <GLFW/glfw3.h>

#include <stdexcept>

namespace kbr
{
	static void FramebufferResizeCallback(GLFWwindow* window, int width, int height)
	{
		const auto app = static_cast<Application*>(glfwGetWindowUserPointer(window));
		app->FramebufferResized(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
	}

	Application::Application()
	{
		// Initialize GLFW
		if (!glfwInit())
		{
			throw std::runtime_error("Failed to initialize GLFW");
		}
		// Create a GLFW window
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		window = glfwCreateWindow(800, 600, "Kerberos Renderer", nullptr, nullptr);
		if (!window)
		{
			glfwTerminate();
			throw std::runtime_error("Failed to create GLFW window");
		}

		glfwSetWindowUserPointer(window, this);
		glfwSetFramebufferSizeCallback(window, FramebufferResizeCallback);

		vulkanContext = std::make_unique<VulkanContext>(window);
	}

	Application::~Application()
	{
		glfwDestroyWindow(window);
		glfwTerminate();
	}

	void Application::Run() const 
	{
		while (!glfwWindowShouldClose(window))
		{
			glfwPollEvents();

			for (const auto& layer : layers)
			{
				layer->OnUpdate(0.0f); // TODO: Replace 0.0f with actual delta time
			}

			vulkanContext->PrepareImGuiFrame();

			for (const auto& layer : layers)
			{
				layer->OnImGuiRender();
			}

			vulkanContext->RenderImGui();

			vulkanContext->Draw();
			vulkanContext->Present();
		}
	}

	void Application::FramebufferResized(uint32_t width, uint32_t height) 
	{
		vulkanContext->FramebufferResized(width, height);
	}
}
