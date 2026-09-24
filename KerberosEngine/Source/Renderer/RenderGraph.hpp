#pragma once

#include "Vulkan.hpp"

#include <string>
#include <vector>
#include <functional>

namespace Kerberos {

	class RenderGraph
	{
    public:
        /**
         * Resource description and management structure.
         * Represents Image resource used during rendering (textures).
         */
        struct Resource
        {
            std::string Name;              // Human-readable identifier for debugging and referencing
            vk::Format Format;             // Pixel format
            vk::Extent2D Extent;           // Dimensions in pixels for 2D resources
            vk::ImageUsageFlags Usage;     // How this resource will be used
            vk::ImageLayout InitialLayout; // Expected layout when the frame begins
            vk::ImageLayout FinalLayout;   // Required layout when the frame ends

            vk::raii::Image Image = nullptr;
            vk::raii::DeviceMemory Memory = nullptr;
            vk::raii::ImageView View = nullptr;

            Resource() = default;
            Resource(const std::string& name,
                     vk::Format format,
                     vk::Extent2D extent,
                     vk::ImageUsageFlags usage,
                     vk::ImageLayout initialLayout,
                     vk::ImageLayout finalLayout)
                : Name(name), Format(format), Extent(extent), Usage(usage), InitialLayout(initialLayout),
                  FinalLayout(finalLayout)
            {
            }
        };

    public:
        explicit RenderGraph(const vk::raii::Device& device);

        void AddResource(const std::string& name,
                         vk::Format format,
                         vk::Extent2D extent,
                         vk::ImageUsageFlags usage,
                         vk::ImageLayout initialLayout,
                         vk::ImageLayout finalLayout);

        void AddPass(const std::string& name,
                     const std::vector<std::string>& inputs,
                     const std::vector<std::string>& outputs,
                     const std::function<void(vk::raii::CommandBuffer&)>& executeFunc);

        void Compile();

        void Execute(vk::raii::CommandBuffer& commandBuffer, vk::Queue queue);

        Resource* GetResource(const std::string& name);

	private:
        // Render pass representation within the graph structure.
        // Each pass represents a distinct rendering operation with defined inputs and outputs.
        struct Pass
        {
            std::string Name;                                          // Descriptive name for debugging and profiling
            std::vector<std::string> Inputs;                           // Resources this pass reads from
            std::vector<std::string> Outputs;                          // Resources this pass writes to
            std::function<void(vk::raii::CommandBuffer&)> ExecuteFunc; // The actual rendering code
        };

        std::unordered_map<std::string, Resource> m_Resources; // All resources referenced in the graph
        std::vector<Pass> m_Passes;                            // All rendering passes in definition order
        std::vector<size_t> m_ExecutionOrder;                  // Computed optimal execution sequence

        struct SemaphoreDependency
        {
            size_t SignalingPassIndex; // Index of the pass that signals this semaphore
            size_t WaitingPassIndex;   // Index of the pass that waits on this semaphore
        };
        std::vector<vk::raii::Semaphore> m_Semaphores;                  // GPU synchronization primitives
        std::vector<SemaphoreDependency> m_SemaphoreSignalWaitPairs;    // Dependencies between passes for synchronization

        const vk::raii::Device& m_Device;
	};

}