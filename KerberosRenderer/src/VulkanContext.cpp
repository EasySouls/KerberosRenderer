#include "VulkanContext.hpp"

#include <iostream>

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"

#include "Texture.hpp"
#include "Utils.hpp"

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
	vk::KHRCreateRenderpass2ExtensionName
};

#ifdef NDEBUG
constexpr bool enableValidationLayers = false;
#else
constexpr bool enableValidationLayers = true;
#endif

static VKAPI_ATTR vk::Bool32 VKAPI_CALL DebugCallback(
	vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
	const vk::DebugUtilsMessageTypeFlagsEXT type,
	const vk::DebugUtilsMessengerCallbackDataEXT* pCallbackData,
	void*)
{
	std::cerr << "validation layer: type " << to_string(type) << " msg: " << pCallbackData->pMessage << '\n';

	return vk::False;
}

namespace kbr
{
	VulkanContext::VulkanContext(GLFWwindow* window)
		: window(window)
	{
		CreateInstance();
		SetupDebugMessenger();
		CreateSurface();
		PickPhysicalDevice();
		CreateLogicalDevice();
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
		const vk::Result fenceResult = device.waitForFences(*inFlightFences[frameIndex], vk::True, UINT64_MAX);
		if (fenceResult != vk::Result::eSuccess)
		{
			throw std::runtime_error("failed to wait for fence!");
		}

		auto [result, imageIndex] = swapChain.acquireNextImage(UINT64_MAX, *presentCompleteSemaphores[frameIndex], nullptr);
		if (result == vk::Result::eErrorOutOfDateKHR) {
			RecreateSwapchain();
			return;
		}
		if (result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR) {
			throw std::runtime_error("failed to acquire swap chain image!");
		}
		currentImageIndex = imageIndex;

		device.resetFences(*inFlightFences[frameIndex]);

		commandBuffers[frameIndex].reset();
		RecordCommandBuffer(currentImageIndex);

		//UpdateUniformBuffers(frameIndex);

		//vk::PipelineStageFlags waitDestinationStageMask(vk::PipelineStageFlagBits::eTopOfPipe);
		vk::PipelineStageFlags waitDestinationStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput);
		const vk::SubmitInfo submitInfo{
			.waitSemaphoreCount = 1,
			.pWaitSemaphores = &*presentCompleteSemaphores[frameIndex],
			.pWaitDstStageMask = &waitDestinationStageMask,
			.commandBufferCount = 1,
			.pCommandBuffers = &*commandBuffers[frameIndex],
			.signalSemaphoreCount = 1,
			.pSignalSemaphores = &*renderFinishedSemaphores[imageIndex] };

		graphicsQueue.submit(submitInfo, *inFlightFences[frameIndex]);
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
			.pWaitSemaphores = &*renderFinishedSemaphores[currentImageIndex],
			.swapchainCount = 1,
			.pSwapchains = &*swapChain,
			.pImageIndices = &currentImageIndex
		};

		const auto result = presentQueue.presentKHR(presentInfoKHR);
		if (result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eSuboptimalKHR || framebufferResized) {
			framebufferResized = false;
			RecreateSwapchain();
		}
		else if (result != vk::Result::eSuccess) {
			throw std::runtime_error("failed to present swap chain image!");
		}

		frameIndex = (frameIndex + 1) % maxFramesInFlight;
	}

	vk::raii::CommandBuffer VulkanContext::BeginSingleTimeCommands() const
	{
		const vk::CommandBufferAllocateInfo allocInfo{
			.commandPool = commandPool,
			.level = vk::CommandBufferLevel::ePrimary,
			.commandBufferCount = 1
		};
		vk::raii::CommandBuffer commandBuffer = std::move(vk::raii::CommandBuffers(device, allocInfo).front());

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
		graphicsQueue.submit(submitInfo, nullptr);
		graphicsQueue.waitIdle();
	}

	uint32_t VulkanContext::GetMaxFramesInFlight() const 
	{
		return maxFramesInFlight;
	}

	void VulkanContext::FramebufferResized(uint32_t width, uint32_t height) 
	{
		framebufferResized = true;
	}

	void VulkanContext::RecordCommandBuffer(const uint32_t imageIndex) const 
	{
		commandBuffers[frameIndex].begin({});

		// Transition the swapchain image to COLOR_ATTACHMENT_OPTIMAL
		TransitionImageLayout(
			swapChainImages[imageIndex],
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
			colorImage,
			vk::ImageLayout::eUndefined,
			vk::ImageLayout::eColorAttachmentOptimal,
			vk::AccessFlagBits2::eColorAttachmentWrite,
			vk::AccessFlagBits2::eColorAttachmentWrite,
			vk::PipelineStageFlagBits2::eColorAttachmentOutput,
			vk::PipelineStageFlagBits2::eColorAttachmentOutput,
			vk::ImageAspectFlagBits::eColor);

		// Transition the depth image to DEPTH_STENCIL_ATTACHMENT_OPTIMAL
		TransitionImageLayout(
			depthImage,
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
			.imageView = colorImageView,
			.imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
			.resolveMode = vk::ResolveModeFlagBits::eAverage,
			.resolveImageView = swapChainImageViews[imageIndex],
			.resolveImageLayout = vk::ImageLayout::eColorAttachmentOptimal,
			.loadOp = vk::AttachmentLoadOp::eClear,
			.storeOp = vk::AttachmentStoreOp::eStore,
			.clearValue = clearColor
		};

		vk::RenderingAttachmentInfo depthAttachmentInfo = {
			.imageView = depthImageView,
			.imageLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal,
			.loadOp = vk::AttachmentLoadOp::eClear,
			.storeOp = vk::AttachmentStoreOp::eDontCare,
			.clearValue = clearDepth
		};

		const vk::RenderingInfo renderingInfo = {
			.renderArea = {.offset = {.x = 0, .y = 0 }, .extent = swapChainExtent },
			.layerCount = 1,
			.colorAttachmentCount = 1,
			.pColorAttachments = &attachmentInfo,
			.pDepthAttachment = &depthAttachmentInfo
		};

		commandBuffers[frameIndex].beginRendering(renderingInfo);

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

		ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), *commandBuffers[frameIndex]);

		commandBuffers[frameIndex].endRendering();

		// After rendering, transition the swapchain image to PRESENT_SRC
		TransitionImageLayout(
			swapChainImages[imageIndex],
			vk::ImageLayout::eColorAttachmentOptimal,
			vk::ImageLayout::ePresentSrcKHR,
			vk::AccessFlagBits2::eColorAttachmentWrite,
			{},
			vk::PipelineStageFlagBits2::eColorAttachmentOutput,
			vk::PipelineStageFlagBits2::eBottomOfPipe,
			vk::ImageAspectFlagBits::eColor
		);

		commandBuffers[frameIndex].end();
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
		auto layerProperties = context.enumerateInstanceLayerProperties();
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
		auto extensionProperties = context.enumerateInstanceExtensionProperties();
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

		instance = vk::raii::Instance{ context, createInfo };
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

		debugMessenger = instance.createDebugUtilsMessengerEXT(debugUtilsMessengerCreateInfoEXT);
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
		if (glfwCreateWindowSurface(static_cast<VkInstance>(*instance), window, nullptr, &_surface) != VK_SUCCESS) {
			throw std::runtime_error("failed to create window surface!");
		}
		surface = vk::raii::SurfaceKHR{ instance, _surface };
	}

	void VulkanContext::PickPhysicalDevice()
	{
		const auto devices = instance.enumeratePhysicalDevices();
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
				physicalDevice = device;
				msaaSamples = GetMaxUsableSampleCount(physicalDevice);
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
		std::vector<vk::QueueFamilyProperties> queueFamilyProperties = physicalDevice.getQueueFamilyProperties();
		uint32_t graphicsIndex = FindQueueFamilies(physicalDevice);

		// determine a queueFamilyIndex that supports present
		// first check if the graphicsIndex is good enough
		uint32_t presentIndex = physicalDevice.getSurfaceSupportKHR(graphicsIndex, *surface)
			? graphicsIndex
			: static_cast<uint32_t>(queueFamilyProperties.size());

		if (presentIndex == queueFamilyProperties.size())
		{
			// the graphicsIndex doesn't support present -> look for another family index that supports both
			// graphics and present
			for (size_t i = 0; i < queueFamilyProperties.size(); i++)
			{
				if ((queueFamilyProperties[i].queueFlags & vk::QueueFlagBits::eGraphics) &&
					physicalDevice.getSurfaceSupportKHR(static_cast<uint32_t>(i), *surface))
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
					if (physicalDevice.getSurfaceSupportKHR(static_cast<uint32_t>(i), *surface))
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
		vk::StructureChain<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features, vk::PhysicalDeviceVulkan13Features, vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT> featureChain = {
			{.features = {.samplerAnisotropy = true } },
			{.shaderDrawParameters = true },
			{.synchronization2 = true, .dynamicRendering = true },
			{.extendedDynamicState = true }
		};

		vk::DeviceCreateInfo deviceCreateInfo{
			.pNext = &featureChain.get<vk::PhysicalDeviceFeatures2>(),
			.queueCreateInfoCount = 1,
			.pQueueCreateInfos = &deviceQueueCreateInfo,
			.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size()),
			.ppEnabledExtensionNames = deviceExtensions.data()
		};

		device = vk::raii::Device{ physicalDevice, deviceCreateInfo };

		graphicsQueue = device.getQueue(graphicsIndex, 0);
		presentQueue = device.getQueue(presentIndex, 0);

		graphicsQueueFamilyIndex = graphicsIndex;
		presentQueueFamilyIndex = presentIndex;
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
		glfwGetFramebufferSize(window, &width, &height);

		return {
			.width = std::clamp(static_cast<uint32_t>(width), capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
			.height = std::clamp(static_cast<uint32_t>(height), capabilities.minImageExtent.height, capabilities.maxImageExtent.height)
		};
	}

	void VulkanContext::CreateSwapChain()
	{
		const auto surfaceCapabilities = physicalDevice.getSurfaceCapabilitiesKHR(surface);
		const auto [format, colorSpace] = ChooseSwapSurfaceFormat(physicalDevice.getSurfaceFormatsKHR(surface));
		const auto extent = ChooseSwapExtent(surfaceCapabilities);

		auto minImageCount = std::max(3u, surfaceCapabilities.minImageCount);
		minImageCount = (surfaceCapabilities.maxImageCount > 0 && minImageCount > surfaceCapabilities.maxImageCount)
			? surfaceCapabilities.maxImageCount
			: minImageCount;

		uint32_t imageCount = surfaceCapabilities.minImageCount + 1;
		if (surfaceCapabilities.maxImageCount > 0 && imageCount > surfaceCapabilities.maxImageCount) {
			imageCount = surfaceCapabilities.maxImageCount;
		}

		const vk::PresentModeKHR presentMode = ChooseSwapPresentMode(physicalDevice.getSurfacePresentModesKHR(surface));

		const vk::SwapchainCreateInfoKHR swapChainCreateInfo{
			.flags = vk::SwapchainCreateFlagsKHR(),
			.surface = surface,
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

		swapChain = vk::raii::SwapchainKHR(device, swapChainCreateInfo);
		swapChainImageFormat = format;
		swapChainExtent = extent;

		swapChainImages = swapChain.getImages();
	}

	void VulkanContext::CreateSwapChainImageViews()
	{
		swapChainImageViews.clear();

		vk::ImageViewCreateInfo imageViewCreateInfo{
			.viewType = vk::ImageViewType::e2D,
			.format = swapChainImageFormat,
			.subresourceRange = {
				.aspectMask = vk::ImageAspectFlagBits::eColor,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1
			}
		};

		for (const auto image : swapChainImages) {
			imageViewCreateInfo.image = image;
			swapChainImageViews.emplace_back(device, imageViewCreateInfo);
		}
	}

	void VulkanContext::CreateCommandPool()
	{
		const vk::CommandPoolCreateInfo poolInfo{
			.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
			.queueFamilyIndex = graphicsQueueFamilyIndex
		};

		commandPool = vk::raii::CommandPool(device, poolInfo);
	}

	void VulkanContext::CreateCommandBuffers()
	{
		const vk::CommandBufferAllocateInfo allocInfo{
			.commandPool = commandPool,
			.level = vk::CommandBufferLevel::ePrimary,
			.commandBufferCount = maxFramesInFlight
		};

		commandBuffers = vk::raii::CommandBuffers(device, allocInfo);
	}

	void VulkanContext::CreateSyncObjects()
	{
		assert(presentCompleteSemaphores.empty() && renderFinishedSemaphores.empty() && inFlightFences.empty());

		for (size_t i = 0; i < swapChainImages.size(); i++)
		{
			renderFinishedSemaphores.emplace_back(device, vk::SemaphoreCreateInfo());
		}

		for (size_t i = 0; i < maxFramesInFlight; i++)
		{
			presentCompleteSemaphores.emplace_back(device, vk::SemaphoreCreateInfo());
			inFlightFences.emplace_back(device, vk::FenceCreateInfo{ .flags = vk::FenceCreateFlagBits::eSignaled });
		}
	}

	vk::Format VulkanContext::FindDepthFormat() const
	{
		return FindSupportedFormat(
			physicalDevice,
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
		const vk::Format colorFormat = swapChainImageFormat;

		CreateImage(device, physicalDevice,
					swapChainExtent.width,
					swapChainExtent.height,
					1,
					msaaSamples,
					colorFormat,
					vk::ImageTiling::eOptimal,
					vk::ImageUsageFlagBits::eTransientAttachment | vk::ImageUsageFlagBits::eColorAttachment,
					vk::MemoryPropertyFlagBits::eDeviceLocal,
					colorImage,
					colorImageMemory);

		colorImageView = CreateImageView(device, colorImage, colorFormat, vk::ImageAspectFlagBits::eColor, 1);
	}

	void VulkanContext::CreateDepthResources()
	{
		depthFormat = FindDepthFormat();

		CreateImage(device, physicalDevice,
					swapChainExtent.width,
					swapChainExtent.height,
					1,
					msaaSamples,
					depthFormat,
					vk::ImageTiling::eOptimal,
					vk::ImageUsageFlagBits::eDepthStencilAttachment,
					vk::MemoryPropertyFlagBits::eDeviceLocal,
					depthImage,
					depthImageMemory);

		depthImageView = CreateImageView(device, depthImage, depthFormat, vk::ImageAspectFlagBits::eDepth, 1);
	}

	void VulkanContext::CleanupSwapChain() 
	{
		swapChainImageViews.clear();
		swapChain = nullptr;
	}

	void VulkanContext::RecreateSwapchain() 
	{
		int width = 0, height = 0;
		glfwGetFramebufferSize(window, &width, &height);
		while (width == 0 || height == 0) {
			glfwGetFramebufferSize(window, &width, &height);
			glfwWaitEvents();
		}

		device.waitIdle();

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

		commandBuffers[frameIndex].pipelineBarrier2(dependencyInfo);
	}

	void VulkanContext::Cleanup() const 
	{
		device.waitIdle();

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
		imGuiDescriptorPool = vk::raii::DescriptorPool{ device, poolInfo };
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

		ImGui_ImplGlfw_InitForVulkan(window, true);

		const VkFormat colorFormatVk = static_cast<VkFormat>(swapChainImageFormat);
		const VkFormat depthFormatVk = static_cast<VkFormat>(depthFormat);

		const VkPipelineRenderingCreateInfoKHR pipelineRenderingCreateInfo = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
			.pNext = nullptr,
			.viewMask = 0,
			.colorAttachmentCount = 1,
			.pColorAttachmentFormats = &colorFormatVk,
			.depthAttachmentFormat = depthFormatVk,
			.stencilAttachmentFormat = VK_FORMAT_UNDEFINED,
		};

		const VkSampleCountFlagBits msaaSamplesVk = static_cast<VkSampleCountFlagBits>(msaaSamples);
		constexpr VkSampleCountFlagBits msaaSamplesViewportVk = VK_SAMPLE_COUNT_1_BIT;

		ImGui_ImplVulkan_InitInfo initInfo = {};
		ZeroMemory(&initInfo, sizeof(initInfo));
		initInfo.Instance = *instance;
		initInfo.PhysicalDevice = *physicalDevice;
		initInfo.Device = *device;
		initInfo.QueueFamily = graphicsQueueFamilyIndex;
		initInfo.Queue = *graphicsQueue;
		initInfo.PipelineCache = nullptr;
		initInfo.DescriptorPool = *imGuiDescriptorPool;
		initInfo.MinImageCount = static_cast<uint32_t>(swapChainImages.size());
		initInfo.ImageCount = static_cast<uint32_t>(swapChainImages.size());
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