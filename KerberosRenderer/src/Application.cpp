#include "Application.hpp"

#include <GLFW/glfw3.h>

#include <stdexcept>

namespace kbr
{
	static void FramebufferResizeCallback(GLFWwindow* window, const int width, const int height)
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
		m_Window = glfwCreateWindow(800, 600, "Kerberos Renderer", nullptr, nullptr);
		if (!m_Window)
		{
			glfwTerminate();
			throw std::runtime_error("Failed to create GLFW window");
		}

		glfwSetWindowUserPointer(m_Window, this);
		glfwSetFramebufferSizeCallback(m_Window, FramebufferResizeCallback);

		m_VulkanContext = std::make_unique<VulkanContext>(m_Window);
	}

	Application::~Application()
	{
		for (const auto& layer : m_Layers)
		{
			layer->OnDetach();
		}

		glfwDestroyWindow(m_Window);
		glfwTerminate();
	}

	void Application::Run() 
	{
		while (!glfwWindowShouldClose(m_Window))
		{
			glfwPollEvents();

			const float time = static_cast<float>(glfwGetTime());
			const float deltaTime = time - m_LastFrameTime;
			m_LastFrameTime = time;

			for (const auto& layer : m_Layers)
			{
				layer->OnUpdate(deltaTime);
			}

			m_VulkanContext->PrepareImGuiFrame();

			for (const auto& layer : m_Layers)
			{
				layer->OnImGuiRender();
			}

			m_VulkanContext->RenderImGui();

			m_VulkanContext->Draw();
			m_VulkanContext->Present();
		}
	}

	void Application::FramebufferResized(const uint32_t width, const uint32_t height) const 
	{
		m_VulkanContext->FramebufferResized(width, height);
	}
}
