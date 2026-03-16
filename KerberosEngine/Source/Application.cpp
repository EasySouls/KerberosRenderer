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

#include "Events/WindowClosedEvent.hpp"

#if defined(KBR_PLATFORM_WINDOWS)
extern "C"
{
	__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
	__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

namespace Kerberos
{
	Application* Application::s_Instance = nullptr;

	static void GLFWErrorCallback(const int error, const char* description)
	{
		KBR_CORE_ERROR("GLFW Error ({0}): {1}", error, description);
	}

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

		m_AudioManager.reset(AudioManager::Create());
		m_AudioManager->Init();

		// Initialize GLFW
		if (!glfwInit())
		{
			throw std::runtime_error("Failed to initialize GLFW");
		}

		glfwSetErrorCallback(GLFWErrorCallback);

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
			const auto& app = *static_cast<Application*>(glfwGetWindowUserPointer(window));

			KBR_CORE_INFO("Framebuffer resized: ({}, {})", width, height);
			app.m_VulkanContext->FramebufferResized(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
		});

		// Setup GLFW event handlers
		glfwSetWindowSizeCallback(m_Window, [](GLFWwindow* window, const int width, const int height)
		{
			auto& app = *static_cast<Application*>(glfwGetWindowUserPointer(window));

			auto event = WindowResizedEvent(width, height);
			app.OnEvent(event);
		});

		glfwSetWindowCloseCallback(m_Window, [](GLFWwindow* window)
		{
			auto& app = *static_cast<Application*>(glfwGetWindowUserPointer(window));

			auto event = WindowClosedEvent();
			app.OnEvent(event);
		});

		glfwSetKeyCallback(m_Window, [](GLFWwindow* window, const int key, const int scancode, const int action, const int mods)
		{
			auto& app = *static_cast<Application*>(glfwGetWindowUserPointer(window));

			switch (action)
			{
				case GLFW_PRESS:
				{
					auto event = KeyPressedEvent(key, 0);
					app.OnEvent(event);
					break;
				}

				case GLFW_RELEASE:
				{
					auto event = KeyReleasedEvent(key);
					app.OnEvent(event);
					break;
				}

				case GLFW_REPEAT:
				{
					auto event = KeyPressedEvent(key, 1);
					app.OnEvent(event);
					break;
				}

				default:
					break;
			}
		});

		glfwSetCharCallback(m_Window, [](GLFWwindow* window, const unsigned int keycode)
		{
			auto& app = *static_cast<Application*>(glfwGetWindowUserPointer(window));

			auto event = KeyTypedEvent(keycode);
			app.OnEvent(event);
		});

		glfwSetMouseButtonCallback(m_Window, [](GLFWwindow* window, const int button, const int action, const int mods)
		{
			auto& app = *static_cast<Application*>(glfwGetWindowUserPointer(window));
			switch (action)
			{
				case GLFW_PRESS:
				{
					auto event = MouseButtonPressedEvent(button);
					app.OnEvent(event);
					break;
				}
				case GLFW_RELEASE:
				{
					auto event = MouseButtonReleasedEvent(button);
					app.OnEvent(event);
					break;
				}
				default:
					break;
			}
		});

		glfwSetScrollCallback(m_Window, [](GLFWwindow* window, const double xOffset, const double yOffset)
		{
			auto& app = *static_cast<Application*>(glfwGetWindowUserPointer(window));

			auto event = MouseScrolledEvent(static_cast<float>(xOffset), static_cast<float>(yOffset));
			app.OnEvent(event);
		});

		glfwSetCursorPosCallback(m_Window, [](GLFWwindow* window, const double xPos, const double yPos)
		{
			auto& app = *static_cast<Application*>(glfwGetWindowUserPointer(window));

			auto event = MouseMovedEvent(static_cast<float>(xPos), static_cast<float>(yPos));
			app.OnEvent(event);
		});

		glfwSetDropCallback(m_Window, [](GLFWwindow* window, const int pathCount, const char* paths[])
		{
			auto& app = *static_cast<Application*>(glfwGetWindowUserPointer(window));

			std::vector<std::filesystem::path> filepaths(pathCount);
			for (int i = 0; i < pathCount; ++i)
			{
				filepaths[i] = paths[i];
			}

			auto event = WindowDropEvent(filepaths);
			app.OnEvent(event);
		});

		m_VulkanContext = CreateOwner<VulkanContext>(m_Window);
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
		while (!glfwWindowShouldClose(m_Window) && m_IsRunning)
		{
			glfwPollEvents();

			const float time = static_cast<float>(glfwGetTime());
			const float deltaTime = time - m_LastFrameTime;
			m_LastFrameTime = time;

			ExecuteMainThreadQueue();

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

	void Application::Close()
	{
		m_IsRunning = false;
	}

	void Application::OnEvent(Event& event) 
	{
		EventDispatcher dispatcher(event);
		dispatcher.Dispatch<WindowClosedEvent>(KBR_BIND_FN(Application::OnWindowClose));
		// dispatcher.Dispatch<WindowResizeEvent>(KBR_BIND_FN(Application::OnWindowResize));

		for (const auto& layer : m_Layers)
		{
			layer->OnEvent(event);
			if (event.Handled)
				break;
		}
	}

	void Application::SubmitToMainThreadQueue(const std::function<void()>& fn) 
	{
		std::scoped_lock lock(m_QueueMutex);

		m_MainThreadQueue.push(fn);
	}

	void Application::ExecuteMainThreadQueue() 
	{
		std::queue<std::function<void()>> functions;
		{
			std::scoped_lock lock(m_QueueMutex);
			functions.swap(m_MainThreadQueue);
		}

		while (!functions.empty())
		{
			const auto fn = functions.front();
			functions.pop();
			fn();
		}
	}

	bool Application::OnWindowClose(const WindowClosedEvent&) 
	{
		m_IsRunning = false;
		return true;
	}
}
