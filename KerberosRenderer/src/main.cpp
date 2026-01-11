#include "Vulkan.hpp"

#define VK_USE_PLATFORM_WIN32_KHR
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/hash.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"

#include <iostream>
#include <stdexcept>
#include <cstdlib>
#include <cstdint>
#include <limits>
#include <algorithm>
#include <vector>
#include <unordered_map>
#include <array>
#include <chrono>

#include "Buffer.hpp"
#include "io.hpp"
#include "Texture.hpp"
#include "Utils.hpp"
#include "GameObject.hpp"

#include "Application.hpp"
#include "GameLayer.hpp"

constexpr uint32_t width = 800;
constexpr uint32_t height = 600;

const std::string modelPath = "src/models/viking_room/viking_room.obj";
const std::string modelTexturePath = "src/models/viking_room/viking_room.png";

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

static void CheckVkResult(const VkResult err)
{
	if (err == VK_SUCCESS)
		return;
	std::ignore = fprintf(stderr, "[vulkan] Error: VkResult = %d\n", err);
	if (err < 0)
		abort();
}

struct Vertex
{
	glm::vec3 pos;
	glm::vec3 color;
	glm::vec2 texCoord;

	static vk::VertexInputBindingDescription GetBindingDescription() {
		return { 0, sizeof(Vertex), vk::VertexInputRate::eVertex };
	}

	static std::array<vk::VertexInputAttributeDescription, 3> GetAttributeDescriptions() {
		return {
			vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, pos)),
			vk::VertexInputAttributeDescription(1, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, color)),
			vk::VertexInputAttributeDescription(2, 0, vk::Format::eR32G32Sfloat, offsetof(Vertex, texCoord)),
		};
	}

	bool operator==(const Vertex& other) const {
		return pos == other.pos && color == other.color && texCoord == other.texCoord;
	}
};

namespace std {
	template<> struct hash<Vertex>
	{
		size_t operator()(Vertex const& vertex) const {
			return ((hash<glm::vec3>()(vertex.pos) ^
					 (hash<glm::vec3>()(vertex.color) << 1)) >> 1) ^
				(hash<glm::vec2>()(vertex.texCoord) << 1);
		}
	};
}

struct UniformBufferObject
{
	glm::mat4 model;
	glm::mat4 view;
	glm::mat4 proj;
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

	vk::raii::Buffer vertexBuffer = nullptr;
	vk::raii::DeviceMemory vertexBufferMemory = nullptr;
	vk::raii::Buffer indexBuffer = nullptr;
	vk::raii::DeviceMemory indexBufferMemory = nullptr;

	uint32_t mipLevels = 0;
	vk::raii::Image textureImage = nullptr;
	vk::raii::DeviceMemory textureImageMemory = nullptr;
	vk::raii::ImageView textureImageView = nullptr;

	vk::raii::Sampler textureSampler = nullptr;

	std::vector<vk::raii::Buffer> uniformBuffers;
	std::vector<vk::raii::DeviceMemory> uniformBuffersMemory;
	std::vector<void*> uniformBuffersMapped;

	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;

	std::vector<GameObject> gameObjects;

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

		CreateColorResources();
		CreateDepthResources();

		CreateDescriptorSetLayout();
		CreateGraphicsPipeline();
		CreateCommandPool();

		CreateTextureImage();
		CreateTextureImageView();
		CreateTextureSampler();

		LoadModel();
		SetupGameObjects();
		CreateVertexBuffer();
		CreateIndexBuffer();

		CreateUniformBuffers();
		CreateDescriptorPool();
		CreateDescriptorSets();

		CreateCommandBuffers();
		CreateSyncObjects();

		CreateImGuiDescriptorPool();
		SetupImGui();
	}

	void MainLoop()
	{
		bool showDemoWindow = true;
		bool showAnotherWindow = false;
		ImVec4 clearColor = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

		while (!glfwWindowShouldClose(window)) 
		{
			glfwPollEvents();

			ImGui_ImplVulkan_NewFrame();
			ImGui_ImplGlfw_NewFrame();
			ImGui::NewFrame();

			ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);

			if (showDemoWindow)
				ImGui::ShowDemoWindow(&showDemoWindow);

			{
				const auto& io = ImGui::GetIO();
				static float f = 0.0f;
				static int counter = 0;

				ImGui::Begin("Hello, world!");                          // Create a window called "Hello, world!" and append into it.

				ImGui::Text("This is some useful text.");               // Display some text (you can use a format strings too)
				ImGui::Checkbox("Demo Window", &showDemoWindow);      // Edit bools storing our window open/close state
				ImGui::Checkbox("Another Window", &showAnotherWindow);

				ImGui::SliderFloat("float", &f, 0.0f, 1.0f);            // Edit 1 float using a slider from 0.0f to 1.0f
				ImGui::ColorEdit3("clear color", reinterpret_cast<float*>(&clearColor)); // Edit 3 floats representing a color

				if (ImGui::Button("Button"))                            // Buttons return true when clicked (most widgets return true when edited/activated)
					counter++;
				ImGui::SameLine();
				ImGui::Text("counter = %d", counter);

				ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
				ImGui::End();
			}

			if (showAnotherWindow)
			{
				ImGui::Begin("Another Window", &showAnotherWindow);
				ImGui::Text("Hello from another window!");
				if (ImGui::Button("Close Me"))
					showAnotherWindow = false;
				ImGui::End();
			}

			ImGui::Render();

			DrawFrame();
		}

		device.waitIdle();
	}

	void Cleanup() const
	{
		ImGui_ImplVulkan_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();

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

		UpdateUniformBuffers(frameIndex);

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

		const ImGuiIO& io = ImGui::GetIO();
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();
		}

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
	}

	void RecordCommandBuffer(const uint32_t imageIndex) const
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
		constexpr vk::ClearValue clearDepth = vk::ClearDepthStencilValue{.depth = 1.0f, .stencil = 0 };

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

		commandBuffers[frameIndex].bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);
		commandBuffers[frameIndex].bindVertexBuffers(0, *vertexBuffer, {0});
		commandBuffers[frameIndex].bindIndexBuffer(*indexBuffer, 0, vk::IndexType::eUint32);

		commandBuffers[frameIndex].setViewport(0, vk::Viewport{ 0.0f, 0.0f, static_cast<float>(swapChainExtent.width), static_cast<float>(swapChainExtent.height), 0.0f, 1.0f });
		commandBuffers[frameIndex].setScissor(0, vk::Rect2D{ vk::Offset2D(0, 0), swapChainExtent });

		// Draw each object with its own descriptor set
		for (const auto& gameObject : gameObjects) {
			// Bind the descriptor set for this object
			commandBuffers[frameIndex].bindDescriptorSets(
				vk::PipelineBindPoint::eGraphics,
				*pipelineLayout,
				0,
				*gameObject.descriptorSets[frameIndex],
				nullptr
			);

			// Draw the object
			commandBuffers[frameIndex].drawIndexed(static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);
		}

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

	void CreateDescriptorSetLayout()
	{
		constexpr std::array bindings = {
			vk::DescriptorSetLayoutBinding{
				.binding = 0,
				.descriptorType = vk::DescriptorType::eUniformBuffer,
				.descriptorCount = 1,
				.stageFlags = vk::ShaderStageFlagBits::eVertex,
				.pImmutableSamplers = nullptr
			},
			vk::DescriptorSetLayoutBinding{
				.binding = 1,
				.descriptorType = vk::DescriptorType::eCombinedImageSampler,
				.descriptorCount = 1,
				.stageFlags = vk::ShaderStageFlagBits::eFragment,
				.pImmutableSamplers = nullptr
			}
		};
		const vk::DescriptorSetLayoutCreateInfo layoutInfo{
			.bindingCount = static_cast<uint32_t>(bindings.size()),
			.pBindings = bindings.data()
		};
		descriptorSetLayout = vk::raii::DescriptorSetLayout{ device, layoutInfo };
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
			.frontFace = vk::FrontFace::eCounterClockwise, 
			.depthBiasEnable = vk::False,
			.depthBiasSlopeFactor = 1.0f, 
			.lineWidth = 1.0f
		};

		vk::PipelineMultisampleStateCreateInfo multisampling{ 
			.rasterizationSamples = msaaSamples, 
			.sampleShadingEnable = vk::False 
		};

		vk::PipelineDepthStencilStateCreateInfo depthStencil{ 
			.depthTestEnable = vk::True, 
			.depthWriteEnable = vk::True,
			.depthCompareOp = vk::CompareOp::eLess, 
			.depthBoundsTestEnable = vk::False,
			.stencilTestEnable = vk::False,
			.minDepthBounds = 0.0f, 
			.maxDepthBounds = 1.0f,
		};

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
			.setLayoutCount = 1,
			.pSetLayouts = &*descriptorSetLayout,
			.pushConstantRangeCount = 0
		};

		pipelineLayout = vk::raii::PipelineLayout{ device, pipelineLayoutInfo };

		vk::PipelineRenderingCreateInfo pipelineRenderingCreateInfo{ 
			.colorAttachmentCount = 1, 
			.pColorAttachmentFormats = &swapChainImageFormat,
			.depthAttachmentFormat = depthFormat
		};

		vk::GraphicsPipelineCreateInfo pipelineInfo{ 
			.pNext = &pipelineRenderingCreateInfo,
			.stageCount = 2, 
			.pStages = shaderStages,
			.pVertexInputState = &vertexInputInfo, 
			.pInputAssemblyState = &inputAssembly,
			.pViewportState = &viewportState, 
			.pRasterizationState = &rasterizer,
			.pMultisampleState = &multisampling, 
			.pDepthStencilState = &depthStencil,
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

	vk::Format FindDepthFormat() const
	{
		return FindSupportedFormat(
			physicalDevice,
			{ vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint },
			vk::ImageTiling::eOptimal,
			vk::FormatFeatureFlagBits::eDepthStencilAttachment
		);
	}

	static bool HasStencilComponent(const vk::Format format) 
	{
		return format == vk::Format::eD32SfloatS8Uint || format == vk::Format::eD24UnormS8Uint;
	}

	void CreateColorResources()
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

	void CreateDepthResources()
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

	void CreateTextureImage() 
	{
		int texWidth, texHeight, texChannels;
		//stbi_uc* pixels = stbi_load("src/textures/texture.jpg", &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
		stbi_uc* pixels = stbi_load(modelTexturePath.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
		const vk::DeviceSize imageSize = static_cast<uint32_t>(texWidth) * texHeight * 4;

		if (!pixels) {
			throw std::runtime_error("failed to load texture image!");
		}

		mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(texWidth, texHeight)))) + 1;

		vk::raii::Buffer stagingBuffer({});
		vk::raii::DeviceMemory stagingBufferMemory({});

		CreateBuffer(device, physicalDevice,
					 imageSize, 
					 vk::BufferUsageFlagBits::eTransferSrc, 
					 vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, 
					 stagingBuffer, stagingBufferMemory);

		void* data = stagingBufferMemory.mapMemory(0, imageSize);
		memcpy(data, pixels, static_cast<size_t>(imageSize));
		stagingBufferMemory.unmapMemory();
		
		stbi_image_free(pixels);

		vk::raii::Image textureImageTemp({});
		vk::raii::DeviceMemory textureImageMemoryTemp({});
		CreateImage(device, physicalDevice, 
					texWidth, texHeight,
					mipLevels,
					vk::SampleCountFlagBits::e1,
					vk::Format::eR8G8B8A8Srgb, 
					vk::ImageTiling::eOptimal, 
					vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled, 
					vk::MemoryPropertyFlagBits::eDeviceLocal, 
					textureImage, textureImageMemory);

		TransitionImageLayout(textureImage, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, mipLevels);
		CopyBufferToImage(stagingBuffer, textureImage, static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight));

		// Transitions the image to SHADER_READ_ONLY_OPTIMAL after generating mipmaps
		GenerateMipMaps(textureImage, vk::Format::eR8G8B8A8Srgb, texWidth, texHeight, mipLevels);
		//TransitionImageLayout(textureImage, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, mipLevels);
	}

	void GenerateMipMaps(const vk::raii::Image& image, const vk::Format imageFormat, const int32_t texWidth, const int32_t texHeight, const uint32_t mipLevels) const 
	{
		// Check if image format supports linear blit-ing
		const vk::FormatProperties formatProperties = physicalDevice.getFormatProperties(imageFormat);
		if (!(formatProperties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eSampledImageFilterLinear)) {
			throw std::runtime_error("texture image format does not support linear blitting!");
		}

		const vk::raii::CommandBuffer cmd = BeginSingleTimeCommands();

		vk::ImageMemoryBarrier barrier{
			.srcAccessMask = vk::AccessFlagBits::eTransferWrite,
			.dstAccessMask = vk::AccessFlagBits::eTransferRead,
			.oldLayout = vk::ImageLayout::eUndefined,
			.newLayout = vk::ImageLayout::eTransferDstOptimal,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = image,
			.subresourceRange = {
				.aspectMask = vk::ImageAspectFlagBits::eColor,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1
			}
		};

		int32_t mipWidth = texWidth;
		int32_t mipHeight = texHeight;

		for (uint32_t i = 1; i < mipLevels; i++) 
		{
			barrier.subresourceRange.baseMipLevel = i - 1;
			barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
			barrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;
			barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
			barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;

			cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, barrier);

			vk::ArrayWrapper1D<vk::Offset3D, 2> offsets, dstOffsets;
			offsets[0] = vk::Offset3D(0, 0, 0);
			offsets[1] = vk::Offset3D(mipWidth, mipHeight, 1);
			dstOffsets[0] = vk::Offset3D(0, 0, 0);
			dstOffsets[1] = vk::Offset3D(mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1);

			vk::ImageBlit blit = { .srcSubresource = {}, .srcOffsets = offsets,
								.dstSubresource = {}, .dstOffsets = dstOffsets };
			blit.srcSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, i - 1, 0, 1);
			blit.dstSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, i, 0, 1);

			cmd.blitImage(image, vk::ImageLayout::eTransferSrcOptimal, image, vk::ImageLayout::eTransferDstOptimal, { blit }, vk::Filter::eLinear);

			barrier.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
			barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
			barrier.srcAccessMask = vk::AccessFlagBits::eTransferRead;
			barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

			cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, {}, {}, {}, barrier);

			if (mipWidth > 1) mipWidth /= 2;
			if (mipHeight > 1) mipHeight /= 2;
		}

		barrier.subresourceRange.baseMipLevel = mipLevels - 1;
		barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
		barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
		barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

		cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, {}, {}, {}, barrier);

		EndSingleTimeCommands(cmd);
	}

	void CreateTextureImageView()
	{
		textureImageView = CreateImageView(device, textureImage, vk::Format::eR8G8B8A8Srgb, vk::ImageAspectFlagBits::eColor, mipLevels);
	}

	void CreateTextureSampler()
	{
		const vk::PhysicalDeviceProperties properties = physicalDevice.getProperties();

		const vk::SamplerCreateInfo samplerInfo{
			.magFilter = vk::Filter::eLinear,
			.minFilter = vk::Filter::eLinear,
			.mipmapMode = vk::SamplerMipmapMode::eLinear,
			.addressModeU = vk::SamplerAddressMode::eRepeat,
			.addressModeV = vk::SamplerAddressMode::eRepeat,
			.addressModeW = vk::SamplerAddressMode::eRepeat,
			.mipLodBias = 0.0f,
			.anisotropyEnable = vk::True,
			.maxAnisotropy = properties.limits.maxSamplerAnisotropy,
			.compareEnable = vk::False,
			.compareOp = vk::CompareOp::eAlways,
			.minLod = 0.0,
			.maxLod = vk::LodClampNone,
			.borderColor = vk::BorderColor::eIntOpaqueBlack,
			.unnormalizedCoordinates = vk::False,
		};
		textureSampler = vk::raii::Sampler(device, samplerInfo);
	}

	void LoadModel()
	{
		tinyobj::attrib_t attrib;
		std::vector<tinyobj::shape_t> shapes;
		std::vector<tinyobj::material_t> materials;
		std::string warn, err;

		if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, modelPath.c_str())) {
			throw std::runtime_error(warn + err);
		}

		std::unordered_map<Vertex, uint32_t> uniqueVertices{};

		for (const auto& shape : shapes) {
			for (const auto& index : shape.mesh.indices) {
				Vertex vertex{};

				vertex.pos = {
					attrib.vertices[3 * index.vertex_index + 0],
					attrib.vertices[3 * index.vertex_index + 1],
					attrib.vertices[3 * index.vertex_index + 2]
				};

				/*vertex.texCoord = {
					attrib.texcoords[2 * index.texcoord_index + 0],
					attrib.texcoords[2 * index.texcoord_index + 1]
				};*/
				vertex.texCoord = {
	                attrib.texcoords[2 * index.texcoord_index + 0],
					1.0f - attrib.texcoords[2 * index.texcoord_index + 1]
				};

				vertex.color = { 1.0f, 1.0f, 1.0f };

				if (!uniqueVertices.contains(vertex)) {
					uniqueVertices[vertex] = static_cast<uint32_t>(vertices.size());
					vertices.push_back(vertex);
				}

				indices.push_back(uniqueVertices[vertex]);
			}
		}
	}

	void SetupGameObjects()
	{
		gameObjects.push_back(GameObject{
			.position = {0.0f, 0.0f, 0.0f}, .rotation = {0.0f, 0.0f, 0.0f}, .scale = {1.0f, 1.0f, 1.0f}
		});

		gameObjects.push_back(GameObject{
			.position = {-2.0f, 0.0f, -1.0f}, .rotation = {0.0f, glm::radians(45.0f), 0.0f}, .scale = {0.75f, 0.75f, 0.75f}
		});

		gameObjects.push_back(GameObject{
			.position = {2.0f, 0.0f, -1.0f}, .rotation = {0.0f, glm::radians(-45.0f), 0.0f}, .scale = {0.80f, 0.80f, 0.80f}
		});
	}

	void CreateVertexBuffer() 
	{
		const vk::DeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();
		const vk::BufferCreateInfo stagingInfo{ .size = bufferSize, .usage = vk::BufferUsageFlagBits::eTransferSrc, .sharingMode = vk::SharingMode::eExclusive };
		const vk::raii::Buffer stagingBuffer(device, stagingInfo);
		const vk::MemoryRequirements memRequirementsStaging = stagingBuffer.getMemoryRequirements();
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

		const vk::BufferCreateInfo bufferInfo{ 
			.size = bufferSize,  
			.usage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst, 
			.sharingMode = vk::SharingMode::eExclusive 
		};
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

	void CreateUniformBuffers()
	{
		uniformBuffers.clear();
		uniformBuffersMemory.clear();
		uniformBuffersMapped.clear();

		for (size_t i = 0; i < swapChainImages.size(); i++)
		{
			constexpr vk::DeviceSize bufferSize = sizeof(UniformBufferObject);
			vk::raii::Buffer uniformBuffer({});
			vk::raii::DeviceMemory uniformBufferMemory({});
			CreateBuffer(device, physicalDevice, bufferSize,
			             vk::BufferUsageFlagBits::eUniformBuffer,
			             vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
			             uniformBuffer, uniformBufferMemory);

			uniformBuffers.emplace_back(std::move(uniformBuffer));
			uniformBuffersMemory.emplace_back(std::move(uniformBufferMemory));
			uniformBuffersMapped.emplace_back(uniformBuffersMemory.back().mapMemory(0, bufferSize));
		}

		for (auto& gameObject : gameObjects) {
			gameObject.uniformBuffers.clear();
			gameObject.uniformBuffersMemory.clear();
			gameObject.uniformBuffersMapped.clear();

			// Create uniform buffers for each frame in flight
			for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) 
			{
				constexpr vk::DeviceSize bufferSize = sizeof(UniformBufferObject);
				vk::raii::Buffer buffer({});
				vk::raii::DeviceMemory bufferMem({});
				CreateBuffer(device, physicalDevice, bufferSize, 
							 vk::BufferUsageFlagBits::eUniformBuffer,
							 vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
							 buffer, bufferMem);

				gameObject.uniformBuffers.emplace_back(std::move(buffer));
				gameObject.uniformBuffersMemory.emplace_back(std::move(bufferMem));
				gameObject.uniformBuffersMapped.emplace_back(gameObject.uniformBuffersMemory[i].mapMemory(0, bufferSize));
			}
		}
	}

	void CreateDescriptorPool()
	{
		const std::array poolSizes = {
			vk::DescriptorPoolSize{
				.type = vk::DescriptorType::eUniformBuffer,
				.descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT * gameObjects.size())
			},
			vk::DescriptorPoolSize{
				.type = vk::DescriptorType::eCombinedImageSampler,
				.descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT * gameObjects.size())
			}
		};

		const vk::DescriptorPoolCreateInfo poolInfo{
			.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
			.maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT * gameObjects.size()),
			.poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
			.pPoolSizes = poolSizes.data()
		};

		descriptorPool = vk::raii::DescriptorPool{ device, poolInfo };
	}

	void CreateDescriptorSets()
	{
		for (auto& gameObject : gameObjects) {
			// Create descriptor sets for each frame in flight
			std::vector<vk::DescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, *descriptorSetLayout);
			vk::DescriptorSetAllocateInfo allocInfo{
				.descriptorPool = *descriptorPool,
				.descriptorSetCount = static_cast<uint32_t>(layouts.size()),
				.pSetLayouts = layouts.data()
			};

			gameObject.descriptorSets.clear();
			gameObject.descriptorSets = device.allocateDescriptorSets(allocInfo);

			for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
				vk::DescriptorBufferInfo bufferInfo{
					.buffer = *gameObject.uniformBuffers[i],
					.offset = 0,
					.range = sizeof(UniformBufferObject)
				};
				vk::DescriptorImageInfo imageInfo{
					.sampler = *textureSampler,
					.imageView = *textureImageView,
					.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
				};
				std::array descriptorWrites{
					vk::WriteDescriptorSet{
						.dstSet = *gameObject.descriptorSets[i],
						.dstBinding = 0,
						.dstArrayElement = 0,
						.descriptorCount = 1,
						.descriptorType = vk::DescriptorType::eUniformBuffer,
						.pBufferInfo = &bufferInfo
					},
					vk::WriteDescriptorSet{
						.dstSet = *gameObject.descriptorSets[i],
						.dstBinding = 1,
						.dstArrayElement = 0,
						.descriptorCount = 1,
						.descriptorType = vk::DescriptorType::eCombinedImageSampler,
						.pImageInfo = &imageInfo
					}
				};
				device.updateDescriptorSets(descriptorWrites, {});
			}
		}
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

	void CreateImGuiDescriptorPool()
	{
		const std::array poolSizes = {
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

	void SetupImGui()
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
			.colorAttachmentCount = 1,
			.pColorAttachmentFormats = &colorFormatVk,
			.depthAttachmentFormat = depthFormatVk,
			.stencilAttachmentFormat = VK_FORMAT_UNDEFINED
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

	void UpdateUniformBuffers(const uint32_t currentImage) 
	{
		static auto startTime = std::chrono::high_resolution_clock::now();
		const auto currentTime = std::chrono::high_resolution_clock::now();
		const float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

		const glm::mat4 view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
		glm::mat4 proj = glm::perspective(glm::radians(45.0f), 
									static_cast<float>(swapChainExtent.width) / static_cast<float>(swapChainExtent.height),
									0.1f, 10.0f);
		proj[1][1] *= -1; // Invert Y coordinate for Vulkan

		for (auto& gameObject : gameObjects) {
			// Apply continuous rotation to the object
			gameObject.rotation.y += 0.001f; // Slow rotation around Y axis

			// Get the model matrix for this object
			glm::mat4 initialRotation = glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
			const glm::mat4 model = gameObject.GetModelMatrix() * initialRotation;

			// Create and update the UBO
			UniformBufferObject ubo{
				.model = model,
				.view = view,
				.proj = proj
			};

			// Copy the UBO data to the mapped memory
			memcpy(gameObject.uniformBuffersMapped[frameIndex], &ubo, sizeof(ubo));
		}
	}

	void TransitionImageLayout(
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

	void TransitionImageLayout(const vk::raii::Image& image, const vk::ImageLayout oldLayout, const vk::ImageLayout newLayout, const uint32_t mipLevels) const 
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

	vk::raii::CommandBuffer BeginSingleTimeCommands() const
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

	void EndSingleTimeCommands(const vk::raii::CommandBuffer& commandBuffer) const
	{
		commandBuffer.end();
		const vk::SubmitInfo submitInfo{
			.commandBufferCount = 1,
			.pCommandBuffers = &*commandBuffer
		};
		graphicsQueue.submit(submitInfo, nullptr);
		graphicsQueue.waitIdle();
	}

	void CopyBufferToImage(const vk::raii::Buffer& buffer, const vk::raii::Image& image, const uint32_t _width, const uint32_t _height) const
	{
		const vk::raii::CommandBuffer commandBuffer = BeginSingleTimeCommands();
		vk::BufferImageCopy region{
			.bufferOffset = 0,
			.bufferRowLength = 0,
			.bufferImageHeight = 0,
			.imageSubresource = {
				.aspectMask = vk::ImageAspectFlagBits::eColor,
				.mipLevel = 0,
				.baseArrayLayer = 0,
				.layerCount = 1
			},
			.imageOffset = { 0, 0, 0 },
			.imageExtent = {
				.width = _width,
				.height = _height,
				.depth = 1
			}
		};
		commandBuffer.copyBufferToImage(
			buffer,
			image,
			vk::ImageLayout::eTransferDstOptimal,
			{ region }
		);
		EndSingleTimeCommands(commandBuffer);
	}

	static vk::SampleCountFlagBits GetMaxUsableSampleCount(const vk::raii::PhysicalDevice& physicalDevice) 
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
		CreateColorResources();
		CreateDepthResources();
	}
};

int main() {
	try {
		/*HelloTriangleApplication app;
		app.run();*/

		kbr::Application app;
		app.PushLayer<Game::GameLayer>();

		app.Run();
	}
	catch (const std::exception& e) {
		std::cerr << e.what() << '\n';
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}