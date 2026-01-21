#pragma once

#include "VulkanContext.hpp"
#include "Layer.hpp"

#include <memory>
#include <mutex>
#include <vector>
#include <queue>
#include <functional>

#include "events/Event.hpp"

struct GLFWwindow;

namespace kbr
{
	class Application
	{
	public:
		Application();
		~Application();

		Application(const Application& other) = delete;
		Application(Application&& other) noexcept = delete;
		Application& operator=(const Application& other) = delete;
		Application& operator=(Application&& other) noexcept = delete;

		void Run();

		template<typename T> 
			requires std::is_base_of_v<Layer, T>
		void PushLayer();

		void PushEvent(const std::shared_ptr<Event>& event)
		{
			//std::lock_guard<std::mutex> lock(m_QueueMutex);
			m_EventQueue.push(event);
		}

		static Application& Get() { return *s_Instance; }
		GLFWwindow* GetWindow() const { return m_Window; }

	private:
		GLFWwindow* m_Window;
		std::unique_ptr<VulkanContext> m_VulkanContext;

		std::vector<std::unique_ptr<Layer>> m_Layers;
		float m_LastFrameTime = 0.0f;

		std::queue<std::function<void()>> m_MainThreadQueue;
		std::mutex m_QueueMutex;

		std::queue<std::shared_ptr<Event>> m_EventQueue;

		static Application* s_Instance;
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
