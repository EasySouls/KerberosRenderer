#include "AnimationSystem.hpp"

namespace Kerberos {

void AnimationSystem::Initialize(const Owner<DescriptorAllocator>& frameAllocator)
{
    throw std::logic_error("Not implemented");
}

void AnimationSystem::Shutdown()
{
    throw std::logic_error("Not implemented");
}

void AnimationSystem::BeginFrame(const Ref<Scene>& scene, float dt, bool paused)
{
    throw std::logic_error("Not implemented");
}

void AnimationSystem::AddPasses(RenderGraph::Graph& graph, uint32_t frameIndex)
{
    throw std::logic_error("Not implemented");
}

const SkinningFrameSnapshot& AnimationSystem::GetSkinningSnapshot() const
{
    throw std::logic_error("Not implemented");
}

void AnimationSystem::RecompileShaders() const
{
    throw std::logic_error("Not implemented");
}

void AnimationSystem::Reset()
{
    throw std::logic_error("Not implemented");
}

}