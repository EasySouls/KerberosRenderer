#pragma once

#include "Vulkan.hpp"

#include <string>
#include <vector>

namespace Kerberos {

class RenderTarget
{
public:
    RenderTarget(uint32_t width, uint32_t height);

    vk::ImageView GetColorImageView() const
    {
        return *m_ColorImageView;
    }
    vk::ImageView GetDepthImageView() const
    {
        return *m_DepthImageView;
    }

    uint32_t GetWidth() const
    {
        return m_Width;
    }
    uint32_t GetHeight() const
    {
        return m_Height;
    }

private:
    void CreateColorResources();
    void CreateDepthResources();

private:
    vk::raii::Image m_ColorImage = nullptr;
    vk::raii::DeviceMemory m_ColorMemory = nullptr;
    vk::raii::ImageView m_ColorImageView = nullptr;

    vk::raii::Image m_DepthImage = nullptr;
    vk::raii::DeviceMemory m_DepthMemory = nullptr;
    vk::raii::ImageView m_DepthImageView = nullptr;

    uint32_t m_Width = 0;
    uint32_t m_Height = 0;
};

class RenderPass
{
public:
    explicit RenderPass(const std::string& name);
    virtual ~RenderPass() = default;

    RenderPass(const RenderPass& other) = default;
    RenderPass(RenderPass&& other) noexcept = default;
    RenderPass& operator=(const RenderPass& other) = default;
    RenderPass& operator=(RenderPass&& other) noexcept = default;

    const std::string& GetName() const
    {
        return m_Name;
    }

    void AddDependency(const std::string& dependency)
    {
        m_Dependencies.push_back(dependency);
    }

    const std::vector<std::string>& GetDependencies() const
    {
        return m_Dependencies;
    }

    void SetRenderTarget(RenderTarget* renderTarget)
    {
        m_Target = renderTarget;
    }

    RenderTarget* GetRenderTarget() const
    {
        return m_Target;
    }

    void SetEnabled(const bool isEnabled)
    {
        m_Enabled = isEnabled;
    }

    bool IsEnabled() const
    {
        return m_Enabled;
    }

    virtual void Execute(vk::raii::CommandBuffer& commandBuffer)
    {
        if (!m_Enabled)
            return;

        BeginPass(commandBuffer);
        Render(commandBuffer);
        EndPass(commandBuffer);
    }

protected:
    virtual void BeginPass(vk::raii::CommandBuffer& commandBuffer) = 0;
    virtual void Render(vk::raii::CommandBuffer& commandBuffer) = 0;
    virtual void EndPass(vk::raii::CommandBuffer& commandBuffer) = 0;

private:
    std::string m_Name;
    std::vector<std::string> m_Dependencies;
    RenderTarget* m_Target = nullptr;
    bool m_Enabled = true;
};

}