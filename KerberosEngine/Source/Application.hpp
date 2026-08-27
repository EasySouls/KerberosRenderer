#pragma once

#include "VulkanContext.hpp"
#include "Events/Event.hpp"
#include "Events/WindowClosedEvent.hpp"
#include "Audio/AudioManager.hpp"
#include "Logging/Log.hpp"
#include "Core/Core.hpp"
#include "Layer.hpp"

#include <memory>
#include <mutex>
#include <vector>
#include <queue>
#include <functional>
#include <filesystem>
#include <future>
#include <thread>


struct GLFWwindow;

namespace Kerberos
{
	struct ApplicationCommandLineArgs
	{
		int Count = 0;
		char** Args = nullptr;

		const char* operator[](const int index) const
		{
			KBR_CORE_ASSERT(index < Count, "Wrong index into ApplicationCommandLineArgs");
			return Args[index];
		}
	};

	struct ApplicationSpecification
	{
		std::string Name = "Kerberos Application";
		std::filesystem::path WorkingDirectory;
		ApplicationCommandLineArgs CommandLineArgs;
	};

	class Application
	{
	public:
		// GPU upload jobs are produced by workers but are always executed by the
		// render thread with a live Vulkan context. Captures transfer ownership of
		// any staging data to the queue until the returned completion is signaled.
		using GPUUploadJob = std::function<void(VulkanContext&)>;

		explicit Application(const ApplicationSpecification& spec);
		virtual ~Application();

		Application(const Application& other) = delete;
		Application(Application&& other) noexcept = delete;
		Application& operator=(const Application& other) = delete;
		Application& operator=(Application&& other) noexcept = delete;

		void Run();

		void Close();

		template<typename T> 
			requires std::is_base_of_v<Layer, T>
		void PushLayer();

		void OnEvent(Event& event);

		void SubmitToMainThreadQueue(const std::function<void()>& fn);
		std::shared_future<void> SubmitToGPUUploadQueue(GPUUploadJob job);

		void BlockEvents(bool shouldBlock);

		static Application& Get() { return *s_Instance; }

		GLFWwindow* GetWindow() const { return m_Window; }
		AudioManager* GetAudioManager() const { return m_AudioManager.get(); }
		const ApplicationSpecification& GetSpecification() const { return m_Specification; }

	private:
		void ExecuteMainThreadQueue();
		void ExecuteGPUUploadQueue();

		bool OnWindowClose(const WindowClosedEvent& event);

	private:
		ApplicationSpecification m_Specification;

		GLFWwindow* m_Window;
		Owner<VulkanContext> m_VulkanContext;
		Owner<AudioManager> m_AudioManager;

		std::vector<Owner<Layer>> m_Layers;
		float m_LastFrameTime = 0.0f;

		bool m_IsRunning = true;

		std::queue<std::function<void()>> m_MainThreadQueue;
		std::mutex m_QueueMutex;

		struct GPUUploadQueueEntry
		{
			GPUUploadJob Job;
			std::promise<void> Completion;
		};

		std::queue<GPUUploadQueueEntry> m_GPUUploadQueue;
		std::mutex m_GPUUploadQueueMutex;
		std::thread::id m_RenderThreadId;

		bool m_BlockEvents = false;

		static Application* s_Instance;
	};

	template <typename T> 
		requires std::is_base_of_v<Layer, T>
	void Application::PushLayer() 
	{
        auto layer = CreateOwner<T>();
		layer->OnAttach();

		m_Layers.emplace_back(std::move(layer));
	}
}
