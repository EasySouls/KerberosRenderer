#pragma once

#include "SkinningTypes.hpp"
#include "Renderer/DescriptorAllocator.hpp"
#include "Renderer/Graph.hpp"
#include "Scene/Scene.hpp"

namespace Kerberos {

class AnimationSystem
{
public:
    void Initialize(const Owner<DescriptorAllocator>& frameAllocator);
    void Shutdown();

    void BeginFrame(const Ref<Scene>& scene, float dt, bool paused);
    void AddPasses(RenderGraph::Graph& graph, uint32_t frameIndex);

    const SkinningFrameSnapshot& GetSkinningSnapshot() const;

    //std::optional<PoseView> GetPose(UUID skeletonEntity) const;

    void RecompileShaders() const;

    void Reset();
};

}