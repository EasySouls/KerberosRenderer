#pragma once

#include "Vulkan.hpp"
#include "VulkanContext.hpp"
#include "Core/Core.hpp"
#include "UI/UITypes.hpp"

#include <array>

namespace Kerberos {

class GraphicsPipeline;
class TextureManager;

/// One pipeline that batches solid/gradient rectangles,
/// images and MSDF text into the final display-referred image.
class UIRenderer
{
public:
    UIRenderer() = default;
    ~UIRenderer() = default;

    UIRenderer(const UIRenderer&) = delete;
    UIRenderer& operator=(const UIRenderer&) = delete;

    void Init(TextureManager& textureManager, vk::Format colorFormat);
    void Shutdown();

    void Render(const vk::raii::CommandBuffer& cmd, uint32_t frameIndex,
                const UIDrawData& drawData, glm::vec2 screenSize);

private:
    struct UIPushConstants
    {
        glm::vec2 ScreenSize{ 0.0f };
        float Padding[2]{ 0.0f, 0.0f };
    };

    struct FrameBuffer
    {
        vk::raii::Buffer Buffer{ nullptr };
        vk::raii::DeviceMemory Memory{ nullptr };
        void* Mapped = nullptr;
        vk::DeviceSize Capacity = 0;
    };

    static void CreateFrameBuffers(FrameBuffer& buffer, vk::DeviceSize size, vk::BufferUsageFlags usage);
    static void EnsureCapacity(FrameBuffer& buffer, vk::DeviceSize requiredSize, vk::BufferUsageFlags usage);
    
private:
    TextureManager* m_TextureManager = nullptr;
    vk::raii::PipelineLayout m_PipelineLayout{ nullptr };
    Ref<GraphicsPipeline> m_Pipeline = nullptr;

    std::array<FrameBuffer, VulkanContext::MaxFramesInFlight> m_VertexBuffers{};
    std::array<FrameBuffer, VulkanContext::MaxFramesInFlight> m_IndexBuffers{};
};

}
