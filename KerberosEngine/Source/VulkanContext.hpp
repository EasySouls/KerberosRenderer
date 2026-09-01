#pragma once

#include "Vulkan.hpp"
#include "Renderer/VMA/VMA.hpp"
#include "Utils/MemoryBudget.hpp"

#include "Renderer/Textures/Texture2D.hpp"
#include "Renderer/ImGuiDescriptorSetManager.hpp"
#include <limits>
#include <optional>
#include <vector>
#include <type_traits>
#include <functional>


struct GLFWwindow;

namespace Kerberos
{
	class VulkanContext final
	{
	public:
		static constexpr uint32_t MaxFramesInFlight = 2;

		explicit VulkanContext(GLFWwindow* window);
		~VulkanContext();

		VulkanContext(const VulkanContext& other) = delete;
		VulkanContext(VulkanContext&& other) noexcept = delete;
		VulkanContext& operator=(const VulkanContext& other) = delete;
		VulkanContext& operator=(VulkanContext&& other) noexcept = delete;

		static void PrepareImGuiFrame();
		static void RenderImGui();
		void Draw();
		void Present();

		vk::raii::CommandBuffer BeginSingleTimeCommands() const;
		void EndSingleTimeCommands(const vk::raii::CommandBuffer& commandBuffer,
			const vk::raii::Semaphore* signalTimelineSemaphore = nullptr,
			uint64_t signalTimelineValue = 0) const;

		enum class OperationType : std::uint8_t
		{
			Graphics,
			Compute,
			Transfer
		};

		void Submit(OperationType type, const std::function<void(const vk::raii::CommandBuffer&)>& cmd) const;

		vk::raii::CommandBuffer BeginSingleTimeCommands(OperationType type) const;
		void EndSingleTimeCommands(const vk::raii::CommandBuffer& commandBuffer, OperationType type) const;

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
		uint32_t GetCurrentFrameIndex() const { return m_FrameIndex; }
		uint32_t GetFrameCount() const { return m_FrameCount; }

		const VMA::Allocator& GetAllocator() const { return m_Allocator; }

		void WaitIdle() const;

		void SetObjectDebugName(uint64_t objectHandle, vk::ObjectType objectType, const std::string& name) const;
		void SetObjectDebugName(VmaAllocation allocation, const std::string& name) const;

		template <typename Handle>
		static uint64_t RawHandleToUint64(Handle handle)
		{
			if constexpr (std::is_pointer_v<Handle>)
			{
				return reinterpret_cast<uint64_t>(handle);
			}
			else
			{
				return static_cast<uint64_t>(handle);
			}
		}

		template <typename T>
		void SetObjectDebugName(const T& object, const std::string& name) const
			requires requires { T::objectType; }
		{
			uint64_t rawHandle;

			if constexpr (requires { typename T::CppType; })
			{
				rawHandle = RawHandleToUint64(static_cast<typename T::CType>(*object));
			}
			else if constexpr (requires { typename T::CType; })
			{
				rawHandle = RawHandleToUint64(static_cast<typename T::CType>(object));
			}
			else
			{
				rawHandle = RawHandleToUint64(object);
			}

			SetObjectDebugName(rawHandle, T::objectType, name);
		}

		struct QueueFamilyInfo
		{
			uint32_t graphics = (std::numeric_limits<uint32_t>::max)();
			uint32_t present = (std::numeric_limits<uint32_t>::max)();
			std::optional<uint32_t> compute{};
			std::optional<uint32_t> transfer{};

			[[nodiscard]] bool HasSeparateComputeQueue() const { return compute.has_value() && compute.value() != graphics; }
			[[nodiscard]] bool HasSeparateTransferQueue() const { return transfer.has_value() && transfer.value() != graphics; }
			[[nodiscard]] bool HasDedicatedTransferQueue() const
			{
				return transfer.has_value() && transfer.value() != graphics && (!compute.has_value() || transfer.value() != compute.value());
			}
		};

		MemoryBudgetInfo GetMemoryBudgetInfo() const;

		vk::raii::Device& GetDevice();
		vk::raii::PhysicalDevice& GetPhysicalDevice();
		vk::PhysicalDeviceProperties2 GetProperties() const;
		vk::PhysicalDeviceMemoryProperties2 GetMemoryProperties() const;
		vk::FormatProperties2 GetFormatProperties(vk::Format format) const;
		vk::SampleCountFlagBits GetMaxMSAASamples() const;
		float GetMaxAnisotropy() const;
		const QueueFamilyInfo& GetQueueFamilyInfo() const { return m_QueueFamilyInfo; }

		bool IsExtensionActive(const char* extensionName) const;
		bool UseDescriptorBuffers() const;

		static vk::DescriptorSet GenerateImGuiDescriptorSet(const vk::raii::Sampler& sampler,
															const vk::raii::ImageView& imageView, 
															vk::ImageLayout imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal);
		static void DestroyImGuiDescriptorSet(const vk::DescriptorSet& descriptorSet);
		uint64_t GetImGuiRendererID(const Ref<Texture2D>& texture);

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
		void CreateCommandPools();
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
		static QueueFamilyInfo FindQueueFamilies(const vk::raii::PhysicalDevice& physicalDevice, const vk::raii::SurfaceKHR& surface);
		static bool HasStencilComponent(vk::Format format);
		static vk::SampleCountFlagBits GetMaxUsableSampleCount(const vk::raii::PhysicalDevice& physicalDevice);
		static std::vector<char const*> GetRequiredExtensions();

	private:
		GLFWwindow* m_Window = nullptr;
		vk::raii::Context m_Context;
		vk::raii::Instance m_Instance = nullptr;
		vk::raii::DebugUtilsMessengerEXT m_DebugMessenger = nullptr;

		std::vector<const char*> m_ActiveDeviceExtensions;

		vk::raii::PhysicalDevice m_PhysicalDevice = nullptr;
		std::string m_PhysicalDeviceName;
		vk::StructureChain<vk::PhysicalDeviceFeatures2, 
			vk::PhysicalDeviceVulkan11Features, 
			vk::PhysicalDeviceVulkan12Features, 
			vk::PhysicalDeviceVulkan13Features, 
			vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT, 
			vk::PhysicalDeviceAccelerationStructureFeaturesKHR, 
			vk::PhysicalDeviceRayQueryFeaturesKHR,
			vk::PhysicalDeviceDescriptorBufferFeaturesEXT,
			vk::PhysicalDeviceMeshShaderFeaturesEXT,
			vk::PhysicalDeviceShaderObjectFeaturesEXT> m_SupportedFeatures;
		vk::raii::Device m_Device = nullptr;
		vk::raii::Queue m_GraphicsQueue = nullptr;
		vk::raii::Queue m_PresentQueue = nullptr;
		vk::raii::Queue m_ComputeQueue = nullptr;
		vk::raii::Queue m_TransferQueue = nullptr;
        QueueFamilyInfo m_QueueFamilyInfo{};

		VMA::Allocator m_Allocator{};

		vk::SampleCountFlagBits m_MaxMSAASamples = vk::SampleCountFlagBits::e1;

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

		vk::raii::CommandPool m_GraphicsCommandPool = nullptr;
		vk::raii::CommandPool m_ComputeCommandPool = nullptr;
		vk::raii::CommandPool m_TransferCommandPool = nullptr;
		std::vector<vk::raii::CommandBuffer> m_CommandBuffers;

		std::vector<vk::raii::Semaphore> m_PresentCompleteSemaphores;
		std::vector<vk::raii::Semaphore> m_RenderFinishedSemaphores;
		std::vector<vk::raii::Fence> m_InFlightFences;

		bool m_FramebufferResized = false;

		uint32_t m_FrameIndex = 0;
		uint32_t m_CurrentImageIndex = 0;
		uint32_t m_FrameCount = 0; // Total number of frames rendered since the application started
		uint32_t m_ImageCount = 0; // Number of images in the swapchain

		MemoryBudget m_MemoryBudget;
		ImGuiDescriptorSetManager m_ImGuiDescriptorSetManager;

		// Singleton instance
		static VulkanContext* s_Instance;
	};

	void BeginRenderPassDebugLabel(const vk::raii::CommandBuffer& cmd, const std::string_view labelName);
	void EndRenderPassDebugLabel(const vk::raii::CommandBuffer& cmd);
} 