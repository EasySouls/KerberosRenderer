#include "RenderPass.hpp"

namespace Kerberos {

RenderTarget::RenderTarget(const uint32_t width, const uint32_t height)
    : m_Width(width), m_Height(height)
{
    CreateColorResources();
    CreateDepthResources();
}

void RenderTarget::CreateColorResources()
{
    throw std::logic_error("Not implemented");
}

void RenderTarget::CreateDepthResources()
{
    throw std::logic_error("Not implemented");
}

RenderPass::RenderPass(const std::string& name) : m_Name(name) {}

}