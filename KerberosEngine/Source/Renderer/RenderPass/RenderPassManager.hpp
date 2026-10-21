#pragma once

#include "Vulkan.hpp"
#include "RenderPass.hpp"

#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace Kerberos {

class RenderPassManager
{
public:
    RenderPassManager() = default;
    ~RenderPassManager() = default;

    RenderPassManager(const RenderPassManager& other) = delete;
    RenderPassManager(RenderPassManager&& other) noexcept = delete;
    RenderPassManager& operator=(const RenderPassManager& other) = delete;
    RenderPassManager& operator=(RenderPassManager&& other) noexcept = delete;

    template <typename T, typename... Args> T* AddRenderPass(const std::string& name, Args&&... args)
        requires std::is_base_of_v<RenderPass, T>
    {
        const auto it = m_RenderPasses.find(name);
        if (it != m_RenderPasses.end()) {
            return dynamic_cast<T*>(it->second.get());
        }

        auto pass = std::make_unique<T>(std::forward<Args>(args)...);
        T* passPtr = pass.get();
        m_RenderPasses[name] = std::move(pass);
        m_Dirty = true;

        return passPtr;
    }

    RenderPass* GetRenderPass(const std::string& name);

    void RemoveRenderPass(const std::string& name);

    void Execute(vk::raii::CommandBuffer& commandBuffer);

private:
    void SortPasses();

    void TopologicalSort(const std::string& name,
                         const std::unordered_map<std::string, RenderPass*>& passMap,
                         std::unordered_set<std::string>& visited,
                         std::unordered_set<std::string>& visiting);

private:
    std::unordered_map<std::string, std::unique_ptr<RenderPass>> m_RenderPasses;
    std::vector<RenderPass*> m_SortedPasses;
    bool m_Dirty = true;
};

}