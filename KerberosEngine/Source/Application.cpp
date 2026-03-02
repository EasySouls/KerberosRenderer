#include "kbrpch.hpp"
#include "Application.hpp"

#include "events/KeyPressedEvent.hpp"
#include "events/KeyReleasedEvent.hpp"
#include "events/KeyTypedEvent.hpp"
#include "events/MouseButtonPressedEvent.hpp"
#include "events/MouseButtonReleasedEvent.hpp"
#include "events/MouseMovedEvent.hpp"
#include "events/MouseScrolledEvent.hpp"
#include "events/WindowDropEvent.hpp"
#include "events/WindowResizedEvent.hpp"

#include "Logging/Log.hpp"

#include <GLFW/glfw3.h>

#include <stdexcept>
#include <iostream>
#include <filesystem>


namespace Kerberos
{
	Application* Application::s_Instance = nullptr;

	Application::Application(const ApplicationSpecification& spec)
	{
		KBR_CORE_ASSERT(!s_Instance, "Application already exists!");
		s_Instance = this;

		m_Specification = spec;

		/// Set the working directory
		if (!spec.WorkingDirectory.empty())
		{
			std::filesystem::current_path(spec.WorkingDirectory);
			KBR_CORE_INFO("Working directory set to: {0}", std::filesystem::current_path().string());
		}
		else
		{
			m_Specification.WorkingDirectory = std::filesystem::current_path();
			KBR_CORE_WARN("No working directory specified, using current path: {0}", std::filesystem::current_path().string());
		}

		m_AudioManager = CreateOwner<AudioManager>(AudioManager::Create());
		m_AudioManager->Init();

		// Initialize GLFW
		if (!glfwInit())
		{
			throw std::runtime_error("Failed to initialize GLFW");
		}
		// Create a GLFW window
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		m_Window = glfwCreateWindow(1200, 800, "Kerberos Renderer", nullptr, nullptr);
		if (!m_Window)
		{
			glfwTerminate();
			throw std::runtime_error("Failed to create GLFW window");
		}

		glfwSetWindowUserPointer(m_Window, this);

		glfwSetFramebufferSizeCallback(m_Window, [](GLFWwindow* window, const int width, const int height)
		{
			const auto app = static_cast<Application*>(glfwGetWindowUserPointer(window));

			KBR_CORE_INFO("Framebuffer resized: ({}, {})", width, height);
			app->m_VulkanContext->FramebufferResized(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
		});

		// Setup GLFW event handlers
		glfwSetWindowSizeCallback(m_Window, [](GLFWwindow* window, const int width, const int height)
		{
			const auto app = static_cast<Application*>(glfwGetWindowUserPointer(window));

			const auto event = std::make_shared<WindowResizedEvent>(width, height);
			app->PushEvent(event);
		});

		/*glfwSetWindowCloseCallback(m_Window, [](GLFWwindow* window)
		{
			const auto app = static_cast<Application*>(glfwGetWindowUserPointer(window));

			const auto event = std::make_shared<WindowClosedEvent>();
			app->PushEvent(event);
		});*/

		glfwSetKeyCallback(m_Window, [](GLFWwindow* window, const int key, const int scancode, const int action, const int mods)
		{
			const auto app = static_cast<Application*>(glfwGetWindowUserPointer(window));

			switch (action)
			{
				case GLFW_PRESS:
				{
					const auto event = std::make_shared<KeyPressedEvent>(key, 0);
					app->PushEvent(event);
					break;
				}

				case GLFW_RELEASE:
				{
					const auto event = std::make_shared<KeyReleasedEvent>(key);
					app->PushEvent(event);
					break;
				}

				case GLFW_REPEAT:
				{
					const auto event = std::make_shared<KeyPressedEvent>(key, 1);
					app->PushEvent(event);
					break;
				}

				default:
					break;
			}
		});

		glfwSetCharCallback(m_Window, [](GLFWwindow* window, const unsigned int keycode)
		{
			const auto app = static_cast<Application*>(glfwGetWindowUserPointer(window));

			const auto event = std::make_shared<KeyTypedEvent>(keycode);
			app->PushEvent(event);
		});

		glfwSetMouseButtonCallback(m_Window, [](GLFWwindow* window, const int button, const int action, const int mods)
		{
			const auto app = static_cast<Application*>(glfwGetWindowUserPointer(window));
			switch (action)
			{
				case GLFW_PRESS:
				{
					const auto event = std::make_shared<MouseButtonPressedEvent>(button);
					app->PushEvent(event);
					break;
				}
				case GLFW_RELEASE:
				{
					const auto event = std::make_shared<MouseButtonReleasedEvent>(button);
					app->PushEvent(event);
					break;
				}
				default:
					break;
			}
		});

		glfwSetScrollCallback(m_Window, [](GLFWwindow* window, const double xOffset, const double yOffset)
		{
			const auto app = static_cast<Application*>(glfwGetWindowUserPointer(window));

			const auto event = std::make_shared<MouseScrolledEvent>(static_cast<float>(xOffset), static_cast<float>(yOffset));
			app->PushEvent(event);
		});

		glfwSetCursorPosCallback(m_Window, [](GLFWwindow* window, const double xPos, const double yPos)
		{
			const auto app = static_cast<Application*>(glfwGetWindowUserPointer(window));

			const auto event = std::make_shared<MouseMovedEvent>(static_cast<float>(xPos), static_cast<float>(yPos));
			app->PushEvent(event);
		});

		glfwSetDropCallback(m_Window, [](GLFWwindow* window, const int pathCount, const char* paths[])
		{
			const auto app = static_cast<Application*>(glfwGetWindowUserPointer(window));

			std::vector<std::filesystem::path> filepaths(pathCount);
			for (int i = 0; i < pathCount; ++i)
			{
				filepaths[i] = paths[i];
			}

			const auto event = std::make_shared<WindowDropEvent>(filepaths);
			app->PushEvent(event);
		});

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

			// Process events
			while (!m_EventQueue.empty())
			{
				const auto event = m_EventQueue.front();
				for (const auto& layer : m_Layers)
				{
					layer->OnEvent(event);
				}
				m_EventQueue.pop();
			}

			for (const auto& layer : m_Layers)
			{
				layer->OnUpdate(deltaTime);
			}
			KBR_CORE_TRACE("Layers OnUpdate complete.");

			m_VulkanContext->PrepareImGuiFrame();
			KBR_CORE_TRACE("ImGui frame prepared.");

			for (const auto& layer : m_Layers)
			{
				layer->OnImGuiRender();
			}
			KBR_CORE_TRACE("Layers ImGui render complete.");

			m_VulkanContext->RenderImGui();
			KBR_CORE_TRACE("ImGui rendered.");

			m_VulkanContext->Draw();
			KBR_CORE_TRACE("Frame drawn.");
			m_VulkanContext->Present();
			KBR_CORE_TRACE("Frame presented.");
		}
	}
}
