#pragma once

#include "Vulkan.hpp"

#include <vector>

class GLFWwindow;

namespace kbr
{
	class VulkanContext
	{
	public:
		explicit VulkanContext(GLFWwindow* window);
		virtual ~VulkanContext();

		vk::raii::CommandBuffer BeginSingleTimeCommands() const;
		void EndSingleTimeCommands(const vk::raii::CommandBuffer& commandBuffer) const;

		uint32_t GetMaxFramesInFlight() const;

	private:
		void CreateInstance();
		void SetupDebugMessenger();
		void CreateSurface();
		void PickPhysicalDevice();
		void CreateLogicalDevice();
		void CreateSwapChain();
		void CreateSwapChainImageViews();
		void CreateCommandPool();
		void CreateCommandBuffers();
		void CreateSyncObjects();
		void CreateColorResources();
		void CreateDepthResources();
		void CreateImGuiDescriptorPool();
		void SetupImGui();

		void CleanupSwapChain();
		void RecreateSwapchain();


		vk::Format FindDepthFormat() const;
		vk::Extent2D ChooseSwapExtent(const vk::SurfaceCapabilitiesKHR& capabilities) const;
		static uint32_t FindQueueFamilies(const vk::raii::PhysicalDevice& physicalDevice);
		static bool HasStencilComponent(vk::Format format);
		static vk::SampleCountFlagBits GetMaxUsableSampleCount(const vk::raii::PhysicalDevice& physicalDevice);
		static std::vector<char const*> GetRequiredExtensions();

	private:
		GLFWwindow* window = nullptr;
		vk::raii::Context context;
		vk::raii::Instance instance = nullptr;
		vk::raii::DebugUtilsMessengerEXT debugMessenger = nullptr;

		vk::raii::PhysicalDevice physicalDevice = nullptr;
		vk::raii::Device device = nullptr;
		vk::raii::Queue graphicsQueue = nullptr;
		vk::raii::Queue presentQueue = nullptr;
		uint32_t graphicsQueueFamilyIndex = 0;
		uint32_t presentQueueFamilyIndex = 0;

		vk::SampleCountFlagBits msaaSamples = vk::SampleCountFlagBits::e1;

		vk::raii::SurfaceKHR surface = nullptr;

		vk::raii::SwapchainKHR swapChain = nullptr;
		vk::Format swapChainImageFormat = vk::Format::eUndefined;
		vk::Extent2D swapChainExtent;

		std::vector<vk::Image> swapChainImages;
		std::vector<vk::raii::ImageView> swapChainImageViews;

		vk::raii::Image colorImage = nullptr;
		vk::raii::DeviceMemory colorImageMemory = nullptr;
		vk::raii::ImageView colorImageView = nullptr;

		vk::raii::Image depthImage = nullptr;
		vk::raii::DeviceMemory depthImageMemory = nullptr;
		vk::raii::ImageView depthImageView = nullptr;
		vk::Format depthFormat = vk::Format::eUndefined;

		vk::raii::DescriptorPool descriptorPool = nullptr;
		std::vector<vk::raii::DescriptorSet> descriptorSets;
		vk::raii::DescriptorPool imGuiDescriptorPool = nullptr;

		vk::raii::DescriptorSetLayout descriptorSetLayout = nullptr;
		vk::raii::PipelineLayout pipelineLayout = nullptr;
		vk::raii::Pipeline graphicsPipeline = nullptr;

		vk::raii::CommandPool commandPool = nullptr;
		std::vector<vk::raii::CommandBuffer> commandBuffers;

		std::vector<vk::raii::Semaphore> presentCompleteSemaphores;
		std::vector<vk::raii::Semaphore> renderFinishedSemaphores;
		std::vector<vk::raii::Fence> inFlightFences;

		bool framebufferResized = false;

		uint32_t frameIndex = 0;
	};
} 