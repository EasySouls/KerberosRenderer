#pragma once

#include "Assets/Asset.hpp"
#include "Renderer/Animation/AnimationEvaluator.hpp"
#include "Renderer/Animation/AnimationEventTypes.hpp"
#include "Renderer/Animation/Skeleton.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Kerberos {

class Scene;
struct AnimatorComponent;

struct AnimationInstance
{
    UUID SkeletonEntity = UUID::Invalid();
    UUID AnimatorEntity = UUID::Invalid();
    AssetHandle SkeletonAsset = AssetHandle::Invalid();
    Ref<Skeleton> Skeleton;
    uint32_t JointCount = 0;

    SkeletonPose Pose; // local/global/skin
    uint64_t PoseVersion = 0;
    bool HasPosed = false;

    // Playback state snapshot

    UUID LastEvaluatedAnimator = UUID::Invalid();
    int32_t LastClipIndex = -1;
    const AnimationClip* LastClip = nullptr;
    float PreviousClockTime = 0.0f;             // Last sampled pose time, for pose invalidation
    float LastEventTime = 0.0f;                 // Last time events were processed
    // Keep a reference so an interrupted duration event can still be closed after a clip reload.
    Ref<AnimationClip> LastEventClip = nullptr;
    uint32_t LastPlaybackRevision = 0;

    // Duration-event state
    std::vector<ActiveAnimationEvent> ActiveEvents;

    // Future blending
    float ClipWeight = 1.0f;
};

/// CPU-side animation runtime owned by a Scene.
///
/// The runtime resolves animators to skeletons, advances playback clocks, evaluates the
/// authoritative CPU pose, and publishes it for the renderer to upload. Physics, gameplay,
/// sockets, and future ragdoll/IK all consume this pose rather than GPU readback.
class AnimationRuntime
{
public:
    explicit AnimationRuntime(Scene& scene);

    /// Resolves skeleton instances, advances playback clocks, and prepares event state.
    /// Must run before scripts so animation events can affect gameplay in the same frame.
    void AdvanceClocks(float dt, bool paused);

    /// Evaluates the current CPU pose. Called before events/scripts and again after scripts
    /// when playback state changed, so script-driven Play/SetTime still applies this frame.
    void EvaluatePose();

    /// Post-physics hook for future ragdoll blending, IK, and socket/attachment updates.
    void FinalizePose(float dt);

    void Reset();

    const std::vector<AnimationInstance>& GetInstances() const;
    const SkeletonPose* FindPose(UUID skeletonEntity) const;
    bool TryGetJointGlobalTransform(UUID skeletonEntity, int32_t joint, glm::mat4& out) const;

    bool TryGetMarkerTime(UUID skeletonEntity, std::string_view name, float& outTime) const;
    bool GetNextMarker(UUID skeletonEntity, float afterTime, AnimationMarker& out) const;

    using PendingDispatch = DispatchedAnimationEvent;

    const std::vector<PendingDispatch>& GetPendingDispatches() const;
    void ClearPendingDispatches();

private:
    struct ClipCacheEntry
    {
        ClipTrackCache Cache;
        const Skeleton* Skeleton = nullptr;
        AssetHandle ClipHandle = AssetHandle::Invalid();
        uint32_t JointCount = 0;
    };

    void SyncInstances();
    static void ResolveAnimatorClips(AnimatorComponent& animator);
    void CloseActiveDurationEvents(AnimationInstance& instance, const Ref<AnimationClip>& clip, float time);
    const ClipTrackCache& GetClipTrackCache(const Ref<Skeleton>& skeleton, const Ref<AnimationClip>& clip);

private:
    Scene* m_Scene = nullptr;

    std::vector<AnimationInstance> m_Instances;
    std::unordered_map<UUID, size_t> m_InstanceByEntity;
    std::unordered_map<const AnimationClip*, ClipCacheEntry> m_ClipCaches;
    std::unordered_set<UUID> m_WarnedAnimators;
    std::unordered_set<UUID> m_WarnedSkeletons;
    std::vector<PendingDispatch> m_PendingDispatches;

    // Monotonic across instance resets so the renderer never mistakes a rebuilt instance for an
    // already-uploaded pose when the scene pointer stays the same (for example, scene reload).
    uint64_t m_NextPoseVersion = 1;
};

}
