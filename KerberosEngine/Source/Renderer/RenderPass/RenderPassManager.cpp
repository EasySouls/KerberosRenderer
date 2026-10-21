#include "RenderPassManager.hpp"

#include <ranges>

namespace Kerberos {

RenderPass* RenderPassManager::GetRenderPass(const std::string& name)
{
    const auto it = m_RenderPasses.find(name);
    if (it != m_RenderPasses.end()) {
        return it->second.get();
    }
    return nullptr;
}

void RenderPassManager::RemoveRenderPass(const std::string& name)
{
    const auto it = m_RenderPasses.find(name);
    if (it != m_RenderPasses.end()) {
        m_RenderPasses.erase(it);
        m_Dirty = true;
    }
}

void RenderPassManager::Execute(vk::raii::CommandBuffer& commandBuffer)
{
    if (m_Dirty) {
        SortPasses();
        m_Dirty = false;
    }

    for (const auto pass : m_SortedPasses) {
        pass->Execute(commandBuffer);
    }
}

void RenderPassManager::SortPasses()
{
    m_SortedPasses.clear();

    std::unordered_map<std::string, RenderPass*> passMap;
    for (const auto& [name, pass] : m_RenderPasses) {
        passMap[name] = pass.get();
    }

    std::unordered_set<std::string> visited;
    std::unordered_set<std::string> visiting;

    for (const auto& name : passMap | std::views::keys) {
        if (!visited.contains(name)) {
            TopologicalSort(name, passMap, visited, visiting);
        }
    }
}

void RenderPassManager::TopologicalSort(const std::string& name,
                                        const std::unordered_map<std::string, RenderPass*>& passMap,
                                        std::unordered_set<std::string>& visited,
                                        std::unordered_set<std::string>& visiting)
{
    visiting.insert(name);

    const auto pass = passMap.at(name);
    for (const auto& dep : pass->GetDependencies()) {
        if (!visited.contains(dep)) {
            if (visiting.contains(dep)) {
                throw std::runtime_error("Circular dependency detected in render passes");
            }
            TopologicalSort(dep, passMap, visited, visiting);
        }
    }

    visiting.erase(name);
    visited.insert(name);
    m_SortedPasses.push_back(pass);
}

}