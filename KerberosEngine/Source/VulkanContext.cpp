#include "kbrpch.hpp"
#include "VulkanContext.hpp"

#include "Renderer/Textures/Texture.hpp"
#include <iostream>

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"

#include "Utils.hpp"
#include "logging/Log.hpp"

#define VK_USE_PLATFORM_WIN32_KHR
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>


constexpr uint32_t maxFramesInFlight = 2;

const std::vector<char const*> validationLayers = {
	"VK_LAYER_KHRONOS_validation"
};

const std::vector<const char*> deviceExtensions = {
	vk::KHRSwapchainExtensionName,
	vk::KHRSpirv14ExtensionName,
	vk::KHRSynchronization2ExtensionName,
	vk::KHRCreateRenderpass2ExtensionName,
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

		CreateCommandPool();
		CreateCommandBuffers();
		CreateSyncObjects();

		CreateImGuiDescriptorPool();
		SetupImGui();
	}

	VulkanContext::~VulkanContext()
	{
		Cleanup();
	};

	void VulkanContext::PrepareImGuiFrame() 
	{
		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

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
		RecordCommandBuffer(m_CurrentImageIndex);

		//UpdateUniformBuffers(frameIndex);

		//vk::PipelineStageFlags waitDestinationStageMask(vk::PipelineStageFlagBits::eTopOfPipe);
		vk::PipelineStageFlags waitDestinationStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput);
		const vk::SubmitInfo submitInfo{
			.waitSemaphoreCount = 1,
			.pWaitSemaphores = &*m_PresentCompleteSemaphores[m_FrameIndex],
			.pWaitDstStageMask = &waitDestinationStageMask,
			.commandBufferCount = 1,
			.pCommandBuffers = &*m_CommandBuffers[m_FrameIndex],
			.signalSemaphoreCount = 1,
			.pSignalSemaphores = &*m_RenderFinishedSemaphores[imageIndex] };

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

		const auto result = m_PresentQueue.presentKHR(presentInfoKHR);
		if (result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eSuboptimalKHR || m_FramebufferResized) {
			m_FramebufferResized = false;
			RecreateSwapchain();
		}
		else if (result != vk::Result::eSuccess) {
			throw std::runtime_error("failed to present swap chain image!");
		}

		m_FrameIndex = (m_FrameIndex + 1) % maxFramesInFlight;
	}

	vk::raii::CommandBuffer VulkanContext::BeginSingleTimeCommands() const
	{
		const vk::CommandBufferAllocateInfo allocInfo{
			.commandPool = m_CommandPool,
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

	void VulkanContext::EndSingleTimeCommands(const vk::raii::CommandBuffer& commandBuffer) const
	{
		commandBuffer.end();
		const vk::SubmitInfo submitInfo{
			.commandBufferCount = 1,
			.pCommandBuffers = &*commandBuffer
		};
		m_GraphicsQueue.submit(submitInfo, nullptr);
		m_GraphicsQueue.waitIdle();
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
			.commandPool = *m_CommandPool,
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
		m_GraphicsQueue.submit(submitInfo, nullptr);
		m_GraphicsQueue.waitIdle();
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
			const vk::FormatProperties props = GetFormatProperties(format);

			if (tiling == vk::ImageTiling::eLinear && (props.linearTilingFeatures & features) == features)
			{
				return format;
			}
			if (tiling == vk::ImageTiling::eOptimal && (props.optimalTilingFeatures & features) == features)
			{
				return format;
			}
		}

		throw std::runtime_error("Failed to find supported format!");
	}

	uint32_t VulkanContext::GetMaxFramesInFlight() const 
	{
		return maxFramesInFlight;
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

	vk::PhysicalDeviceProperties VulkanContext::GetProperties() const 
	{
		return m_PhysicalDevice.getProperties();
	}

	vk::PhysicalDeviceMemoryProperties VulkanContext::GetMemoryProperties() const 
	{
		return m_PhysicalDevice.getMemoryProperties();
	}

	vk::FormatProperties VulkanContext::GetFormatProperties(const vk::Format format) const 
	{
		return m_PhysicalDevice.getFormatProperties(format);
	}

	vk::SampleCountFlagBits VulkanContext::GetMSAASamples() const 
	{
		return m_MSAASamples;
	}

	void VulkanContext::FramebufferResized(uint32_t width, uint32_t height) 
	{
		m_FramebufferResized = true;
	}

	void VulkanContext::RecordCommandBuffer(const uint32_t imageIndex) const 
	{
		m_CommandBuffers[m_FrameIndex].begin({});

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
		if (!enableValidationLayers) return;

		constexpr vk::DebugUtilsMessageSeverityFlagsEXT severityFlags(vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose | vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError);
		constexpr vk::DebugUtilsMessageTypeFlagsEXT    messageTypeFlags(vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation);
		constexpr vk::DebugUtilsMessengerCreateInfoEXT debugUtilsMessengerCreateInfoEXT{
			.messageSeverity = severityFlags,
			.messageType = messageTypeFlags,
			.pfnUserCallback = &DebugCallback
		};

		m_DebugMessenger = m_Instance.createDebugUtilsMessengerEXT(debugUtilsMessengerCreateInfoEXT);
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

	void VulkanContext::CreateSurface()
	{
		VkSurfaceKHR _surface;
		if (glfwCreateWindowSurface(static_cast<VkInstance>(*m_Instance), m_Window, nullptr, &_surface) != VK_SUCCESS) {
			throw std::runtime_error("failed to create window surface!");
		}
		m_Surface = vk::raii::SurfaceKHR{ m_Instance, _surface };
	}

	void VulkanContext::PickPhysicalDevice()
	{
		const auto devices = m_Instance.enumeratePhysicalDevices();
		if (devices.empty()) {
			throw std::runtime_error("Failed to find GPUs with Vulkan support!");
		}

		const auto devIter = std::ranges::find_if(devices,
												  [&](auto const& device) {
			auto queueFamilies = device.getQueueFamilyProperties();
			bool isSuitable = device.getProperties().apiVersion >= VK_API_VERSION_1_3;
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
				m_MSAASamples = GetMaxUsableSampleCount(m_PhysicalDevice);
			}
			return isSuitable;
		});
		if (devIter == devices.end()) {
			throw std::runtime_error("failed to find a suitable GPU!");
		}
	}

	static bool IsDeviceSuitable(const vk::raii::PhysicalDevice& physicalDevice)
	{
		const auto deviceProperties = physicalDevice.getProperties();
		const auto deviceFeatures = physicalDevice.getFeatures();

		if (deviceProperties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu && deviceFeatures.geometryShader) {
			return true;
		}

		return false;
	}

	uint32_t VulkanContext::FindQueueFamilies(const vk::raii::PhysicalDevice& physicalDevice) {
		// find the index of the first queue family that supports graphics
		std::vector<vk::QueueFamilyProperties> queueFamilyProperties = physicalDevice.getQueueFamilyProperties();

		// get the first index into queueFamilyProperties which supports graphics
		const auto graphicsQueueFamilyProperty =
			std::find_if(queueFamilyProperties.begin(),
						 queueFamilyProperties.end(),
						 [](vk::QueueFamilyProperties const& qfp) { return qfp.queueFlags & vk::QueueFlagBits::eGraphics; });

		return static_cast<uint32_t>(std::distance(queueFamilyProperties.begin(), graphicsQueueFamilyProperty));
	}

	void VulkanContext::CreateLogicalDevice()
	{
		std::vector<vk::QueueFamilyProperties> queueFamilyProperties = m_PhysicalDevice.getQueueFamilyProperties();
		uint32_t graphicsIndex = FindQueueFamilies(m_PhysicalDevice);

		// determine a queueFamilyIndex that supports present
		// first check if the graphicsIndex is good enough
		uint32_t presentIndex = m_PhysicalDevice.getSurfaceSupportKHR(graphicsIndex, *m_Surface)
			? graphicsIndex
			: static_cast<uint32_t>(queueFamilyProperties.size());

		if (presentIndex == queueFamilyProperties.size())
		{
			// the graphicsIndex doesn't support present -> look for another family index that supports both
			// graphics and present
			for (size_t i = 0; i < queueFamilyProperties.size(); i++)
			{
				if ((queueFamilyProperties[i].queueFlags & vk::QueueFlagBits::eGraphics) &&
					m_PhysicalDevice.getSurfaceSupportKHR(static_cast<uint32_t>(i), *m_Surface))
				{
					graphicsIndex = static_cast<uint32_t>(i);
					presentIndex = graphicsIndex;
					break;
				}
			}
			if (presentIndex == queueFamilyProperties.size())
			{
				// there's nothing like a single family index that supports both graphics and present -> look for another
				// family index that supports present
				for (size_t i = 0; i < queueFamilyProperties.size(); i++)
				{
					if (m_PhysicalDevice.getSurfaceSupportKHR(static_cast<uint32_t>(i), *m_Surface))
					{
						presentIndex = static_cast<uint32_t>(i);
						break;
					}
				}
			}
		}
		if ((graphicsIndex == queueFamilyProperties.size()) || (presentIndex == queueFamilyProperties.size()))
		{
			throw std::runtime_error("Could not find a queue for graphics or present -> terminating");
		}

		constexpr float queuePriority = 0.5f;
		vk::DeviceQueueCreateInfo deviceQueueCreateInfo{
			.queueFamilyIndex = graphicsIndex,
			.queueCount = 1,
			.pQueuePriorities = &queuePriority
		};

		// Create a chain of feature structures
		vk::StructureChain<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features, vk::PhysicalDeviceVulkan12Features, vk::PhysicalDeviceVulkan13Features, vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT> featureChain = {
			{ .features = {
				.geometryShader = true, .depthClamp = true, .depthBiasClamp = true, .samplerAnisotropy = true,
				.shaderInt64 = true
		       },
			},
			{ .shaderDrawParameters = true },
			{ .descriptorIndexing = true, .bufferDeviceAddress = true },
			{ .synchronization2 = true, .dynamicRendering = true },
			{ .extendedDynamicState = true }
		};

		const vk::DeviceCreateInfo deviceCreateInfo{
			.pNext = &featureChain.get<vk::PhysicalDeviceFeatures2>(),
			.queueCreateInfoCount = 1,
			.pQueueCreateInfos = &deviceQueueCreateInfo,
			.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size()),
			.ppEnabledExtensionNames = deviceExtensions.data()
		};

		m_Device = vk::raii::Device{ m_PhysicalDevice, deviceCreateInfo };

		m_GraphicsQueue = m_Device.getQueue(graphicsIndex, 0);
		m_PresentQueue = m_Device.getQueue(presentIndex, 0);

		m_GraphicsQueueFamilyIndex = graphicsIndex;
		m_PresentQueueFamilyIndex = presentIndex;
	}

	void VulkanContext::CreateAllocator() 
	{
		KBR_CORE_ASSERT(instance != nullptr, "VkInstance has to be initialized to create allocator!");
		KBR_CORE_ASSERT(physicalDevice != nullptr, "VkPhysicalDevice has to be initialized to create allocator!");
		KBR_CORE_ASSERT(device != nullptr, "VkDevice has to be initialized to create allocator!");

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

	void VulkanContext::CreateCommandPool()
	{
		const vk::CommandPoolCreateInfo poolInfo{
			.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
			.queueFamilyIndex = m_GraphicsQueueFamilyIndex
		};

		m_CommandPool = vk::raii::CommandPool(m_Device, poolInfo);
	}

	void VulkanContext::CreateCommandBuffers()
	{
		const vk::CommandBufferAllocateInfo allocInfo{
			.commandPool = m_CommandPool,
			.level = vk::CommandBufferLevel::ePrimary,
			.commandBufferCount = maxFramesInFlight
		};

		m_CommandBuffers = vk::raii::CommandBuffers(m_Device, allocInfo);
	}

	void VulkanContext::CreateSyncObjects()
	{
		assert(m_PresentCompleteSemaphores.empty() && m_RenderFinishedSemaphores.empty() && m_InFlightFences.empty());

		for (size_t i = 0; i < m_SwapChainImages.size(); i++)
		{
			m_RenderFinishedSemaphores.emplace_back(m_Device, vk::SemaphoreCreateInfo());
		}

		for (size_t i = 0; i < maxFramesInFlight; i++)
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
					m_MSAASamples,
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
					m_MSAASamples,
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

		const VkSampleCountFlagBits msaaSamplesVk = static_cast<VkSampleCountFlagBits>(m_MSAASamples);
		constexpr VkSampleCountFlagBits msaaSamplesViewportVk = VK_SAMPLE_COUNT_1_BIT;

		ImGui_ImplVulkan_InitInfo initInfo = {};
		ZeroMemory(&initInfo, sizeof(initInfo));
		initInfo.Instance = *m_Instance;
		initInfo.PhysicalDevice = *m_PhysicalDevice;
		initInfo.Device = *m_Device;
		initInfo.QueueFamily = m_GraphicsQueueFamilyIndex;
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
	}

	vk::SampleCountFlagBits VulkanContext::GetMaxUsableSampleCount(const vk::raii::PhysicalDevice& physicalDevice)
	{
		const vk::PhysicalDeviceProperties physicalDeviceProperties = physicalDevice.getProperties();

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