#pragma once

#include "Vulkan.hpp"
#include "Renderer/VMA/VMA.hpp""
#include "Utils/MemoryBudget.hpp"

#include <vector>

struct GLFWwindow;

namespace Kerberos
{
	class VulkanContext final
	{
	public:
		explicit VulkanContext(GLFWwindow* window);
		~VulkanContext();

		VulkanContext(const VulkanContext& other) = delete;
		VulkanContext(VulkanContext&& other) noexcept = delete;
		VulkanContext& operator=(const VulkanContext& other) = delete;
		VulkanContext& operator=(VulkanContext&& other) noexcept = delete;

		void PrepareImGuiFrame();
		void RenderImGui();
		void Draw();
		void Present();

		vk::raii::CommandBuffer BeginSingleTimeCommands() const;
		void EndSingleTimeCommands(const vk::raii::CommandBuffer& commandBuffer) const;

		void CopyBuffer(
			const vk::raii::Buffer& srcBuffer,
			const vk::raii::Buffer& dstBuffer,
			vk::DeviceSize size,
			const vk::raii::Semaphore* waitSemaphore = nullptr,
			const vk::raii::Semaphore* signalSemaphore = nullptr
		) const;

		void TransitionImageLayout(const vk::raii::Image& image, 
								   vk::ImageLayout oldLayout,
								   vk::ImageLayout newLayout, 
								   uint32_t mipLevels) const;

		void TransitionImageLayout(const vk::raii::CommandBuffer& copyCmd,
								   const vk::raii::Image& image,
								   vk::ImageLayout oldLayout,
								   vk::ImageLayout newLayout,
								   const vk::ImageSubresourceRange& subresourceRange, 
								   vk::PipelineStageFlags2 srcStageMask = vk::PipelineStageFlagBits2::eAllCommands, 
								   vk::PipelineStageFlags2 dstStageMask = vk::PipelineStageFlagBits2::eAllCommands) const;

		vk::Format FindSupportedFormat(const std::vector<vk::Format>& candidates,
									   vk::ImageTiling tiling,
									   vk::FormatFeatureFlags features) const;

		uint32_t GetMaxFramesInFlight() const;

		const VMA::Allocator& GetAllocator() const { return m_Allocator; }

		void WaitIdle() const;

		void SetObjectDebugName(uint64_t objectHandle, vk::ObjectType objectType, const std::string& name) const;

		template<typename T>
		void SetObjectDebugName(const T& object, const std::string& name) const
			requires requires { T::objectType; }
		{
			SetObjectDebugName(
				reinterpret_cast<uint64_t>(static_cast<T::CType>(*object)),
				T::objectType,
				name
			);
		}

		MemoryBudgetInfo GetMemoryBudgetInfo() const;

		vk::raii::Device& GetDevice();
		vk::raii::PhysicalDevice& GetPhysicalDevice();
		vk::PhysicalDeviceProperties GetProperties() const;
		vk::PhysicalDeviceMemoryProperties GetMemoryProperties() const;
		vk::FormatProperties GetFormatProperties(vk::Format format) const;
		vk::SampleCountFlagBits GetMSAASamples() const;

		static vk::DescriptorSet GenerateImGuiDescriptorSet(const vk::raii::Sampler& sampler,
															const vk::raii::ImageView& imageView, 
															vk::ImageLayout imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal);
		static void DestroyImGuiDescriptorSet(const vk::DescriptorSet& descriptorSet);

		void FramebufferResized(uint32_t width, uint32_t height);

		static VulkanContext& Get() { return *s_Instance; }

	private:
		void RecordCommandBuffer(uint32_t imageIndex) const;

		void CreateInstance();
		void SetupDebugMessenger();
		void CreateSurface();
		void PickPhysicalDevice();
		void CreateLogicalDevice();
		void CreateAllocator();
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

		void TransitionImageLayout(
			vk::Image image,
			vk::ImageLayout oldLayout,
			vk::ImageLayout newLayout,
			vk::AccessFlags2 srcAccessMask,
			vk::AccessFlags2 dstAccessMask,
			vk::PipelineStageFlags2 srcStageMask,
			vk::PipelineStageFlags2 dstStageMask,
			vk::ImageAspectFlagBits aspectFlags
		) const;

		void Cleanup() const;

		vk::Format FindDepthFormat() const;
		vk::Extent2D ChooseSwapExtent(const vk::SurfaceCapabilitiesKHR& capabilities) const;
		static uint32_t FindQueueFamilies(const vk::raii::PhysicalDevice& physicalDevice);
		static bool HasStencilComponent(vk::Format format);
		static vk::SampleCountFlagBits GetMaxUsableSampleCount(const vk::raii::PhysicalDevice& physicalDevice);
		static std::vector<char const*> GetRequiredExtensions();

	private:
		GLFWwindow* m_Window = nullptr;
		vk::raii::Context m_Context;
		vk::raii::Instance m_Instance = nullptr;
		vk::raii::DebugUtilsMessengerEXT m_DebugMessenger = nullptr;

		vk::raii::PhysicalDevice m_PhysicalDevice = nullptr;
		vk::raii::Device m_Device = nullptr;
		vk::raii::Queue m_GraphicsQueue = nullptr;
		vk::raii::Queue m_PresentQueue = nullptr;
		vk::raii::Queue m_ComputeQueue = nullptr;
		vk::raii::Queue m_TransferQueue = nullptr;
		uint32_t m_GraphicsQueueFamilyIndex = 0;
		uint32_t m_PresentQueueFamilyIndex = 0;

		VMA::Allocator m_Allocator{};

		vk::SampleCountFlagBits m_MSAASamples = vk::SampleCountFlagBits::e1;

		vk::raii::SurfaceKHR m_Surface = nullptr;

		vk::raii::SwapchainKHR m_SwapChain = nullptr;
		vk::Format m_SwapChainImageFormat = vk::Format::eUndefined;
		vk::Extent2D m_SwapChainExtent;

		std::vector<vk::Image> m_SwapChainImages;
		std::vector<vk::raii::ImageView> m_SwapChainImageViews;

		vk::raii::Image m_ColorImage = nullptr;
		vk::raii::DeviceMemory m_ColorImageMemory = nullptr;
		vk::raii::ImageView m_ColorImageView = nullptr;

		vk::raii::Image m_DepthImage = nullptr;
		vk::raii::DeviceMemory m_DepthImageMemory = nullptr;
		vk::raii::ImageView m_DepthImageView = nullptr;
		vk::Format m_DepthFormat = vk::Format::eUndefined;

		vk::raii::DescriptorPool m_DescriptorPool = nullptr;
		std::vector<vk::raii::DescriptorSet> m_DescriptorSets;
		vk::raii::DescriptorPool m_ImGuiDescriptorPool = nullptr;

		vk::raii::DescriptorSetLayout m_DescriptorSetLayout = nullptr;
		vk::raii::PipelineLayout m_PipelineLayout = nullptr;
		vk::raii::Pipeline m_GraphicsPipeline = nullptr;

		vk::raii::CommandPool m_CommandPool = nullptr;
		std::vector<vk::raii::CommandBuffer> m_CommandBuffers;

		std::vector<vk::raii::Semaphore> m_PresentCompleteSemaphores;
		std::vector<vk::raii::Semaphore> m_RenderFinishedSemaphores;
		std::vector<vk::raii::Fence> m_InFlightFences;

		bool m_FramebufferResized = false;

		uint32_t m_FrameIndex = 0;
		uint32_t m_CurrentImageIndex = 0;

		MemoryBudget m_MemoryBudget;

		// Singleton instance
		static VulkanContext* s_Instance;
	};
} 