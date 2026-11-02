#pragma once

#include "AnimationEvaluator.hpp"
#include "SkinningTypes.hpp"
#include "Renderer/DescriptorAllocator.hpp"
#include "Renderer/Graph.hpp"
#include "Renderer/VulkanBuffer.hpp"
#include "Scene/Scene.hpp"
#include "VulkanContext.hpp"

#include <array>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Kerberos {

class Skeleton;
struct AnimatorComponent;

class AnimationSystem
{
public:
    void Initialize(const Owner<DescriptorAllocator>& allocator);
    void Shutdown();

    void BeginFrame(const Ref<Scene>& scene, uint32_t frameIndex, float dt, bool paused);
    void AddPasses(RenderGraph::Graph& graph, uint32_t frameIndex);

    const SkinningFrameSnapshot& GetSkinningSnapshot() const;

    vk::Buffer GetSkinMatricesBuffer(uint32_t frameIndex) const;
    vk::DeviceSize GetSkinMatricesBufferSize(uint32_t frameIndex) const;
    std::vector<vk::Buffer> GetSkinMatricesBufferHandles() const;
    std::vector<vk::DeviceSize> GetSkinMatricesBufferSizes() const;

    void RecompileShaders() const;
    void Reset();

private:
    struct ClipCacheEntry
    {
        ClipTrackCache Cache;
        const Skeleton* Skeleton = nullptr;
        AssetHandle ClipHandle = AssetHandle::Invalid();
        uint32_t JointCount = 0;
    };

    struct SkeletonInstanceState
    {
        SkeletonInstanceId Id = 0;
        UUID Entity = UUID::Invalid();
        AssetHandle SkeletonAsset = AssetHandle::Invalid();
        uint32_t FirstSkinMatrix = 0;
        uint32_t JointCount = 0;
        Ref<Skeleton> Skeleton = nullptr;
        SkeletonPose Pose;

        // Runtime animation state.
        UUID AnimatorEntity = UUID::Invalid();
        UUID EvaluatedAnimator = UUID::Invalid();
        int32_t EvaluatedClip = -1;
        const AnimationClip* EvaluatedClipPtr = nullptr;
        float EvaluatedTime = 0.0f;
        bool HasEvaluatedPose = false;

        uint64_t SkinMatrixVersion = 0;
        // Last pose version uploaded into each frame-in-flight slot. Lets a paused instance
        // initialize a frame slot that has not seen the current pose yet.
        std::array<uint64_t, VulkanContext::MaxFramesInFlight> FrameVersions{};
    };

    static constexpr uint32_t MaxSkinMatrixJoints = 262144; // ~12.5 MB of BoneMatrix data

    void AllocateSkinMatricesBuffer();
    static void ResolveAnimatorClips(AnimatorComponent& animator);
    static void AdvanceAnimators(const Ref<Scene>& scene, float dt, bool paused);
    void EvaluateSkeletonInstance(SkeletonInstanceState& instance,
                                  UUID animatorEntity,
                                  const AnimatorComponent* animator,
                                  uint32_t frameIndex);
    void UploadSkinMatrices(SkeletonInstanceState& instance, uint32_t frameIndex) const;
    const ClipTrackCache& GetClipTrackCache(const Ref<Skeleton>& skeleton, const Ref<AnimationClip>& clip);
    static BoneMatrix ToBoneMatrix(const glm::mat4& matrix);

private:
    DescriptorAllocator* m_DescriptorAllocator = nullptr;

    std::vector<VulkanBuffer> m_SkinMatricesBuffers;
    uint32_t m_NextSkinMatrix = 0;
    SkeletonInstanceId m_NextInstanceId = 0;

    std::unordered_map<UUID, SkeletonInstanceState> m_Instances;
    std::unordered_map<const AnimationClip*, ClipCacheEntry> m_ClipCaches;
    std::unordered_set<UUID> m_WarnedAnimators;
    std::unordered_set<UUID> m_WarnedSkeletons;
    std::vector<SkeletonFrame> m_FrameSkeletons;
    SkinningFrameSnapshot m_Snapshot{};
};

}
