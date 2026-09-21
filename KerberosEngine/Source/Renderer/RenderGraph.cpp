#include "RenderGraph.hpp"

#include "VulkanContext.hpp"
#include "Utils.hpp"

#include <ranges>

namespace Kerberos {

RenderGraph::RenderGraph(const vk::raii::Device& device) : m_Device(device) {}

void RenderGraph::AddResource(const std::string& name,
    const vk::Format format,
    const vk::Extent2D extent,
    const vk::ImageUsageFlags usage,
    const vk::ImageLayout initialLayout,
    const vk::ImageLayout finalLayout)
{
    Resource resource;
    resource.Name = name;
    resource.Format = format;
    resource.Extent = extent;
    resource.Usage = usage;
    resource.InitialLayout = initialLayout; 
    resource.FinalLayout = finalLayout;

    m_Resources.emplace(name, std::move(resource));
}

void RenderGraph::AddPass(const std::string& name,
    const std::vector<std::string>& inputs,
    const std::vector<std::string>& outputs,
    const std::function<void(vk::raii::CommandBuffer&)>& executeFunc)
{
    Pass pass;
    pass.Name = name;
    pass.Inputs = inputs;
    pass.Outputs = outputs;
    pass.ExecuteFunc = executeFunc;

    m_Passes.push_back(pass); 
}

void RenderGraph::Compile()
{
    std::vector<std::vector<size_t>> dependencies(m_Passes.size()); // What each pass depends on
    std::vector<std::vector<size_t>> dependents(m_Passes.size());   // What depends on each pass

    // Track which pass produces each resource (write-after-write dependencies)
    std::unordered_map<std::string, size_t> resourceWriters;

    for (size_t i = 0; i < m_Passes.size(); ++i) {
        const auto& pass = m_Passes[i];

        // Process input dependencies - this pass must wait for producers
        for (const auto& input : pass.Inputs) {
            auto it = resourceWriters.find(input);
            if (it != resourceWriters.end()) {
                // Found the pass that produces this input - create dependency link
                dependencies[i].push_back(it->second); // This pass depends on the producer
                dependents[it->second].push_back(i);   // Producer has this as dependent
            }
        }

        // Register output production - subsequent passes may depend on these
        for (const auto& output : pass.Outputs) {
            resourceWriters[output] = i; // Record this pass as producer
        }
    }

    // Topological Sort for Optimal Execution Order
    // Use depth-first search to compute valid execution sequence while detecting cycles
    std::vector<bool> visited(m_Passes.size(), false); // Track completed nodes
    std::vector<bool> inStack(m_Passes.size(), false); // Track current recursion path

    std::function<void(size_t)> visit = [&](const size_t node) {
        if (inStack[node]) {
            // Cycle detection - circular dependency found
            throw std::runtime_error("Cycle detected in rendergraph");
        }

        if (visited[node]) {
            return; // Already processed this node and its dependencies
        }

        inStack[node] = true; // Mark as currently being processed

        // Recursively process all dependent passes first (post-order traversal)
        for (const auto dependent : dependents[node]) {
            visit(dependent);
        }

        inStack[node] = false;          // Remove from current path
        visited[node] = true;           // Mark as completely processed
        m_ExecutionOrder.push_back(node); // Add to execution sequence
    };

    for (size_t i = 0; i < m_Passes.size(); ++i) 
    {
        if (!visited[i]) {
            visit(i);
        }
    }

    for (size_t i = 0; i < m_Passes.size(); ++i) 
    {
        for (auto dep : dependencies[i]) 
        {
            // Create a GPU semaphore for this dependency relationship
            // The dependent pass will wait on this semaphore before executing
            m_Semaphores.emplace_back(m_Device.createSemaphore({}));
            m_SemaphoreSignalWaitPairs.emplace_back(dep, i); // (producer, consumer) pair
        }
    }

    for (auto& resource : m_Resources | std::views::values) 
    {
        vk::ImageCreateInfo imageInfo;
        imageInfo
            .setImageType(vk::ImageType::e2D)
            .setFormat(resource.Format)
            .setExtent({ resource.Extent.width, resource.Extent.height, 1 })
            .setMipLevels(1)
            .setArrayLayers(1)
            .setSamples(vk::SampleCountFlagBits::e1)
            .setTiling(vk::ImageTiling::eOptimal)
            .setUsage(resource.Usage)
            .setSharingMode(vk::SharingMode::eExclusive)
            .setInitialLayout(vk::ImageLayout::eUndefined);

        resource.Image = m_Device.createImage(imageInfo); // Create the GPU image object

        // Allocate backing memory for the image
        vk::MemoryRequirements memRequirements = resource.Image.getMemoryRequirements();

        vk::MemoryAllocateInfo allocInfo;
        allocInfo
            .setAllocationSize(memRequirements.size) // Required memory size
            .setMemoryTypeIndex(FindMemoryType(memRequirements.memoryTypeBits,
                                               vk::MemoryPropertyFlagBits::eDeviceLocal)); // GPU-local memory

        resource.Memory = m_Device.allocateMemory(allocInfo); // Allocate GPU memory
        resource.Image.bindMemory(*resource.Memory, 0);     // Bind memory to image

        // Create image view for shader access
        vk::ImageViewCreateInfo viewInfo;
        viewInfo
            .setImage(*resource.Image)
            .setViewType(vk::ImageViewType::e2D)
            .setFormat(resource.Format)
            .setSubresourceRange({ .aspectMask = vk::ImageAspectFlagBits::eColor, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1 }); // Full image access

        resource.View = m_Device.createImageView(viewInfo); // Create shader-accessible view
    }
}

void RenderGraph::Execute(vk::raii::CommandBuffer& commandBuffer, vk::Queue queue)
{
    // Execution state management for dynamic synchronization
    std::vector<vk::CommandBuffer> cmdBuffers;      // Command buffer storage
    std::vector<vk::Semaphore> waitSemaphores;      // Synchronization dependencies for current pass
    std::vector<vk::PipelineStageFlags2> waitStages; // Pipeline stages to wait on
    std::vector<vk::Semaphore> signalSemaphores;    // Semaphores to signal after current pass

    // Ordered Pass Execution with Automatic Dependency Management
    // Execute each pass in the computed dependency-safe order
    for (auto passIdx : m_ExecutionOrder) {
        const auto& pass = m_Passes[passIdx];

        // Synchronization Setup - Collect Dependencies for Current Pass
        // Determine what this pass must wait for before executing
        waitSemaphores.clear();
        waitStages.clear();

        for (size_t i = 0; i < m_SemaphoreSignalWaitPairs.size(); ++i) {
            if (m_SemaphoreSignalWaitPairs[i].WaitingPassIndex == passIdx) {
                // This pass depends on the completion of another pass
                waitSemaphores.push_back(*m_Semaphores[i]); // Wait for dependency completion
                waitStages.emplace_back(vk::PipelineStageFlagBits2::eColorAttachmentOutput); // Wait at output stage
            }
        }

        // Collect semaphores that this pass will signal for dependent passes
        signalSemaphores.clear();
        for (size_t i = 0; i < m_SemaphoreSignalWaitPairs.size(); ++i) {
            if (m_SemaphoreSignalWaitPairs[i].SignalingPassIndex == passIdx) {
                // Other passes depend on this pass's completion
                signalSemaphores.push_back(*m_Semaphores[i]); // Signal completion for dependents
            }
        }

        commandBuffer.begin({});

        // Transition input resources to shader-readable layouts
        for (const auto& input : pass.Inputs) {
            auto& resource = m_Resources[input];

            vk::ImageMemoryBarrier2 barrier;
            barrier
                .setOldLayout(resource.InitialLayout)
                .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
                .setSrcQueueFamilyIndex(vk::QueueFamilyIgnored)
                .setDstQueueFamilyIndex(vk::QueueFamilyIgnored)
                .setImage(*resource.Image)
                .setSubresourceRange({ .aspectMask = vk::ImageAspectFlagBits::eColor, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1 })
                .setSrcAccessMask(vk::AccessFlagBits2::eMemoryWrite)
                .setDstAccessMask(vk::AccessFlagBits2::eShaderRead)
                .setSrcStageMask(vk::PipelineStageFlagBits2::eAllCommands)
                .setDstStageMask(vk::PipelineStageFlagBits2::eFragmentShader);

            const vk::DependencyInfo dependencyInfo{ .dependencyFlags = vk::DependencyFlagBits::eByRegion,
                                                     .memoryBarrierCount = 0,
                                                     .pMemoryBarriers = nullptr,
                                                     .bufferMemoryBarrierCount = 0,
                                                     .pBufferMemoryBarriers = nullptr,
                                                     .imageMemoryBarrierCount = 1,
                                                     .pImageMemoryBarriers = &barrier };

            commandBuffer.pipelineBarrier2(dependencyInfo);
        }

        // Transition output resources to render target layouts
        for (const auto& output : pass.Outputs) {
            auto& resource = m_Resources[output];

            vk::ImageMemoryBarrier2 barrier;
            barrier
                .setOldLayout(resource.InitialLayout)
                .setNewLayout(vk::ImageLayout::eColorAttachmentOptimal)
                .setSrcQueueFamilyIndex(vk::QueueFamilyIgnored)
                .setDstQueueFamilyIndex(vk::QueueFamilyIgnored)
                .setImage(*resource.Image)
                .setSubresourceRange({ .aspectMask = vk::ImageAspectFlagBits::eColor, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1 })
                .setSrcAccessMask(vk::AccessFlagBits2::eMemoryRead)
                .setDstAccessMask(vk::AccessFlagBits2::eColorAttachmentWrite)
                .setSrcStageMask(vk::PipelineStageFlagBits2::eAllCommands)
                .setDstStageMask(vk::PipelineStageFlagBits2::eColorAttachmentOutput);

            const vk::DependencyInfo dependencyInfo{ .dependencyFlags = vk::DependencyFlagBits::eByRegion,
                                                     .memoryBarrierCount = 0,
                                                     .pMemoryBarriers = nullptr,
                                                     .bufferMemoryBarrierCount = 0,
                                                     .pBufferMemoryBarriers = nullptr,
                                                     .imageMemoryBarrierCount = 1,
                                                     .pImageMemoryBarriers = &barrier };

            commandBuffer.pipelineBarrier2(dependencyInfo);
        }

        pass.ExecuteFunc(commandBuffer);

        for (const auto& output : pass.Outputs) {
            auto& resource = m_Resources[output];

            vk::ImageMemoryBarrier2 barrier;
            barrier
                .setOldLayout(vk::ImageLayout::eColorAttachmentOptimal)
                .setNewLayout(resource.FinalLayout)
                .setSrcQueueFamilyIndex(vk::QueueFamilyIgnored)
                .setDstQueueFamilyIndex(vk::QueueFamilyIgnored)
                .setImage(*resource.Image)
                .setSubresourceRange({ .aspectMask = vk::ImageAspectFlagBits::eColor, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1 })
                .setSrcAccessMask(vk::AccessFlagBits2::eColorAttachmentWrite)
                .setDstAccessMask(vk::AccessFlagBits2::eMemoryRead)
                .setSrcStageMask(vk::PipelineStageFlagBits2::eColorAttachmentOutput)
                .setDstStageMask(vk::PipelineStageFlagBits2::eAllCommands);

            const vk::DependencyInfo dependencyInfo{ .dependencyFlags = vk::DependencyFlagBits::eByRegion,
                                                     .memoryBarrierCount = 0,
                                                     .pMemoryBarriers = nullptr,
                                                     .bufferMemoryBarrierCount = 0,
                                                     .pBufferMemoryBarriers = nullptr,
                                                     .imageMemoryBarrierCount = 1,
                                                     .pImageMemoryBarriers = &barrier };

            commandBuffer.pipelineBarrier2(dependencyInfo);
        }

        commandBuffer.end();

        vk::SubmitInfo submitInfo;
        submitInfo
            .setWaitSemaphoreCount(static_cast<uint32_t>(waitSemaphores.size()))
            .setPWaitSemaphores(waitSemaphores.data())
            .setPWaitDstStageMask(waitStages.data())
            .setCommandBufferCount(1)
            .setPCommandBuffers(&*commandBuffer)
            .setSignalSemaphoreCount(static_cast<uint32_t>(signalSemaphores.size()))
            .setPSignalSemaphores(signalSemaphores.data());

        const auto result = queue.submit(1, &submitInfo, nullptr);
        if (result != vk::Result::eSuccess) {
            throw std::runtime_error("Failed to submit render graph pass to queue");
        }
    }
}

RenderGraph::Resource* RenderGraph::GetResource(const std::string& name)
{
    const auto it = m_Resources.find(name);
    return (it != m_Resources.end()) ? &it->second : nullptr;
}


}