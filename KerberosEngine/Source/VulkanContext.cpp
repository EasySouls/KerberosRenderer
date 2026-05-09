#include "kbrpch.hpp"
#include "VulkanContext.hpp"

#include "Renderer/Textures/Texture.hpp"
#include <algorithm>
#include <iostream>

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"

#include "Utils.hpp"
#include "logging/Log.hpp"
#include "Renderer/Renderer.hpp"

#define VK_USE_PLATFORM_WIN32_KHR
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include "ImGuizmo.h"

const std::vector<char const*> validationLayers = {
	"VK_LAYER_KHRONOS_validation"
};

const std::vector<const char*> deviceExtensions = {
	vk::KHRSwapchainExtensionName,
	vk::KHRSpirv14ExtensionName,
	vk::KHRSynchronization2ExtensionName,
	vk::KHRCreateRenderpass2ExtensionName,

	// TODO: These are not neccessary, implement a fallback when ray tracing is not supported
	vk::KHRAccelerationStructureExtensionName,
	vk::KHRBufferDeviceAddressExtensionName,
	vk::KHRDeferredHostOperationsExtensionName,
	vk::KHRRayQueryExtensionName,
#ifdef KBR_DEBUG
	vk::KHRShaderNonSemanticInfoExtensionName,
	vk::GOOGLEHlslFunctionality1ExtensionName,
	vk::GOOGLEUserTypeExtensionName,
#endif
};

#ifdef KBR_DEBUG
constexpr bool enableValidationLayers = true;
#else
constexpr bool enableValidationLayers = false;
#endif

static VKAPI_ATTR vk::Bool32 VKAPI_CALL DebugCallback(
	const vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
	const vk::DebugUtilsMessageTypeFlagsEXT type,
	const vk::DebugUtilsMessengerCallbackDataEXT* pCallbackData,
	void*)
{
	if (type & vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral) {
		if (severity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose) {
			KBR_CORE_TRACE("General|Verbose: {}|{}: {}", pCallbackData->messageIdNumber, pCallbackData->pMessageIdName, pCallbackData->pMessage);
		}
		else if (severity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo) {
			KBR_CORE_INFO("General|Info: {}|{}: {}", pCallbackData->messageIdNumber, pCallbackData->pMessageIdName, pCallbackData->pMessage);
		}
		else if (severity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning) {
			KBR_CORE_WARN("General|Warning: {}|{}: {}", pCallbackData->messageIdNumber, pCallbackData->pMessageIdName, pCallbackData->pMessage);
		}
		else if (severity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eError) {
			KBR_CORE_ERROR("General|Error: {}|{}: {}", pCallbackData->messageIdNumber, pCallbackData->pMessageIdName, pCallbackData->pMessage);
		}
	}
	else if (type & vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation) {
		if (severity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose) {
			KBR_CORE_TRACE("Validation|Verbose: {}|{}: {}", pCallbackData->messageIdNumber, pCallbackData->pMessageIdName, pCallbackData->pMessage);
		}
		else if (severity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo) {
			KBR_CORE_INFO("Validation|Info: {}|{}: {}", pCallbackData->messageIdNumber, pCallbackData->pMessageIdName, pCallbackData->pMessage);
		}
		else if (severity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning) {
			KBR_CORE_WARN("Validation|Warning: {}|{}: {}", pCallbackData->messageIdNumber, pCallbackData->pMessageIdName, pCallbackData->pMessage);
		}
		else if (severity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eError) {
			KBR_CORE_ERROR("Validation|Error: {}|{}: {}", pCallbackData->messageIdNumber, pCallbackData->pMessageIdName, pCallbackData->pMessage);
		}
	}
	else if (type & vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance) {
		if (severity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose) {
			KBR_CORE_TRACE("Performance|Verbose: {}|{}: {}", pCallbackData->messageIdNumber, pCallbackData->pMessageIdName, pCallbackData->pMessage);
		}
		else if (severity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo) {
			KBR_CORE_INFO("Performance|Info: {}|{}: {}", pCallbackData->messageIdNumber, pCallbackData->pMessageIdName, pCallbackData->pMessage);
		}
		else if (severity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning) {
			KBR_CORE_WARN("Performance|Warning: {}|{}: {}", pCallbackData->messageIdNumber, pCallbackData->pMessageIdName, pCallbackData->pMessage);
		}
		else if (severity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eError) {
			KBR_CORE_ERROR("Performance|Error: {}|{}: {}", pCallbackData->messageIdNumber, pCallbackData->pMessageIdName, pCallbackData->pMessage);
		}
	}
	else if (type & vk::DebugUtilsMessageTypeFlagBitsEXT::eDeviceAddressBinding) {
		if (severity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose) {
			KBR_CORE_TRACE("DeviceAddressBinding|Verbose: {}|{}: {}", pCallbackData->messageIdNumber, pCallbackData->pMessageIdName, pCallbackData->pMessage);
		}
		else if (severity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo) {
			KBR_CORE_INFO("DeviceAddressBinding|Info: {}|{}: {}", pCallbackData->messageIdNumber, pCallbackData->pMessageIdName, pCallbackData->pMessage);
		}
		else if (severity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning) {
			KBR_CORE_WARN("DeviceAddressBinding|Warning: {}|{}: {}", pCallbackData->messageIdNumber, pCallbackData->pMessageIdName, pCallbackData->pMessage);
		}
		else if (severity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eError) {
			KBR_CORE_ERROR("DeviceAddressBinding|Error: {}|{}: {}", pCallbackData->messageIdNumber, pCallbackData->pMessageIdName, pCallbackData->pMessage);
		}
	}
	//std::cerr << "validation layer: type " << to_string(type) << " msg: " << pCallbackData->pMessage << "\n\n";

	return vk::False;
}

namespace Kerberos
{
	VulkanContext* VulkanContext::s_Instance = nullptr;

	VulkanContext::VulkanContext(GLFWwindow* window)
		: m_Window(window)
	{
		if (s_Instance != nullptr)
		{
			throw std::runtime_error("VulkanContext instance already exists!");
		}
		s_Instance = this;

		CreateInstance();
		SetupDebugMessenger();
		CreateSurface();
		PickPhysicalDevice();
		CreateLogicalDevice();
		CreateAllocator();
		CreateSwapChain();
		CreateSwapChainImageViews();

		CreateColorResources();
		CreateDepthResources();

		CreateCommandPools();
		CreateCommandBuffers();
		CreateSyncObjects();

		CreateImGuiDescriptorPool();
		SetupImGui();
	}

	VulkanContext::~VulkanContext()
	{
		m_ImGuiDescriptorSetManager.Clear();

		Cleanup();
	};

	void VulkanContext::PrepareImGuiFrame()
	{
		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		ImGuizmo::BeginFrame();

		ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);
	}

	void VulkanContext::RenderImGui()
	{
		ImGui::Render();
	}

	void VulkanContext::Draw()
	{
		const vk::Result fenceResult = m_Device.waitForFences(*m_InFlightFences[m_FrameIndex], vk::True, UINT64_MAX);
		if (fenceResult != vk::Result::eSuccess)
		{
			throw std::runtime_error("failed to wait for fence!");
		}

		auto [result, imageIndex] = m_SwapChain.acquireNextImage(UINT64_MAX, *m_PresentCompleteSemaphores[m_FrameIndex], nullptr);
		if (result == vk::Result::eErrorOutOfDateKHR) {
			RecreateSwapchain();
			return;
		}
		if (result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR) {
			throw std::runtime_error("failed to acquire swap chain image!");
		}
		m_CurrentImageIndex = imageIndex;

		m_Device.resetFences(*m_InFlightFences[m_FrameIndex]);

		m_CommandBuffers[m_FrameIndex].reset();
		SetObjectDebugName(m_CommandBuffers[m_FrameIndex], "Render CommandBuffer " + std::to_string(m_FrameIndex));
		RecordCommandBuffer(m_CurrentImageIndex);

		vk::PipelineStageFlags waitDestinationStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput);

		std::array<vk::Semaphore, 2> signalSemaphores = {
			*m_RenderFinishedSemaphores[imageIndex],
			VK_NULL_HANDLE
		};
		std::array<uint64_t, 2> signalSemaphoreValues = { 0, 0 };
		uint32_t signalSemaphoreCount = 1;

		vk::Semaphore rendererTimelineSemaphore = VK_NULL_HANDLE;
		uint64_t rendererTimelineSignalValue = 0;
		if (Renderer::ConsumePendingMousePickingTimelineSignal(rendererTimelineSemaphore, rendererTimelineSignalValue))
		{
			signalSemaphores[1] = rendererTimelineSemaphore;
			signalSemaphoreValues[1] = rendererTimelineSignalValue;
			signalSemaphoreCount = 2;
		}

		vk::TimelineSemaphoreSubmitInfo timelineSubmitInfo{
			.signalSemaphoreValueCount = signalSemaphoreCount,
			.pSignalSemaphoreValues = signalSemaphoreValues.data()
		};

		const vk::SubmitInfo submitInfo{
			.pNext = signalSemaphoreCount > 1 ? &timelineSubmitInfo : nullptr,
			.waitSemaphoreCount = 1,
			.pWaitSemaphores = &*m_PresentCompleteSemaphores[m_FrameIndex],
			.pWaitDstStageMask = &waitDestinationStageMask,
			.commandBufferCount = 1,
			.pCommandBuffers = &*m_CommandBuffers[m_FrameIndex],
			.signalSemaphoreCount = signalSemaphoreCount,
			.pSignalSemaphores = signalSemaphores.data()
		};

		m_GraphicsQueue.submit(submitInfo, *m_InFlightFences[m_FrameIndex]);

		// Refrech the memory budget info
		// In the future it might be enough to call this from the client, since we do not need to update the memory budget info every frame,
		// only when we create or destroy resources, or the memory usage window is opened
		m_MemoryBudget.UpdateMemoryBudgetInfo();
	}

	void VulkanContext::Present()
	{
		const ImGuiIO& io = ImGui::GetIO();
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();
		}

		const vk::PresentInfoKHR presentInfoKHR{
			.waitSemaphoreCount = 1,
			.pWaitSemaphores = &*m_RenderFinishedSemaphores[m_CurrentImageIndex],
			.swapchainCount = 1,
			.pSwapchains = &*m_SwapChain,
			.pImageIndices = &m_CurrentImageIndex
		};

		try 
		{
			const auto result = m_PresentQueue.presentKHR(presentInfoKHR);
			if (result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eSuboptimalKHR || m_FramebufferResized) {
				m_FramebufferResized = false;
				RecreateSwapchain();
			}
			else if (result != vk::Result::eSuccess) {
				throw std::runtime_error("failed to present swap chain image!");
			}
		} 
		catch (const vk::OutOfDateKHRError&) 
		{
			RecreateSwapchain();
		}

		m_FrameIndex = (m_FrameIndex + 1) % VulkanContext::MaxFramesInFlight;
	}

	vk::raii::CommandBuffer VulkanContext::BeginSingleTimeCommands() const
	{
		const vk::CommandBufferAllocateInfo allocInfo{
			.commandPool = *m_GraphicsCommandPool,
			.level = vk::CommandBufferLevel::ePrimary,
			.commandBufferCount = 1
		};
		vk::raii::CommandBuffer commandBuffer = std::move(vk::raii::CommandBuffers(m_Device, allocInfo).front());

		constexpr vk::CommandBufferBeginInfo beginInfo{
			.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit
		};
		commandBuffer.begin(beginInfo);
		return commandBuffer;
	}

   void VulkanContext::EndSingleTimeCommands(const vk::raii::CommandBuffer& commandBuffer,
		const vk::raii::Semaphore* signalTimelineSemaphore,
		const uint64_t signalTimelineValue) const
	{
		commandBuffer.end();

		vk::TimelineSemaphoreSubmitInfo timelineSemaphoreSubmitInfo;
		vk::SubmitInfo submitInfo{
			.commandBufferCount = 1,
			.pCommandBuffers = &*commandBuffer
		};

		if (signalTimelineSemaphore != nullptr)
		{
			timelineSemaphoreSubmitInfo = vk::TimelineSemaphoreSubmitInfo{
				.signalSemaphoreValueCount = 1,
				.pSignalSemaphoreValues = &signalTimelineValue
			};

			submitInfo.signalSemaphoreCount = 1;
			submitInfo.pSignalSemaphores = &**signalTimelineSemaphore;
			submitInfo.pNext = &timelineSemaphoreSubmitInfo;
		}

		m_GraphicsQueue.submit(submitInfo, nullptr);
		m_GraphicsQueue.waitIdle();
	}

	void VulkanContext::Submit(const OperationType type, const std::function<void(const vk::raii::CommandBuffer&)>& cmd) const 
	{
		vk::CommandPool commandPool;
		vk::Queue queue;
		if (type == OperationType::Graphics)
		{
			commandPool = *m_GraphicsCommandPool;
			queue = *m_GraphicsQueue;
		}
		else if (type == OperationType::Compute) 
		{
			commandPool = *m_ComputeCommandPool;
			queue = *m_ComputeQueue;
		}
		else if (type == OperationType::Transfer) 
		{
			commandPool = *m_TransferCommandPool;
			queue = *m_TransferQueue;
		}
		else 
		{
			KBR_CORE_ASSERT(false, "Unsupported operation type!");
			throw std::invalid_argument("unsupported operation type!");
		}

		constexpr vk::FenceCreateInfo oneTimeFenceCreateInfo{};
		const vk::raii::Fence oneTimeFence = vk::raii::Fence(m_Device, oneTimeFenceCreateInfo);

		const vk::CommandBufferAllocateInfo allocInfo{
			.commandPool = commandPool,
			.level = vk::CommandBufferLevel::ePrimary,
			.commandBufferCount = 1
		};
		const vk::raii::CommandBuffer commandBuffer = std::move(vk::raii::CommandBuffers(m_Device, allocInfo).front());

		constexpr vk::CommandBufferBeginInfo beginInfo{
			.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit
		};
		commandBuffer.begin(beginInfo);

		cmd(commandBuffer);
		
		commandBuffer.end();

		const vk::SubmitInfo submitInfo{
			.commandBufferCount = 1,
			.pCommandBuffers = &*commandBuffer
		};
		queue.submit(submitInfo, *oneTimeFence);
		const vk::Result result = m_Device.waitForFences(*oneTimeFence, vk::True, UINT64_MAX);
		KBR_CORE_ASSERT(result == vk::Result::eSuccess, "Failed to wait for fence!");
	}

	vk::raii::CommandBuffer VulkanContext::BeginSingleTimeCommands(OperationType type) const 
	{
		vk::CommandPool commandPool;
		if (type == OperationType::Graphics)
		{
			commandPool = *m_GraphicsCommandPool;
		}
		else if (type == OperationType::Compute)
		{
			commandPool = *m_ComputeCommandPool;
		}
		else if (type == OperationType::Transfer)
		{
			commandPool = *m_TransferCommandPool;
		}
		else
		{
			KBR_CORE_ASSERT(false, "Unsupported operation type!");
			throw std::invalid_argument("unsupported operation type!");
		}

		const vk::CommandBufferAllocateInfo allocInfo{
			.commandPool = commandPool,
			.level = vk::CommandBufferLevel::ePrimary,
			.commandBufferCount = 1
		};
		vk::raii::CommandBuffer commandBuffer = std::move(vk::raii::CommandBuffers(m_Device, allocInfo).front());

		constexpr vk::CommandBufferBeginInfo beginInfo{
			.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit
		};
		commandBuffer.begin(beginInfo);
		return commandBuffer;
	}

	void VulkanContext::EndSingleTimeCommands(const vk::raii::CommandBuffer& commandBuffer, const OperationType type) const 
	{
		vk::Queue queue;
		if (type == OperationType::Graphics)
		{
			queue = *m_GraphicsQueue;
		}
		else if (type == OperationType::Compute)
		{
			queue = *m_ComputeQueue;
		}
		else if (type == OperationType::Transfer)
		{
			queue = *m_TransferQueue;
		}
		else
		{
			KBR_CORE_ASSERT(false, "Unsupported operation type!");
			throw std::invalid_argument("unsupported operation type!");
		}

		constexpr vk::FenceCreateInfo oneTimeFenceCreateInfo{};
		const vk::raii::Fence oneTimeFence = vk::raii::Fence(m_Device, oneTimeFenceCreateInfo);

		commandBuffer.end();

		const vk::SubmitInfo submitInfo{
			.commandBufferCount = 1,
			.pCommandBuffers = &*commandBuffer
		};
		queue.submit(submitInfo, *oneTimeFence);
		const vk::Result result = m_Device.waitForFences(*oneTimeFence, vk::True, UINT64_MAX);
		KBR_CORE_ASSERT(result == vk::Result::eSuccess, "Failed to wait for fence!");
	}

	void VulkanContext::CopyBuffer(
		const vk::raii::Buffer& srcBuffer,
		const vk::raii::Buffer& dstBuffer,
		const vk::DeviceSize size,
		const vk::raii::Semaphore* waitSemaphore,
		const vk::raii::Semaphore* signalSemaphore
	) const
	{
		const vk::CommandBufferAllocateInfo allocInfo{
			.commandPool = *m_TransferCommandPool,
			.level = vk::CommandBufferLevel::ePrimary,
			.commandBufferCount = 1
		};
		const vk::raii::CommandBuffer copyCmdBuffer = std::move(m_Device.allocateCommandBuffers(allocInfo).front());

		constexpr vk::CommandBufferBeginInfo beginInfo{
			.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit
		};
		copyCmdBuffer.begin(beginInfo);
		copyCmdBuffer.copyBuffer(*srcBuffer, *dstBuffer, vk::BufferCopy{ .size = size });
		copyCmdBuffer.end();

		const vk::SubmitInfo submitInfo{
			.waitSemaphoreCount = waitSemaphore ? 1u : 0u,
			.pWaitSemaphores = waitSemaphore ? reinterpret_cast<const vk::Semaphore*>(waitSemaphore) : nullptr,
			.commandBufferCount = 1u,
			.pCommandBuffers = &*copyCmdBuffer,
			.signalSemaphoreCount = signalSemaphore ? 1u : 0u,
			.pSignalSemaphores = signalSemaphore ? reinterpret_cast<const vk::Semaphore*>(signalSemaphore) : nullptr,
		};
		m_TransferQueue.submit(submitInfo, nullptr);
		m_TransferQueue.waitIdle();
	}

	void VulkanContext::TransitionImageLayout(const vk::raii::Image& image, const vk::ImageLayout oldLayout, const vk::ImageLayout newLayout, const uint32_t mipLevels) const
	{
		const auto commandBuffer = BeginSingleTimeCommands();

		vk::ImageMemoryBarrier2 barrier = {
			.oldLayout = oldLayout,
			.newLayout = newLayout,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = image,
			.subresourceRange = {
				.aspectMask = vk::ImageAspectFlagBits::eColor,
				.baseMipLevel = 0,
				.levelCount = mipLevels,
				.baseArrayLayer = 0,
				.layerCount = 1
			}
		};

		if (oldLayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::eTransferDstOptimal) {
			barrier.srcAccessMask = {};
			barrier.dstAccessMask = vk::AccessFlagBits2::eTransferWrite;

			barrier.srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe;
			barrier.dstStageMask = vk::PipelineStageFlagBits2::eTransfer;
		}
		else if (oldLayout == vk::ImageLayout::eTransferDstOptimal && newLayout == vk::ImageLayout::eShaderReadOnlyOptimal) {
			barrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
			barrier.dstAccessMask = vk::AccessFlagBits2::eShaderRead;

			barrier.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
			barrier.dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader;
		}
		else {
			throw std::invalid_argument("unsupported layout transition!");
		}

		const vk::DependencyInfo dependencyInfo = {
			.dependencyFlags = {},
			.imageMemoryBarrierCount = 1,
			.pImageMemoryBarriers = &barrier
		};

		commandBuffer.pipelineBarrier2(dependencyInfo);

		EndSingleTimeCommands(commandBuffer);
	}

	void VulkanContext::TransitionImageLayout(const vk::raii::CommandBuffer& copyCmd, const vk::raii::Image& image,
	                                          vk::ImageLayout oldLayout, vk::ImageLayout newLayout, const vk::ImageSubresourceRange& subresourceRange,
	                                          vk::PipelineStageFlags2 srcStageMask, vk::PipelineStageFlags2 dstStageMask) const
	{
		vk::ImageMemoryBarrier2 barrier = {
			.oldLayout = oldLayout,
			.newLayout = newLayout,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = image,
			.subresourceRange = subresourceRange
		};

		switch (oldLayout)
		{
		case vk::ImageLayout::eUndefined:
			// Image layout is undefined (or does not matter)
			// Only valid as initial layout
			// No flags required, listed only for completeness
			barrier.srcAccessMask = {};
			break;

		case vk::ImageLayout::ePreinitialized:
			// Image is preinitialized
			// Only valid as initial layout for linear images, preserves memory contents
			// Make sure host writes have been finished
			barrier.srcAccessMask = vk::AccessFlagBits2::eHostWrite;
			break;

		case vk::ImageLayout::eColorAttachmentOptimal:
			// Image is a color attachment
			// Make sure any writes to the color buffer have been finished
			barrier.srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite;
			break;

		case vk::ImageLayout::eDepthStencilAttachmentOptimal:
			// Image is a depth/stencil attachment
			// Make sure any writes to the depth/stencil buffer have been finished
			barrier.srcAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
			break;

		case vk::ImageLayout::eTransferSrcOptimal:
			// Image is a transfer source
			// Make sure any reads from the image have been finished
			barrier.srcAccessMask = vk::AccessFlagBits2::eTransferRead;
			break;

		case vk::ImageLayout::eTransferDstOptimal:
			// Image is a transfer destination
			// Make sure any writes to the image have been finished
			barrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
			break;

		case vk::ImageLayout::eShaderReadOnlyOptimal:
			// Image is read by a shader
			// Make sure any shader reads from the image have been finished
			barrier.srcAccessMask = vk::AccessFlagBits2::eShaderRead;
			break;
		default:
			throw std::invalid_argument("unsupported layout transition!");
		}

		switch (newLayout)
		{
		case vk::ImageLayout::eTransferDstOptimal:
			// Image will be used as a transfer destination
			// Make sure any writes to the image have been finished
			barrier.dstAccessMask = vk::AccessFlagBits2::eTransferWrite;
			break;

		case vk::ImageLayout::eTransferSrcOptimal:
			// Image will be used as a transfer source
			// Make sure any reads from the image have been finished
			barrier.dstAccessMask = vk::AccessFlagBits2::eTransferRead;
			break;

		case vk::ImageLayout::eColorAttachmentOptimal:
			// Image will be used as a color attachment
			// Make sure any writes to the color buffer have been finished
			barrier.dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite;
			break;

		case vk::ImageLayout::eDepthStencilAttachmentOptimal:
			// Image layout will be used as a depth/stencil attachment
			// Make sure any writes to depth/stencil buffer have been finished
			barrier.dstAccessMask = barrier.dstAccessMask | vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
			break;

		case vk::ImageLayout::eShaderReadOnlyOptimal:
			// Image will be read in a shader (sampler, input attachment)
			// Make sure any writes to the image have been finished
			if (barrier.srcAccessMask == vk::AccessFlagBits2::eNone)
			{
				barrier.srcAccessMask = vk::AccessFlagBits2::eHostWrite | vk::AccessFlagBits2::eTransferWrite;
			}
			barrier.dstAccessMask = vk::AccessFlagBits2::eShaderRead;
			break;
		default:
			throw std::invalid_argument("unsupported layout transition!");
		}

		barrier.srcStageMask = srcStageMask;
		barrier.dstStageMask = dstStageMask;

		copyCmd.pipelineBarrier2(
			vk::DependencyInfo{
				.dependencyFlags = {},
				.imageMemoryBarrierCount = 1,
				.pImageMemoryBarriers = &barrier
			}
		);
	}

	vk::Format VulkanContext::FindSupportedFormat(
		const std::vector<vk::Format>& candidates,
		const vk::ImageTiling tiling,
		const vk::FormatFeatureFlags features) const
	{
		for (const vk::Format format : candidates)
		{
			const vk::FormatProperties2 props = GetFormatProperties(format);

			if (tiling == vk::ImageTiling::eLinear && (props.formatProperties.linearTilingFeatures & features) == features)
			{
				return format;
			}
			if (tiling == vk::ImageTiling::eOptimal && (props.formatProperties.optimalTilingFeatures & features) == features)
			{
				return format;
			}
		}

		throw std::runtime_error("Failed to find supported format!");
	}

	uint32_t VulkanContext::GetMaxFramesInFlight() const
	{
		return VulkanContext::MaxFramesInFlight;
	}

	void VulkanContext::WaitIdle() const
	{
		m_Device.waitIdle();
	}

	void VulkanContext::SetObjectDebugName(const uint64_t objectHandle, const vk::ObjectType objectType,
	                                       const std::string& name) const
	{
#ifdef KBR_DEBUG
		const vk::DebugUtilsObjectNameInfoEXT nameInfo{
			.objectType = objectType,
			.objectHandle = objectHandle,
			.pObjectName = name.c_str()
		};

		m_Device.setDebugUtilsObjectNameEXT(nameInfo);
#endif
	}

	MemoryBudgetInfo VulkanContext::GetMemoryBudgetInfo() const
	{
		return m_MemoryBudget.GetMemoryBudgetInfo();
	}

	vk::raii::Device& VulkanContext::GetDevice()
	{
		return m_Device;
	}

	vk::raii::PhysicalDevice& VulkanContext::GetPhysicalDevice()
	{
		return m_PhysicalDevice;
	}

	vk::PhysicalDeviceProperties2 VulkanContext::GetProperties() const
	{
		return m_PhysicalDevice.getProperties2();
	}

	vk::PhysicalDeviceMemoryProperties2 VulkanContext::GetMemoryProperties() const
	{
		return m_PhysicalDevice.getMemoryProperties2();
	}

	vk::FormatProperties2 VulkanContext::GetFormatProperties(const vk::Format format) const
	{
		return m_PhysicalDevice.getFormatProperties2(format);
	}

	vk::SampleCountFlagBits VulkanContext::GetMaxMSAASamples() const
	{
		return m_MaxMSAASamples;
	}

	float VulkanContext::GetMaxAnisotropy() const 
	{
		return m_PhysicalDevice.getProperties2().properties.limits.maxSamplerAnisotropy;
	}

	void VulkanContext::FramebufferResized(uint32_t width, uint32_t height)
	{
		m_FramebufferResized = true;
	}

	void VulkanContext::RecordCommandBuffer(const uint32_t imageIndex) const
	{
		m_CommandBuffers[m_FrameIndex].begin({});

		Renderer::RecordQueuedSceneRender(m_CommandBuffers[m_FrameIndex]);

		// Transition the swapchain image to COLOR_ATTACHMENT_OPTIMAL
		TransitionImageLayout(
			m_SwapChainImages[imageIndex],
			vk::ImageLayout::eUndefined,
			vk::ImageLayout::eColorAttachmentOptimal,
			{},
			vk::AccessFlagBits2::eColorAttachmentWrite,
			vk::PipelineStageFlagBits2::eColorAttachmentOutput,
			vk::PipelineStageFlagBits2::eColorAttachmentOutput,
			vk::ImageAspectFlagBits::eColor
		);

		// Transition the multisampled color image to COLOR_ATTACHMENT_OPTIMAL
		TransitionImageLayout(
			m_ColorImage,
			vk::ImageLayout::eUndefined,
			vk::ImageLayout::eColorAttachmentOptimal,
			vk::AccessFlagBits2::eColorAttachmentWrite,
			vk::AccessFlagBits2::eColorAttachmentWrite,
			vk::PipelineStageFlagBits2::eColorAttachmentOutput,
			vk::PipelineStageFlagBits2::eColorAttachmentOutput,
			vk::ImageAspectFlagBits::eColor);

		// Transition the depth image to DEPTH_STENCIL_ATTACHMENT_OPTIMAL
		TransitionImageLayout(
			m_DepthImage,
			vk::ImageLayout::eUndefined,
			vk::ImageLayout::eDepthAttachmentOptimal,
			vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
			vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
			vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
			vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
			vk::ImageAspectFlagBits::eDepth);

		constexpr vk::ClearValue clearColor = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);
		constexpr vk::ClearValue clearDepth = vk::ClearDepthStencilValue{ .depth = 1.0f, .stencil = 0 };

		// Multisampled color attachment with resolve attachment
		vk::RenderingAttachmentInfo attachmentInfo = {
			.imageView = m_ColorImageView,
			.imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
			.resolveMode = vk::ResolveModeFlagBits::eAverage,
			.resolveImageView = m_SwapChainImageViews[imageIndex],
			.resolveImageLayout = vk::ImageLayout::eColorAttachmentOptimal,
			.loadOp = vk::AttachmentLoadOp::eClear,
			.storeOp = vk::AttachmentStoreOp::eStore,
			.clearValue = clearColor
		};

		vk::RenderingAttachmentInfo depthAttachmentInfo = {
			.imageView = m_DepthImageView,
			.imageLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal,
			.loadOp = vk::AttachmentLoadOp::eClear,
			.storeOp = vk::AttachmentStoreOp::eDontCare,
			.clearValue = clearDepth
		};

		const vk::RenderingInfo renderingInfo = {
			.renderArea = {.offset = {.x = 0, .y = 0 }, .extent = m_SwapChainExtent },
			.layerCount = 1,
			.colorAttachmentCount = 1,
			.pColorAttachments = &attachmentInfo,
			.pDepthAttachment = &depthAttachmentInfo
		};

		m_CommandBuffers[m_FrameIndex].beginRendering(renderingInfo);

		//commandBuffers[frameIndex].bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);
		//commandBuffers[frameIndex].bindVertexBuffers(0, *vertexBuffer, { 0 });
		//commandBuffers[frameIndex].bindIndexBuffer(*indexBuffer, 0, vk::IndexType::eUint32);

		//commandBuffers[frameIndex].setViewport(0, vk::Viewport{ 0.0f, 0.0f, static_cast<float>(swapChainExtent.width), static_cast<float>(swapChainExtent.height), 0.0f, 1.0f });
		//commandBuffers[frameIndex].setScissor(0, vk::Rect2D{ vk::Offset2D(0, 0), swapChainExtent });

		//// Draw each object with its own descriptor set
		//for (const auto& gameObject : gameObjects) {
		//	// Bind the descriptor set for this object
		//	commandBuffers[frameIndex].bindDescriptorSets(
		//		vk::PipelineBindPoint::eGraphics,
		//		*pipelineLayout,
		//		0,
		//		*gameObject.descriptorSets[frameIndex],
		//		nullptr
		//	);

		//	// Draw the object
		//	commandBuffers[frameIndex].drawIndexed(static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);
		//}

		ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), *m_CommandBuffers[m_FrameIndex]);

		m_CommandBuffers[m_FrameIndex].endRendering();

		// After rendering, transition the swapchain image to PRESENT_SRC
		TransitionImageLayout(
			m_SwapChainImages[imageIndex],
			vk::ImageLayout::eColorAttachmentOptimal,
			vk::ImageLayout::ePresentSrcKHR,
			vk::AccessFlagBits2::eColorAttachmentWrite,
			{},
			vk::PipelineStageFlagBits2::eColorAttachmentOutput,
			vk::PipelineStageFlagBits2::eBottomOfPipe,
			vk::ImageAspectFlagBits::eColor
		);

		m_CommandBuffers[m_FrameIndex].end();
	}

	void VulkanContext::CreateInstance()
	{
		constexpr vk::ApplicationInfo appInfo{
			.pApplicationName = "Hello Triangle",
			.applicationVersion = VK_MAKE_VERSION(1, 0, 0),
			.pEngineName = "No Engine",
			.engineVersion = VK_MAKE_VERSION(1, 0, 0),
			.apiVersion = vk::ApiVersion14
		};

		std::vector<char const*> requiredLayers;
		if (enableValidationLayers) {
			requiredLayers.assign(validationLayers.begin(), validationLayers.end());
		}

		// Check if the required layers are supported by the Vulkan implementation.
		auto layerProperties = m_Context.enumerateInstanceLayerProperties();
		if (std::ranges::any_of(requiredLayers, [&layerProperties](auto const& requiredLayer) {
			return std::ranges::none_of(layerProperties,
			                            [requiredLayer](auto const& layerProperty)
			                            { return strcmp(layerProperty.layerName, requiredLayer) == 0; });
		}))
		{
			throw std::runtime_error("One or more required layers are not supported!");
		}

		const auto requiredExtensions = GetRequiredExtensions();

		// Check if the required extensions are supported by the Vulkan implementation.
		auto extensionProperties = m_Context.enumerateInstanceExtensionProperties();
		for (auto const& requiredExtension : requiredExtensions)
		{
			if (std::ranges::none_of(extensionProperties,
			                         [requiredExtension](auto const& extensionProperty)
			                         { return strcmp(extensionProperty.extensionName, requiredExtension) == 0; }))
			{
				throw std::runtime_error("Required extension not supported: " + std::string(requiredExtension));
			}
		}

		const vk::InstanceCreateInfo createInfo{
			.pApplicationInfo = &appInfo,
			.enabledLayerCount = static_cast<uint32_t>(requiredLayers.size()),
			.ppEnabledLayerNames = requiredLayers.data(),
			.enabledExtensionCount = static_cast<uint32_t>(requiredExtensions.size()),
			.ppEnabledExtensionNames = requiredExtensions.data()
		};

		m_Instance = vk::raii::Instance{ m_Context, createInfo };
	}

	void VulkanContext::SetupDebugMessenger() {
		if (!enableValidationLayers) 
			return;

		constexpr vk::DebugUtilsMessageSeverityFlagsEXT severityFlags(vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose | vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError);
		constexpr vk::DebugUtilsMessageTypeFlagsEXT    messageTypeFlags(vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation);
		constexpr vk::DebugUtilsMessengerCreateInfoEXT debugUtilsMessengerCreateInfoEXT{
			.messageSeverity = severityFlags,
			.messageType = messageTypeFlags,
			.pfnUserCallback = &DebugCallback
		};

		m_DebugMessenger = m_Instance.createDebugUtilsMessengerEXT(debugUtilsMessengerCreateInfoEXT);
	}

	void VulkanContext::CreateSurface()
	{
		VkSurfaceKHR surface;
		if (glfwCreateWindowSurface(static_cast<VkInstance>(*m_Instance), m_Window, nullptr, &surface) != VK_SUCCESS) {
			throw std::runtime_error("failed to create window surface!");
		}
		m_Surface = vk::raii::SurfaceKHR{ m_Instance, surface };
	}

	void VulkanContext::PickPhysicalDevice()
	{
		const auto devices = m_Instance.enumeratePhysicalDevices();
		if (devices.empty()) {
			throw std::runtime_error("Failed to find GPUs with Vulkan support!");
		}

		const auto devIter = std::ranges::find_if(devices,
		                                          [&](const vk::raii::PhysicalDevice& device) {
			                                          auto queueFamilies = device.getQueueFamilyProperties2() 
														  | std::views::transform([](const vk::QueueFamilyProperties2& qfp2) { return qfp2.queueFamilyProperties; });

			                                          bool isSuitable = device.getProperties2().properties.apiVersion >= VK_API_VERSION_1_3;
			                                          const auto qfpIter = std::ranges::find_if(queueFamilies,
				                                          [](vk::QueueFamilyProperties const& qfp)
				                                          {
					                                          return (qfp.queueFlags & vk::QueueFlagBits::eGraphics) != static_cast<vk::QueueFlags>(0);
				                                          });
			                                          isSuitable = isSuitable && (qfpIter != queueFamilies.end());
			                                          auto extensions = device.enumerateDeviceExtensionProperties();
			                                          bool found = true;
			                                          for (auto const& extension : deviceExtensions) {
				                                          auto extensionIter = std::ranges::find_if(extensions, [extension](auto const& ext) {return strcmp(ext.extensionName, extension) == 0; });
				                                          found = found && extensionIter != extensions.end();
			                                          }
			                                          isSuitable = isSuitable && found;
			                                          if (isSuitable) {
				                                          m_PhysicalDevice = device;
				                                          m_MaxMSAASamples = GetMaxUsableSampleCount(m_PhysicalDevice);

				                                          // Save name of the selected GPU
				                                          const auto deviceProperties = m_PhysicalDevice.getProperties2().properties;
				                                          m_PhysicalDeviceName = deviceProperties.deviceName.data();
				                                          KBR_CORE_INFO("Selected GPU: {}", m_PhysicalDeviceName);
			                                          }
			                                          return isSuitable;
		                                          });
		if (devIter == devices.end()) {
			throw std::runtime_error("failed to find a suitable GPU!");
		}
	}

	std::vector<const char*> VulkanContext::GetRequiredExtensions()
	{
		uint32_t glfwExtensionCount = 0;
		const auto glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

		std::vector extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

		if (enableValidationLayers) {
			extensions.push_back(vk::EXTDebugUtilsExtensionName);
		}

		return extensions;
	}

	static bool IsDeviceSuitable(const vk::raii::PhysicalDevice& physicalDevice)
	{
		const auto deviceProperties = physicalDevice.getProperties2().properties;
		const auto deviceFeatures = physicalDevice.getFeatures2().features;

		if (deviceProperties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu && deviceFeatures.geometryShader) {
			return true;
		}

		return false;
	}

 VulkanContext::QueueFamilyInfo VulkanContext::FindQueueFamilies(const vk::raii::PhysicalDevice& physicalDevice, const vk::raii::SurfaceKHR& surface)
	{
		QueueFamilyInfo queueFamilyInfo{};

		auto queueFamilyProperties = physicalDevice.getQueueFamilyProperties2()
			| std::views::transform([](const vk::QueueFamilyProperties2& qfp2) { return qfp2.queueFamilyProperties; });
		
		std::optional<uint32_t> presentCandidate;
		std::optional<uint32_t> dedicatedComputeCandidate;
		std::optional<uint32_t> computeCandidate;
		std::optional<uint32_t> dedicatedTransferCandidate;
		std::optional<uint32_t> transferNoGraphicsCandidate;
		std::optional<uint32_t> transferCandidate;

		for (uint32_t i = 0; i < queueFamilyProperties.size(); ++i)
		{
			const auto& queueFamilyProperty = queueFamilyProperties[i];
			const bool supportsGraphics = (queueFamilyProperty.queueFlags & vk::QueueFlagBits::eGraphics) != static_cast<vk::QueueFlags>(0);
			const bool supportsCompute = (queueFamilyProperty.queueFlags & vk::QueueFlagBits::eCompute) != static_cast<vk::QueueFlags>(0);
			const bool supportsTransfer = (queueFamilyProperty.queueFlags & vk::QueueFlagBits::eTransfer) != static_cast<vk::QueueFlags>(0);

           if (queueFamilyInfo.graphics == (std::numeric_limits<uint32_t>::max)() && supportsGraphics)
			{
				queueFamilyInfo.graphics = i;
			}

			if (!presentCandidate.has_value() && physicalDevice.getSurfaceSupportKHR(i, *surface))
			{
				presentCandidate = i;
			}

			if (supportsCompute)
			{
				if (!computeCandidate.has_value())
				{
					computeCandidate = i;
				}

				if (!supportsGraphics && !dedicatedComputeCandidate.has_value())
				{
					dedicatedComputeCandidate = i;
				}
			}

			if (supportsTransfer)
			{
				if (!transferCandidate.has_value())
				{
					transferCandidate = i;
				}

				if (!supportsGraphics && !transferNoGraphicsCandidate.has_value())
				{
					transferNoGraphicsCandidate = i;
				}

				if (!supportsGraphics && !supportsCompute && !dedicatedTransferCandidate.has_value())
				{
					dedicatedTransferCandidate = i;
				}
			}
		}

       if (queueFamilyInfo.graphics == std::numeric_limits<uint32_t>::max())
		{
			throw std::runtime_error("Could not find a graphics queue family -> terminating");
		}

		if (physicalDevice.getSurfaceSupportKHR(queueFamilyInfo.graphics, *surface))
		{
			queueFamilyInfo.present = queueFamilyInfo.graphics;
		}
		else if (presentCandidate.has_value())
		{
			queueFamilyInfo.present = presentCandidate.value();
		}
		else
		{
			throw std::runtime_error("Could not find a present queue family -> terminating");
		}

		if (dedicatedComputeCandidate.has_value())
		{
			queueFamilyInfo.compute = dedicatedComputeCandidate;
		}
		else if (computeCandidate.has_value())
		{
			queueFamilyInfo.compute = computeCandidate;
		}

		if (dedicatedTransferCandidate.has_value())
		{
			queueFamilyInfo.transfer = dedicatedTransferCandidate;
		}
		else if (transferNoGraphicsCandidate.has_value())
		{
			queueFamilyInfo.transfer = transferNoGraphicsCandidate;
		}
		else if (transferCandidate.has_value())
		{
			queueFamilyInfo.transfer = transferCandidate;
		}

		return queueFamilyInfo;
	}

	 void VulkanContext::CreateLogicalDevice()
	 {
		 m_QueueFamilyInfo = FindQueueFamilies(m_PhysicalDevice, m_Surface);
	
		 constexpr float queuePriority = 0.5f;
		 std::vector<uint32_t> uniqueQueueFamilies = { m_QueueFamilyInfo.graphics, m_QueueFamilyInfo.present };
	
		 if (m_QueueFamilyInfo.compute.has_value())
		 {
			 uniqueQueueFamilies.push_back(m_QueueFamilyInfo.compute.value());
		 }
		 if (m_QueueFamilyInfo.transfer.has_value())
		 {
			 uniqueQueueFamilies.push_back(m_QueueFamilyInfo.transfer.value());
		 }
	
		 std::ranges::sort(uniqueQueueFamilies);
		 uniqueQueueFamilies.erase(std::ranges::unique(uniqueQueueFamilies).begin(), uniqueQueueFamilies.end());
	
		 std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;
		 queueCreateInfos.reserve(uniqueQueueFamilies.size());
		 for (const uint32_t queueFamilyIndex : uniqueQueueFamilies)
		 {
			 queueCreateInfos.emplace_back(vk::DeviceQueueCreateInfo{
				 .queueFamilyIndex = queueFamilyIndex,
				 .queueCount = 1,
				 .pQueuePriorities = &queuePriority
			 });
		 }
	
		 // Create a chain of feature structures
		 vk::StructureChain<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features, vk::PhysicalDeviceVulkan12Features, vk::PhysicalDeviceVulkan13Features, vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT, vk::PhysicalDeviceAccelerationStructureFeaturesKHR, vk::PhysicalDeviceRayQueryFeaturesKHR> featureChain = {
			 {.features = {
				 .independentBlend = true,
				 .geometryShader = true, .depthClamp = true, .depthBiasClamp = true, .samplerAnisotropy = true,
				 .shaderInt64 = true
				},
			 },
			 {.shaderDrawParameters = true },
			 {.descriptorIndexing = true, .descriptorBindingSampledImageUpdateAfterBind = true, .descriptorBindingPartiallyBound = true, .timelineSemaphore = true, 
				.bufferDeviceAddress = true, .shaderOutputLayer = true },
			 {.synchronization2 = true, .dynamicRendering = true },
			 {.extendedDynamicState = true },
			 {.accelerationStructure = true },
			 {.rayQuery = true }
		 };
	
		 const vk::DeviceCreateInfo deviceCreateInfo{
			 .pNext = &featureChain.get<vk::PhysicalDeviceFeatures2>(),
			 .queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size()),
			 .pQueueCreateInfos = queueCreateInfos.data(),
			 .enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size()),
			 .ppEnabledExtensionNames = deviceExtensions.data()
		 };
	
		 m_Device = vk::raii::Device{ m_PhysicalDevice, deviceCreateInfo };
	
		 m_GraphicsQueue = m_Device.getQueue(m_QueueFamilyInfo.graphics, 0);
		 m_PresentQueue = m_Device.getQueue(m_QueueFamilyInfo.present, 0);
		 m_ComputeQueue = m_QueueFamilyInfo.compute.has_value()
			 ? m_Device.getQueue(m_QueueFamilyInfo.compute.value(), 0)
			 : m_GraphicsQueue;
		 m_TransferQueue = m_QueueFamilyInfo.transfer.has_value()
			 ? m_Device.getQueue(m_QueueFamilyInfo.transfer.value(), 0)
			 : m_GraphicsQueue;
	
		 KBR_CORE_INFO(
			 "Queue families selected \n\tgraphics: {}, \n\tpresent: {}, \n\tcompute: {}, \n\ttransfer: {}, \n\tseparateCompute: {}, \n\tseparateTransfer: {}, \n\tdedicatedTransfer: {}",
			 m_QueueFamilyInfo.graphics,
			 m_QueueFamilyInfo.present,
			 m_QueueFamilyInfo.compute.has_value() ? std::to_string(m_QueueFamilyInfo.compute.value()) : std::string("None"),
			 m_QueueFamilyInfo.transfer.has_value() ? std::to_string(m_QueueFamilyInfo.transfer.value()) : std::string("None"),
			 m_QueueFamilyInfo.HasSeparateComputeQueue(),
			 m_QueueFamilyInfo.HasSeparateTransferQueue(),
			 m_QueueFamilyInfo.HasDedicatedTransferQueue());
	
		 SetObjectDebugName(m_GraphicsQueue, "Graphics Queue");
		 if (m_QueueFamilyInfo.present != m_QueueFamilyInfo.graphics)
		 {
			 SetObjectDebugName(m_PresentQueue, "Present Queue");
		 }
		 if (m_QueueFamilyInfo.HasSeparateComputeQueue())
		 {
			 SetObjectDebugName(m_ComputeQueue, "Compute Queue");
		 }
		 if (m_QueueFamilyInfo.HasSeparateTransferQueue())
		 {
			 SetObjectDebugName(m_TransferQueue, "Transfer Queue");
		 }
	 }

	void VulkanContext::CreateAllocator()
	{
		KBR_CORE_ASSERT(m_Instance != nullptr, "VkInstance has to be initialized to create allocator!");
		KBR_CORE_ASSERT(m_PhysicalDevice != nullptr, "VkPhysicalDevice has to be initialized to create allocator!");
		KBR_CORE_ASSERT(m_Device != nullptr, "VkDevice has to be initialized to create allocator!");

		m_Allocator = VMA::CreateAllocator(m_Instance, m_PhysicalDevice, m_Device);
	}

	static vk::SurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& availableFormats)
	{
		for (const auto& availableFormat : availableFormats) {
			// This way the GPU applies gamma correction to ImGui windows, which makes them look greyish.
			// Figure out a way to have correct colors in ImGui windows as well.
			/*if (availableFormat.format == vk::Format::eB8G8R8A8Srgb && availableFormat.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
				return availableFormat;
			}*/
			if (availableFormat.format == vk::Format::eB8G8R8A8Unorm && availableFormat.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
				return availableFormat;
			}
		}

		return availableFormats[0];
	}

	static vk::PresentModeKHR ChooseSwapPresentMode(const std::vector<vk::PresentModeKHR>& availablePresentModes)
	{
		for (const auto& availablePresentMode : availablePresentModes) {
			if (availablePresentMode == vk::PresentModeKHR::eMailbox) {
				return availablePresentMode;
			}
		}

		// FIFO is guaranteed to be available
		return vk::PresentModeKHR::eFifo;
	}

	vk::Extent2D VulkanContext::ChooseSwapExtent(const vk::SurfaceCapabilitiesKHR& capabilities) const
	{
		if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
			return capabilities.currentExtent;
		}

		int width, height;
		glfwGetFramebufferSize(m_Window, &width, &height);

		return {
			.width = std::clamp(static_cast<uint32_t>(width), capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
			.height = std::clamp(static_cast<uint32_t>(height), capabilities.minImageExtent.height, capabilities.maxImageExtent.height)
		};
	}

	void VulkanContext::CreateSwapChain()
	{
		const auto surfaceCapabilities = m_PhysicalDevice.getSurfaceCapabilitiesKHR(m_Surface);
		const auto [format, colorSpace] = ChooseSwapSurfaceFormat(m_PhysicalDevice.getSurfaceFormatsKHR(m_Surface));
		const auto extent = ChooseSwapExtent(surfaceCapabilities);

		auto minImageCount = std::max(3u, surfaceCapabilities.minImageCount);
		minImageCount = (surfaceCapabilities.maxImageCount > 0 && minImageCount > surfaceCapabilities.maxImageCount)
			? surfaceCapabilities.maxImageCount
			: minImageCount;

		uint32_t imageCount = surfaceCapabilities.minImageCount + 1;
		if (surfaceCapabilities.maxImageCount > 0 && imageCount > surfaceCapabilities.maxImageCount) {
			imageCount = surfaceCapabilities.maxImageCount;
		}

		const vk::PresentModeKHR presentMode = ChooseSwapPresentMode(m_PhysicalDevice.getSurfacePresentModesKHR(m_Surface));

		const vk::SwapchainCreateInfoKHR swapChainCreateInfo{
			.flags = vk::SwapchainCreateFlagsKHR(),
			.surface = m_Surface,
			.minImageCount = minImageCount,
			.imageFormat = format,
			.imageColorSpace = colorSpace,
			.imageExtent = extent,
			.imageArrayLayers = 1,
			.imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
			.imageSharingMode = vk::SharingMode::eExclusive,
			.preTransform = surfaceCapabilities.currentTransform,
			.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque,
			.presentMode = presentMode,
			.clipped = true,
			.oldSwapchain = nullptr };

		m_SwapChain = vk::raii::SwapchainKHR(m_Device, swapChainCreateInfo);
		m_SwapChainImageFormat = format;
		m_SwapChainExtent = extent;

		m_SwapChainImages = m_SwapChain.getImages();
	}

	void VulkanContext::CreateSwapChainImageViews()
	{
		m_SwapChainImageViews.clear();

		vk::ImageViewCreateInfo imageViewCreateInfo{
			.viewType = vk::ImageViewType::e2D,
			.format = m_SwapChainImageFormat,
			.subresourceRange = {
				.aspectMask = vk::ImageAspectFlagBits::eColor,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1
			}
		};

		for (const auto image : m_SwapChainImages) {
			imageViewCreateInfo.image = image;
			m_SwapChainImageViews.emplace_back(m_Device, imageViewCreateInfo);
		}
	}

	void VulkanContext::CreateCommandPools()
	{
		const vk::CommandPoolCreateInfo graphicsPoolInfo{
			.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
		    .queueFamilyIndex = m_QueueFamilyInfo.graphics
		};
		m_GraphicsCommandPool = vk::raii::CommandPool(m_Device, graphicsPoolInfo);

		if (m_QueueFamilyInfo.HasSeparateComputeQueue())
		{
			const vk::CommandPoolCreateInfo computePoolInfo{
				.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
				.queueFamilyIndex = m_QueueFamilyInfo.compute.value()
			};
			m_ComputeCommandPool = vk::raii::CommandPool(m_Device, computePoolInfo);
		}

		if (m_QueueFamilyInfo.HasSeparateTransferQueue())
		{
			const vk::CommandPoolCreateInfo transferPoolInfo{
				.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
				.queueFamilyIndex = m_QueueFamilyInfo.transfer.value()
			};

			m_TransferCommandPool = vk::raii::CommandPool(m_Device, transferPoolInfo);
		}
	}

	void VulkanContext::CreateCommandBuffers()
	{
		const vk::CommandBufferAllocateInfo allocInfo{
			.commandPool = m_GraphicsCommandPool,
			.level = vk::CommandBufferLevel::ePrimary,
			.commandBufferCount = VulkanContext::MaxFramesInFlight
		};

		m_CommandBuffers = vk::raii::CommandBuffers(m_Device, allocInfo);
	}

	void VulkanContext::CreateSyncObjects()
	{
		KBR_CORE_ASSERT(m_PresentCompleteSemaphores.empty() && m_RenderFinishedSemaphores.empty() && m_InFlightFences.empty(), "Sync objects are not empty!");

		for (size_t i = 0; i < m_SwapChainImages.size(); i++)
		{
			m_RenderFinishedSemaphores.emplace_back(m_Device, vk::SemaphoreCreateInfo());
		}

		for (size_t i = 0; i < VulkanContext::MaxFramesInFlight; i++)
		{
			m_PresentCompleteSemaphores.emplace_back(m_Device, vk::SemaphoreCreateInfo());
			m_InFlightFences.emplace_back(m_Device, vk::FenceCreateInfo{ .flags = vk::FenceCreateFlagBits::eSignaled });
		}
	}

	vk::Format VulkanContext::FindDepthFormat() const
	{
		return FindSupportedFormat(
			{ vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint },
			vk::ImageTiling::eOptimal,
			vk::FormatFeatureFlagBits::eDepthStencilAttachment
		);
	}

	bool VulkanContext::HasStencilComponent(const vk::Format format)
	{
		return format == vk::Format::eD32SfloatS8Uint || format == vk::Format::eD24UnormS8Uint;
	}

	void VulkanContext::CreateColorResources()
	{
		const vk::Format colorFormat = m_SwapChainImageFormat;

		CreateImage(m_Device,
					m_SwapChainExtent.width,
					m_SwapChainExtent.height,
					1,
					m_MaxMSAASamples,
					colorFormat,
					vk::ImageTiling::eOptimal,
					vk::ImageUsageFlagBits::eTransientAttachment | vk::ImageUsageFlagBits::eColorAttachment,
					vk::MemoryPropertyFlagBits::eDeviceLocal,
					m_ColorImage,
					m_ColorImageMemory);

		m_ColorImageView = CreateImageView(m_Device, m_ColorImage, colorFormat, vk::ImageAspectFlagBits::eColor, 1);
	}

	void VulkanContext::CreateDepthResources()
	{
		m_DepthFormat = FindDepthFormat();

		CreateImage(m_Device,
					m_SwapChainExtent.width,
					m_SwapChainExtent.height,
					1,
					m_MaxMSAASamples,
					m_DepthFormat,
					vk::ImageTiling::eOptimal,
					vk::ImageUsageFlagBits::eDepthStencilAttachment,
					vk::MemoryPropertyFlagBits::eDeviceLocal,
					m_DepthImage,
					m_DepthImageMemory);

		m_DepthImageView = CreateImageView(m_Device, m_DepthImage, m_DepthFormat, vk::ImageAspectFlagBits::eDepth, 1);
	}

	void VulkanContext::CleanupSwapChain()
	{
		m_SwapChainImageViews.clear();
		m_SwapChain = nullptr;
	}

	void VulkanContext::RecreateSwapchain()
	{
		int width = 0, height = 0;
		glfwGetFramebufferSize(m_Window, &width, &height);
		while (width == 0 || height == 0) {
			glfwGetFramebufferSize(m_Window, &width, &height);
			glfwWaitEvents();
		}

		m_Device.waitIdle();

		CleanupSwapChain();

		CreateSwapChain();
		CreateSwapChainImageViews();
		CreateColorResources();
		CreateDepthResources();
	}

	void VulkanContext::TransitionImageLayout(
		const vk::Image image,
		const vk::ImageLayout oldLayout,
		const vk::ImageLayout newLayout,
		const vk::AccessFlags2 srcAccessMask,
		const vk::AccessFlags2 dstAccessMask,
		const vk::PipelineStageFlags2 srcStageMask,
		const vk::PipelineStageFlags2 dstStageMask,
		const vk::ImageAspectFlagBits aspectFlags
	) const {
		vk::ImageMemoryBarrier2 barrier = {
			.srcStageMask = srcStageMask,
			.srcAccessMask = srcAccessMask,
			.dstStageMask = dstStageMask,
			.dstAccessMask = dstAccessMask,
			.oldLayout = oldLayout,
			.newLayout = newLayout,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = image,
			.subresourceRange = {
				.aspectMask = aspectFlags,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1
			}
		};

		const vk::DependencyInfo dependencyInfo = {
			.dependencyFlags = {},
			.imageMemoryBarrierCount = 1,
			.pImageMemoryBarriers = &barrier
		};

		m_CommandBuffers[m_FrameIndex].pipelineBarrier2(dependencyInfo);
	}

	vk::DescriptorSet VulkanContext::GenerateImGuiDescriptorSet(const vk::raii::Sampler& sampler,
																const vk::raii::ImageView& imageView, vk::ImageLayout imageLayout)
	{
		return ImGui_ImplVulkan_AddTexture(
			static_cast<VkSampler>(*sampler),
			static_cast<VkImageView>(*imageView),
			static_cast<VkImageLayout>(imageLayout)
		);
	}

	void VulkanContext::DestroyImGuiDescriptorSet(const vk::DescriptorSet& descriptorSet) {
		ImGui_ImplVulkan_RemoveTexture(descriptorSet);
	}

	uint64_t VulkanContext::GetImGuiRendererID(const Ref<Texture2D>& texture)
	{
		const vk::DescriptorSet descriptorSet = m_ImGuiDescriptorSetManager.GetDescriptorSetForTexture(texture);
		const uint64_t rendererID = reinterpret_cast<ImTextureID>(static_cast<VkDescriptorSet>(descriptorSet));
		return rendererID;
	}

	void VulkanContext::Cleanup() const
	{
		m_Device.waitIdle();

		ImGui_ImplVulkan_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();
	}

	void VulkanContext::CreateImGuiDescriptorPool()
	{
		constexpr std::array poolSizes = {
			vk::DescriptorPoolSize{
				.type = vk::DescriptorType::eSampler,
				.descriptorCount = 1000
			},
			vk::DescriptorPoolSize{
				.type = vk::DescriptorType::eCombinedImageSampler,
				.descriptorCount = 1000
			},
			vk::DescriptorPoolSize{
				.type = vk::DescriptorType::eSampledImage,
				.descriptorCount = 1000
			},
			vk::DescriptorPoolSize{
				.type = vk::DescriptorType::eStorageImage,
				.descriptorCount = 1000
			},
			vk::DescriptorPoolSize{
				.type = vk::DescriptorType::eUniformTexelBuffer,
				.descriptorCount = 1000
			},
			vk::DescriptorPoolSize{
				.type = vk::DescriptorType::eStorageTexelBuffer,
				.descriptorCount = 1000
			},
			vk::DescriptorPoolSize{
				.type = vk::DescriptorType::eUniformBuffer,
				.descriptorCount = 1000
			},
			vk::DescriptorPoolSize{
				.type = vk::DescriptorType::eStorageBuffer,
				.descriptorCount = 1000
			},
			vk::DescriptorPoolSize{
				.type = vk::DescriptorType::eUniformBufferDynamic,
				.descriptorCount = 1000
			},
			vk::DescriptorPoolSize{
				.type = vk::DescriptorType::eStorageBufferDynamic,
				.descriptorCount = 1000
			},
			vk::DescriptorPoolSize{
				.type = vk::DescriptorType::eInputAttachment,
				.descriptorCount = 1000
			}
		};
		const vk::DescriptorPoolCreateInfo poolInfo{
			.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
			.maxSets = 1000 * static_cast<uint32_t>(poolSizes.size()),
			.poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
			.pPoolSizes = poolSizes.data()
		};
		m_ImGuiDescriptorPool = vk::raii::DescriptorPool{ m_Device, poolInfo };
	}

	static void CheckVkResult(const VkResult err)
	{
		if (err == VK_SUCCESS)
			return;
		std::ignore = fprintf(stderr, "[vulkan] Error: VkResult = %d\n", err);
		if (err < 0)
			abort();
	}

	void VulkanContext::SetupImGui()
	{
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO(); (void)io;
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // Enable Docking
		io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;       // Enable Multi-Viewport / Platform Windows

		ImGui::StyleColorsDark();

		const float mainScale = ImGui_ImplGlfw_GetContentScaleForMonitor(glfwGetPrimaryMonitor());

		ImGuiStyle& style = ImGui::GetStyle();
		style.ScaleAllSizes(mainScale);        // Bake a fixed style scale. (until we have a solution for dynamic style scaling, changing this requires resetting Style + calling this again)
		style.FontScaleDpi = mainScale;        // Set initial font scale. (using io.ConfigDpiScaleFonts=true makes this unnecessary. We leave both here for documentation purpose)
		io.ConfigDpiScaleFonts = true;          // [Experimental] Automatically overwrite style.FontScaleDpi in Begin() when Monitor DPI changes. This will scale fonts but _NOT_ scale sizes/padding for now.
		io.ConfigDpiScaleViewports = true;      // [Experimental] Scale Dear ImGui and Platform Windows when Monitor DPI changes.

		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			style.WindowRounding = 0.0f;
			style.Colors[ImGuiCol_WindowBg].w = 1.0f;
		}

		// Rounding
		style.WindowRounding = 5.0f; // Soften window corners
		style.FrameRounding = 4.0f; // Soften text boxes, buttons, etc.
		style.ScrollbarRounding = 12.0f;
		style.GrabRounding = 4.0f; // Sliders

		// Padding and Spacing
		style.WindowPadding = ImVec2(10.0f, 10.0f);
		style.FramePadding = ImVec2(8.0f, 6.0f); // Taller, wider buttons/inputs
		style.ItemSpacing = ImVec2(8.0f, 8.0f);
		style.ItemInnerSpacing = ImVec2(6.0f, 6.0f);

		// Borders
		style.WindowBorderSize = 0.0f; // Remove thick window borders
		style.FrameBorderSize = 0.0f;

		ImVec4* colors = style.Colors;
		colors[ImGuiCol_WindowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.00f);
		colors[ImGuiCol_Button] = ImVec4(0.10f, 0.125f, 0.15f, 1.00f);
		colors[ImGuiCol_ButtonHovered] = ImVec4(0.30f, 0.35f, 0.40f, 1.00f);
		colors[ImGuiCol_ButtonActive] = ImVec4(0.40f, 0.45f, 0.50f, 1.00f);
		colors[ImGuiCol_FrameBg] = ImVec4(0.15f, 0.18f, 0.20f, 1.00f);

		ImGui_ImplGlfw_InitForVulkan(m_Window, true);

		const VkFormat colorFormatVk = static_cast<VkFormat>(m_SwapChainImageFormat);
		const VkFormat depthFormatVk = static_cast<VkFormat>(m_DepthFormat);

		const VkPipelineRenderingCreateInfoKHR pipelineRenderingCreateInfo = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
			.pNext = nullptr,
			.viewMask = 0,
			.colorAttachmentCount = 1,
			.pColorAttachmentFormats = &colorFormatVk,
			.depthAttachmentFormat = depthFormatVk,
			.stencilAttachmentFormat = VK_FORMAT_UNDEFINED,
		};

		const VkSampleCountFlagBits msaaSamplesVk = static_cast<VkSampleCountFlagBits>(m_MaxMSAASamples);
		constexpr VkSampleCountFlagBits msaaSamplesViewportVk = VK_SAMPLE_COUNT_1_BIT;

		ImGui_ImplVulkan_InitInfo initInfo = {};
		ZeroMemory(&initInfo, sizeof(initInfo));
		initInfo.Instance = *m_Instance;
		initInfo.PhysicalDevice = *m_PhysicalDevice;
		initInfo.Device = *m_Device;
		initInfo.QueueFamily = m_QueueFamilyInfo.graphics;
		initInfo.Queue = *m_GraphicsQueue;
		initInfo.PipelineCache = nullptr;
		initInfo.DescriptorPool = *m_ImGuiDescriptorPool;
		initInfo.MinImageCount = static_cast<uint32_t>(m_SwapChainImages.size());
		initInfo.ImageCount = static_cast<uint32_t>(m_SwapChainImages.size());
		initInfo.Allocator = nullptr;
		initInfo.UseDynamicRendering = true;
		initInfo.PipelineInfoMain.PipelineRenderingCreateInfo = pipelineRenderingCreateInfo;
		initInfo.PipelineInfoMain.MSAASamples = msaaSamplesVk;
		initInfo.PipelineInfoForViewports.PipelineRenderingCreateInfo = pipelineRenderingCreateInfo;
		initInfo.PipelineInfoForViewports.MSAASamples = msaaSamplesViewportVk;
		initInfo.CheckVkResultFn = CheckVkResult;
		ImGui_ImplVulkan_Init(&initInfo);

		ImFontConfig fontConfig;
		fontConfig.OversampleH = 2;
		fontConfig.OversampleV = 2;

		static constexpr float baseFontSize = 14.0f;
		const float scaledFontSize = baseFontSize * mainScale;

		io.Fonts->AddFontFromFileTTF("Assets/Fonts/Inter/Inter_18pt-Italic.ttf", scaledFontSize, &fontConfig);
		io.Fonts->AddFontFromFileTTF("Assets/Fonts/Inter/Inter_18pt-Bold.ttf", scaledFontSize, &fontConfig);
		io.Fonts->AddFontFromFileTTF("Assets/Fonts/Inter/Inter_18pt-SemiBold.ttf", scaledFontSize, &fontConfig);
		io.Fonts->AddFontFromFileTTF("Assets/Fonts/Inter/Inter_18pt-Regular.ttf", scaledFontSize, &fontConfig);
		ImFont* interRegular = io.Fonts->AddFontFromFileTTF("Assets/Fonts/Inter/Inter_18pt-Regular.ttf", baseFontSize, &fontConfig);

		KBR_CORE_ASSERT(interRegular != nullptr, "Failed to load font!");
		io.FontDefault = interRegular;
	}

	vk::SampleCountFlagBits VulkanContext::GetMaxUsableSampleCount(const vk::raii::PhysicalDevice& physicalDevice)
	{
		const vk::PhysicalDeviceProperties physicalDeviceProperties = physicalDevice.getProperties2().properties;

		const vk::SampleCountFlags counts = physicalDeviceProperties.limits.framebufferColorSampleCounts & physicalDeviceProperties.limits.framebufferDepthSampleCounts;
		if (counts & vk::SampleCountFlagBits::e64) { return vk::SampleCountFlagBits::e64; }
		if (counts & vk::SampleCountFlagBits::e32) { return vk::SampleCountFlagBits::e32; }
		if (counts & vk::SampleCountFlagBits::e16) { return vk::SampleCountFlagBits::e16; }
		if (counts & vk::SampleCountFlagBits::e8) { return vk::SampleCountFlagBits::e8; }
		if (counts & vk::SampleCountFlagBits::e4) { return vk::SampleCountFlagBits::e4; }
		if (counts & vk::SampleCountFlagBits::e2) { return vk::SampleCountFlagBits::e2; }

		return vk::SampleCountFlagBits::e1;
	}
}