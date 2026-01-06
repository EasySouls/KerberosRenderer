#include "Vulkan.hpp"

#define VK_USE_PLATFORM_WIN32_KHR
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <glm/glm.hpp>

#include <iostream>
#include <stdexcept>
#include <cstdlib>
#include <cstdint>
#include <limits>
#include <algorithm>
#include <vector>
#include <map>
#include <array>

#include "Buffer.hpp"
#include "io.hpp"
#include "Utils.hpp"

constexpr uint32_t width = 800;
constexpr uint32_t height = 600;

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

struct Vertex
{
	glm::vec2 pos;
	glm::vec3 color;

	static vk::VertexInputBindingDescription GetBindingDescription() {
		return { 0, sizeof(Vertex), vk::VertexInputRate::eVertex };
	}

	static std::array<vk::VertexInputAttributeDescription, 2> GetAttributeDescriptions() {
		return {
			vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32Sfloat, offsetof(Vertex, pos)),
			vk::VertexInputAttributeDescription(1, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, color))
		};
	}
};

const std::vector<Vertex> vertices = {
	{{-0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}},
	{{0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}},
	{{0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}},
	{{-0.5f, 0.5f}, {1.0f, 1.0f, 1.0f}}
};

const std::vector<uint16_t> indices = {
	0, 1, 2, 2, 3, 0
};

constexpr int MAX_FRAMES_IN_FLIGHT = 2;

class HelloTriangleApplication
{
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

	vk::raii::SurfaceKHR surface = nullptr;

	vk::raii::SwapchainKHR swapChain = nullptr;
	vk::Format swapChainImageFormat = vk::Format::eUndefined;
	vk::Extent2D swapChainExtent;

	std::vector<vk::Image> swapChainImages;
	std::vector<vk::raii::ImageView> swapChainImageViews;

	vk::raii::PipelineLayout pipelineLayout = nullptr;
	vk::raii::Pipeline graphicsPipeline = nullptr;

	vk::raii::CommandPool commandPool = nullptr;
	std::vector<vk::raii::CommandBuffer> commandBuffers;

	std::vector<vk::raii::Semaphore> presentCompleteSemaphores;
	std::vector<vk::raii::Semaphore> renderFinishedSemaphores;
	std::vector<vk::raii::Fence> inFlightFences;

	bool framebufferResized = false;

	uint32_t frameIndex = 0;

	vk::raii::Buffer vertexBuffer = nullptr;
	vk::raii::DeviceMemory vertexBufferMemory = nullptr;
	vk::raii::Buffer indexBuffer = nullptr;
	vk::raii::DeviceMemory indexBufferMemory = nullptr;

public:
	void run()
	{
		InitWindow();
		InitVulkan();
		MainLoop();
		Cleanup();
	}

private:
	static void FramebufferResizeCallback(GLFWwindow* window, int _width, int _height) 
	{
		const auto app = static_cast<HelloTriangleApplication*>(glfwGetWindowUserPointer(window));
		app->framebufferResized = true;
	}

	void InitWindow()
	{
		glfwInit();

		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

		window = glfwCreateWindow(width, height, "Vulkan", nullptr, nullptr);
		glfwSetWindowUserPointer(window, this);
		glfwSetFramebufferSizeCallback(window, FramebufferResizeCallback);
	}

	void InitVulkan()
	{
		CreateInstance();
		SetupDebugMessenger();
		CreateSurface();
		PickPhysicalDevice();
		CreateLogicalDevice();
		CreateSwapChain();
		CreateSwapChainImageViews();
		CreateGraphicsPipeline();
		CreateCommandPool();
		CreateVertexBuffer();
		CreateIndexBuffer();
		CreateCommandBuffers();
		CreateSyncObjects();
	}

	void MainLoop()
	{
		while (!glfwWindowShouldClose(window)) {
			glfwPollEvents();

			DrawFrame();
		}

		device.waitIdle();
	}

	void Cleanup() const
	{
		glfwDestroyWindow(window);

		glfwTerminate();
	}

	void DrawFrame()
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

		device.resetFences(*inFlightFences[frameIndex]);

		commandBuffers[frameIndex].reset();
		RecordCommandBuffer(imageIndex);


		//vk::PipelineStageFlags waitDestinationStageMask(vk::PipelineStageFlagBits::eTopOfPipe);
		vk::PipelineStageFlags waitDestinationStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput);
		const vk::SubmitInfo submitInfo{
			.waitSemaphoreCount = 1,
			.pWaitSemaphores = &*presentCompleteSemaphores[frameIndex],
			.pWaitDstStageMask = &waitDestinationStageMask,
			.commandBufferCount = 1,
			.pCommandBuffers = &*commandBuffers[frameIndex],
			.signalSemaphoreCount = 1,
			.pSignalSemaphores = &*renderFinishedSemaphores[imageIndex]};

		graphicsQueue.submit(submitInfo, *inFlightFences[frameIndex]);

		const vk::PresentInfoKHR presentInfoKHR{
			.waitSemaphoreCount = 1,
			.pWaitSemaphores = &*renderFinishedSemaphores[imageIndex],
			.swapchainCount = 1,
			.pSwapchains = &*swapChain,
			.pImageIndices = &imageIndex 
		};

		result = presentQueue.presentKHR(presentInfoKHR);
		if (result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eSuboptimalKHR || framebufferResized) {
			framebufferResized = false;
			RecreateSwapchain();
		}
		else if (result != vk::Result::eSuccess) {
			throw std::runtime_error("failed to present swap chain image!");
		}

		frameIndex = (frameIndex + 1) % MAX_FRAMES_IN_FLIGHT;

		frameIndex = (frameIndex + 1) % MAX_FRAMES_IN_FLIGHT;
	}

	void RecordCommandBuffer(const uint32_t imageIndex) const
	{
		commandBuffers[frameIndex].begin({});

		TransitionImageLayout(
			commandBuffers[frameIndex],
			imageIndex,
			vk::ImageLayout::eUndefined,
			vk::ImageLayout::eColorAttachmentOptimal,
			{},
			vk::AccessFlagBits2::eColorAttachmentWrite,
			vk::PipelineStageFlagBits2::eColorAttachmentOutput,
			vk::PipelineStageFlagBits2::eColorAttachmentOutput
		);

		constexpr vk::ClearValue clearColor = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);
		vk::RenderingAttachmentInfo attachmentInfo = {
			.imageView = swapChainImageViews[imageIndex],
			.imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
			.loadOp = vk::AttachmentLoadOp::eClear,
			.storeOp = vk::AttachmentStoreOp::eStore,
			.clearValue = clearColor
		};

		const vk::RenderingInfo renderingInfo = {
			.renderArea = {.offset = {.x = 0, .y = 0 }, .extent = swapChainExtent },
			.layerCount = 1,
			.colorAttachmentCount = 1,
			.pColorAttachments = &attachmentInfo
		};

		commandBuffers[frameIndex].beginRendering(renderingInfo);

		commandBuffers[frameIndex].bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);
		commandBuffers[frameIndex].bindVertexBuffers(0, *vertexBuffer, {0});
		commandBuffers[frameIndex].bindIndexBuffer(*indexBuffer, 0, vk::IndexType::eUint16);

		commandBuffers[frameIndex].setViewport(0, vk::Viewport{ 0.0f, 0.0f, static_cast<float>(swapChainExtent.width), static_cast<float>(swapChainExtent.height), 0.0f, 1.0f });
		commandBuffers[frameIndex].setScissor(0, vk::Rect2D{ vk::Offset2D(0, 0), swapChainExtent });

		commandBuffers[frameIndex].drawIndexed(indices.size(), 1, 0, 0, 0);

		commandBuffers[frameIndex].endRendering();

		// After rendering, transition the swapchain image to PRESENT_SRC
		TransitionImageLayout(
			commandBuffers[frameIndex],
			imageIndex,
			vk::ImageLayout::eColorAttachmentOptimal,
			vk::ImageLayout::ePresentSrcKHR,
			vk::AccessFlagBits2::eColorAttachmentWrite,
			{},
			vk::PipelineStageFlagBits2::eColorAttachmentOutput,
			vk::PipelineStageFlagBits2::eBottomOfPipe
		);

		commandBuffers[frameIndex].end();
	}

	void CreateInstance()
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

		instance = vk::raii::Instance{context, createInfo};
	}

	void SetupDebugMessenger() {
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

	static std::vector<const char*> GetRequiredExtensions() 
	{
		uint32_t glfwExtensionCount = 0;
		const auto glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

		std::vector extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

		if (enableValidationLayers) {
			extensions.push_back(vk::EXTDebugUtilsExtensionName);
		}

		return extensions;
	}

	void CreateSurface()
	{
		VkSurfaceKHR _surface;
		if (glfwCreateWindowSurface(static_cast<VkInstance>(*instance), window, nullptr, &_surface) != VK_SUCCESS) {
			throw std::runtime_error("failed to create window surface!");
		}
		surface = vk::raii::SurfaceKHR{ instance, _surface };
	}

	void PickPhysicalDevice()
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

	uint32_t FindQueueFamilies(const vk::raii::PhysicalDevice& physicalDevice) {
		// find the index of the first queue family that supports graphics
		std::vector<vk::QueueFamilyProperties> queueFamilyProperties = physicalDevice.getQueueFamilyProperties();

		// get the first index into queueFamilyProperties which supports graphics
		const auto graphicsQueueFamilyProperty =
			std::find_if(queueFamilyProperties.begin(),
						 queueFamilyProperties.end(),
						 [](vk::QueueFamilyProperties const& qfp) { return qfp.queueFlags & vk::QueueFlagBits::eGraphics; });

		return static_cast<uint32_t>(std::distance(queueFamilyProperties.begin(), graphicsQueueFamilyProperty));
	}

	void CreateLogicalDevice()
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
		vk::DeviceQueueCreateInfo deviceQueueCreateInfo { 
			.queueFamilyIndex = graphicsIndex,
			.queueCount = 1,
			.pQueuePriorities = &queuePriority
		};

		// Create a chain of feature structures
		vk::StructureChain<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features, vk::PhysicalDeviceVulkan13Features, vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT> featureChain = {
			{},
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
			if (availableFormat.format == vk::Format::eB8G8R8A8Srgb && availableFormat.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
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

	vk::Extent2D ChooseSwapExtent(const vk::SurfaceCapabilitiesKHR& capabilities) const 
	{
		if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
			return capabilities.currentExtent;
		}
		
		int width, height;
		glfwGetFramebufferSize(window, &width, &height);

		return {
			std::clamp(static_cast<uint32_t>(width), capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
			std::clamp(static_cast<uint32_t>(height), capabilities.minImageExtent.height, capabilities.maxImageExtent.height)
		};
	}

	void CreateSwapChain()
	{
		const auto surfaceCapabilities = physicalDevice.getSurfaceCapabilitiesKHR(surface);
		const auto swapChainSurfaceFormat = ChooseSwapSurfaceFormat(physicalDevice.getSurfaceFormatsKHR(surface));
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
			.imageFormat = swapChainSurfaceFormat.format, 
			.imageColorSpace = swapChainSurfaceFormat.colorSpace,
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
		swapChainImageFormat = swapChainSurfaceFormat.format;
		swapChainExtent = extent;

		swapChainImages = swapChain.getImages();
	}

	void CreateSwapChainImageViews()
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

	[[nodiscard]] 
	vk::raii::ShaderModule CreateShaderModule(const std::vector<char>& code) const 
	{
		const vk::ShaderModuleCreateInfo createInfo{ .codeSize = code.size() * sizeof(char), .pCode = reinterpret_cast<const uint32_t*>(code.data()) };
		vk::raii::ShaderModule shaderModule{ device, createInfo };
		return shaderModule;
	}

	void CreateGraphicsPipeline()
	{
		const auto shaderCode = IO::ReadFile("src/shaders/simple.spv");
		vk::raii::ShaderModule shaderModule = CreateShaderModule(shaderCode);

		const vk::PipelineShaderStageCreateInfo vertShaderStageInfo{ .stage = vk::ShaderStageFlagBits::eVertex, .module = shaderModule,  .pName = "vertMain" };
		const vk::PipelineShaderStageCreateInfo fragShaderStageInfo{ .stage = vk::ShaderStageFlagBits::eFragment, .module = shaderModule, .pName = "fragMain" };
		vk::PipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

		std::vector dynamicStates = {
			vk::DynamicState::eViewport,
			vk::DynamicState::eScissor
		};

		const vk::PipelineDynamicStateCreateInfo dynamicStateInfo{
			.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
			.pDynamicStates = dynamicStates.data()
		};

		auto bindingDescription = Vertex::GetBindingDescription();
		auto attributeDescriptions = Vertex::GetAttributeDescriptions();
		vk::PipelineVertexInputStateCreateInfo vertexInputInfo{
			.vertexBindingDescriptionCount = 1,
			.pVertexBindingDescriptions = &bindingDescription,
			.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size()),
			.pVertexAttributeDescriptions = attributeDescriptions.data(),
		};

		vk::PipelineInputAssemblyStateCreateInfo inputAssembly{ .topology = vk::PrimitiveTopology::eTriangleList };

		vk::PipelineViewportStateCreateInfo viewportState{ .viewportCount = 1, .scissorCount = 1 };

		vk::PipelineRasterizationStateCreateInfo rasterizer{ 
			.depthClampEnable = vk::False, 
			.rasterizerDiscardEnable = vk::False,
			.polygonMode = vk::PolygonMode::eFill, 
			.cullMode = vk::CullModeFlagBits::eBack,
			.frontFace = vk::FrontFace::eClockwise, 
			.depthBiasEnable = vk::False,
			.depthBiasSlopeFactor = 1.0f, 
			.lineWidth = 1.0f };

		vk::PipelineMultisampleStateCreateInfo multisampling{ .rasterizationSamples = vk::SampleCountFlagBits::e1, .sampleShadingEnable = vk::False };

		vk::PipelineColorBlendAttachmentState colorBlendAttachment{
			.blendEnable = vk::False,
			.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA 
		};

		vk::PipelineColorBlendStateCreateInfo colorBlending{ 
			.logicOpEnable = vk::False, 
			.logicOp = vk::LogicOp::eCopy, 
			.attachmentCount = 1, 
			.pAttachments = &colorBlendAttachment 
		};

		vk::PipelineLayoutCreateInfo pipelineLayoutInfo{
			.setLayoutCount = 0,
			.pushConstantRangeCount = 0
		};

		pipelineLayout = vk::raii::PipelineLayout{ device, pipelineLayoutInfo };

		vk::PipelineRenderingCreateInfo pipelineRenderingCreateInfo{ .colorAttachmentCount = 1, .pColorAttachmentFormats = &swapChainImageFormat };

		vk::GraphicsPipelineCreateInfo pipelineInfo{ 
			.pNext = &pipelineRenderingCreateInfo,
			.stageCount = 2, 
			.pStages = shaderStages,
			.pVertexInputState = &vertexInputInfo, 
			.pInputAssemblyState = &inputAssembly,
			.pViewportState = &viewportState, 
			.pRasterizationState = &rasterizer,
			.pMultisampleState = &multisampling, 
			.pColorBlendState = &colorBlending,
			.pDynamicState = &dynamicStateInfo, 
			.layout = pipelineLayout, 
			.renderPass = nullptr 
		};

		graphicsPipeline = vk::raii::Pipeline(device, nullptr, pipelineInfo);
	}

	void CreateCommandPool()
	{
		const vk::CommandPoolCreateInfo poolInfo{
			.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
			.queueFamilyIndex = graphicsQueueFamilyIndex
		};

		commandPool = vk::raii::CommandPool(device, poolInfo);
	}

	

	void CreateVertexBuffer() 
	{
		const vk::DeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();
		vk::BufferCreateInfo stagingInfo{ .size = bufferSize, .usage = vk::BufferUsageFlagBits::eTransferSrc, .sharingMode = vk::SharingMode::eExclusive };
		vk::raii::Buffer stagingBuffer(device, stagingInfo);
		vk::MemoryRequirements memRequirementsStaging = stagingBuffer.getMemoryRequirements();
		const vk::MemoryAllocateInfo memoryAllocateInfoStaging{
			.allocationSize = memRequirementsStaging.size,
			.memoryTypeIndex = FindMemoryType(physicalDevice, memRequirementsStaging.memoryTypeBits,
			                                  vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent)
		};
		const vk::raii::DeviceMemory stagingBufferMemory(device, memoryAllocateInfoStaging);

		stagingBuffer.bindMemory(stagingBufferMemory, 0);
		void* dataStaging = stagingBufferMemory.mapMemory(0, stagingInfo.size);
		memcpy(dataStaging, vertices.data(), stagingInfo.size);
		stagingBufferMemory.unmapMemory();

		const vk::BufferCreateInfo bufferInfo{ .size = bufferSize,  .usage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst, .sharingMode = vk::SharingMode::eExclusive };
		vertexBuffer = vk::raii::Buffer(device, bufferInfo);

		const vk::MemoryRequirements memRequirements = vertexBuffer.getMemoryRequirements();
		const vk::MemoryAllocateInfo memoryAllocateInfo{
			.allocationSize = memRequirements.size,
			.memoryTypeIndex = FindMemoryType(physicalDevice, memRequirements.memoryTypeBits,
			                                  vk::MemoryPropertyFlagBits::eDeviceLocal)
		};
		vertexBufferMemory = vk::raii::DeviceMemory(device, memoryAllocateInfo);

		vertexBuffer.bindMemory(*vertexBufferMemory, 0);

		CopyBuffer(device, commandPool, graphicsQueue, stagingBuffer, vertexBuffer, stagingInfo.size);
	}

	void CreateIndexBuffer()
	{
		const vk::DeviceSize bufferSize = sizeof(indices[0]) * indices.size();

		vk::raii::Buffer stagingBuffer({});
		vk::raii::DeviceMemory stagingBufferMemory({});
		CreateBuffer(device, physicalDevice, bufferSize,
					 vk::BufferUsageFlagBits::eTransferSrc,
					 vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
					 stagingBuffer, stagingBufferMemory);

		void* data = stagingBufferMemory.mapMemory(0, bufferSize);
		memcpy(data, indices.data(), bufferSize);
		stagingBufferMemory.unmapMemory();

		CreateBuffer(device, physicalDevice, bufferSize, 
					 vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer, 
					 vk::MemoryPropertyFlagBits::eDeviceLocal, 
					 indexBuffer, indexBufferMemory);

		CopyBuffer(device, commandPool, graphicsQueue, stagingBuffer, indexBuffer, bufferSize);
	}

	void CreateCommandBuffers()
	{
		const vk::CommandBufferAllocateInfo allocInfo{ 
			.commandPool = commandPool, 
			.level = vk::CommandBufferLevel::ePrimary, 
			.commandBufferCount = MAX_FRAMES_IN_FLIGHT 
		};

		commandBuffers = vk::raii::CommandBuffers(device, allocInfo);
	}

	void CreateSyncObjects()
	{
		assert(presentCompleteSemaphores.empty() && renderFinishedSemaphores.empty() && inFlightFences.empty());

		for (size_t i = 0; i < swapChainImages.size(); i++)
		{
			renderFinishedSemaphores.emplace_back(device, vk::SemaphoreCreateInfo());
		}

		for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
		{
			presentCompleteSemaphores.emplace_back(device, vk::SemaphoreCreateInfo());
			inFlightFences.emplace_back(device, vk::FenceCreateInfo{ .flags = vk::FenceCreateFlagBits::eSignaled });
		}
	}

	void TransitionImageLayout(
		const vk::raii::CommandBuffer& cmd,
		const uint32_t imageIndex,
		const vk::ImageLayout oldLayout,
		const vk::ImageLayout newLayout,
		const vk::AccessFlags2 srcAccessMask,
		const vk::AccessFlags2 dstAccessMask,
		const vk::PipelineStageFlags2 srcStageMask,
		const vk::PipelineStageFlags2 dstStageMask
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
			.image = swapChainImages[imageIndex],
			.subresourceRange = {
				.aspectMask = vk::ImageAspectFlagBits::eColor,
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

		cmd.pipelineBarrier2(dependencyInfo);
	}

	void CleanupSwapchain() 
	{
		swapChainImageViews.clear();
		swapChain = nullptr;
	}

	void RecreateSwapchain()
	{
		int _width = 0, _height = 0;
		glfwGetFramebufferSize(window, &_width, &_height);
		while (_width == 0 || _height == 0) {
			glfwGetFramebufferSize(window, &_width, &_height);
			glfwWaitEvents();
		}

		device.waitIdle();

		CleanupSwapchain();

		CreateSwapChain();
		CreateSwapChainImageViews();
	}
};


int main() {
	try {
		HelloTriangleApplication app;
		app.run();
	}
	catch (const std::exception& e) {
		std::cerr << e.what() << '\n';
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}