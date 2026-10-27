#include "SkinningSystem.hpp"

namespace Kerberos {

void SkinningSystem::Initialize()
{
    throw std::logic_error("Not implemented");
}

void SkinningSystem::Shutdown()
{
    throw std::logic_error("Not implemented");
}

void SkinningSystem::BeginFrame(const Ref<Scene>& scene, const SkinningFrameSnapshot&)
{
    throw std::logic_error("Not implemented");
}

void SkinningSystem::AddPasses(RenderGraph::Graph& graph, uint32_t frameIndex)
{
    throw std::logic_error("Not implemented");
}

std::optional<SkinnedDrawView> SkinningSystem::FindMesh(UUID meshEntity) const
{
    throw std::logic_error("Not implemented");
}

void SkinningSystem::RecompileShaders() const
{
    throw std::logic_error("Not implemented");
}

void SkinningSystem::Reset()
{
    throw std::logic_error("Not implemented");
}

}