#pragma once

#include "Core/Core.hpp"
#include "SkinningTypes.hpp"
#include "Renderer/Graph.hpp"
#include "Scene/Scene.hpp"

namespace Kerberos {

class SkinningSystem
{
public:
    void Initialize();
    void Shutdown();

    void BeginFrame(const Ref<Scene>& scene,
                    const SkinningFrameSnapshot&);

    void AddPasses(RenderGraph::Graph& graph, uint32_t frameIndex);

    std::optional<SkinnedDrawView> FindMesh(UUID meshEntity) const;

    void RecompileShaders() const;

    void Reset();
};

}