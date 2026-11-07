#pragma once

#include "Assets/Asset.hpp"
#include "Renderer/Animation/AnimationEvaluator.hpp"
#include "Renderer/Animation/AnimationEventTypes.hpp"
#include "Renderer/Animation/Skeleton.hpp"

#include <memory>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Kerberos {

class Scene;
struct AnimatorComponent;

/// Playback state for one contributing clip. A cross-fade keeps two of these alive: the
/// incoming clip in Current and the outgoing clip (or an interrupted blended pose) in Previous.
struct ClipPlaybackState
{
    int32_t ClipIndex = -1;
    Ref<AnimationClip> Clip = nullptr;
    float Time = 0.0f;
    float LastEventTime = 0.0f;
    float Weight = 1.0f;
    std::vector<ActiveAnimationEvent> ActiveEvents;
};

struct AnimationInstance
{
    UUID SkeletonEntity = UUID::Invalid();
    UUID AnimatorEntity = UUID::Invalid();
    AssetHandle SkeletonAsset = AssetHandle::Invalid();
    Ref<Skeleton> Skeleton;
    uint32_t JointCount = 0;

    // AnimationPose.Local is the composed clip pose. 
    SkeletonPose AnimationPose;
    // The authoritative pose that consumers (joints, sockets, skinning, physics, IK) read. 
    // Physics/ragdoll blending will modify FinalPose after it has been derived from AnimationPose.
    SkeletonPose FinalPose;
    // scratch: sampled incoming clip
    SkeletonPose CurrentPose;
    // scratch: sampled outgoing clip or interrupted-pose snapshot
    SkeletonPose PreviousPose;

    uint64_t PoseVersion = 0;
    bool HasPosed = false;

    // Cross-fade state. Both clips own their clock and duration-event windows so an outgoing
    // clip keeps firing weighted events while it fades out.
    ClipPlaybackState Current;
    ClipPlaybackState Previous;
    bool HasPrevious = false;
    bool PreviousIsSnapshot = false;
    float FadeElapsed = 0.0f;
    float FadeDuration = 0.0f;

    // Playback state snapshot used to detect script-driven changes and pose invalidation.
    UUID LastEvaluatedAnimator = UUID::Invalid();
    uint32_t LastPlaybackRevision = 0;

    const AnimationClip* LastSampledClip = nullptr;
    const AnimationClip* LastSampledPreviousClip = nullptr;
    float LastSampledTime = 0.0f;
    float LastSampledPreviousTime = 0.0f;
    float LastSampledFadeWeight = -1.0f;
    bool LastSampledHadPrevious = false;
};

/// CPU-side animation runtime owned by a Scene.
///
/// The runtime resolves animators to skeletons, advances playback clocks, evaluates the
/// authoritative CPU pose, and publishes it for the renderer to upload. Physics, gameplay,
/// sockets, and future ragdoll/IK all consume the final pose rather than GPU readback.
class AnimationRuntime
{
public:
    explicit AnimationRuntime(Scene& scene);

    /// Resolves skeleton instances, advances playback clocks, applies cross-fades, and
    /// prepares event state. Must run before scripts so animation events can affect gameplay
    /// in the same frame.
    void AdvanceClocks(float dt, bool paused);

    /// Evaluates the current CPU pose. Called before events/scripts and again after scripts
    /// when playback state changed, so script-driven Play/CrossFade/SetTime still applies this
    /// frame.
    void EvaluatePose();

    /// Post-physics hook for future ragdoll blending, IK, and socket/attachment updates.
    void FinalizePose(float dt) const;

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
    void UpdateJointTransforms() const;
    void UpdateSockets() const;
    static void ResolveAnimatorClips(AnimatorComponent& animator);
    static Ref<AnimationClip> ResolveAnimatorClip(const AnimatorComponent& animator, int32_t clipIndex);

    void SyncAnimatorState(AnimationInstance& instance, AnimatorComponent& animator);
    void ApplyPendingCrossFade(AnimationInstance& instance,
                               AnimatorComponent& animator,
                               const Ref<AnimationClip>& newClip,
                               int32_t newClipIndex,
                               float duration,
                               bool restart);
    void SnapToActiveClip(AnimationInstance& instance,
                          const AnimatorComponent& animator,
                          const Ref<AnimationClip>& activeClip,
                          int32_t activeClipIndex);
    static void AdvanceCrossFade(AnimationInstance& instance, const AnimatorComponent& animator,
                                 float dt, bool paused);
    void ProcessClipEvents(AnimationInstance& instance, ClipPlaybackState& state,
                           const AnimatorComponent& animator, bool paused);
    void CloseActiveDurationEvents(const AnimationInstance& instance, ClipPlaybackState& state, float time);
    const ClipTrackCache& GetClipTrackCache(const Ref<Skeleton>& skeleton, const Ref<AnimationClip>& clip);

private:
    Scene* m_Scene = nullptr;

    std::vector<AnimationInstance> m_Instances;
    std::unordered_map<UUID, size_t> m_InstanceByEntity;
    std::unordered_map<const AnimationClip*, ClipCacheEntry> m_ClipCaches;
    std::unordered_set<UUID> m_WarnedAnimators;
    std::unordered_set<UUID> m_WarnedSkeletons;
    mutable std::unordered_set<UUID> m_WarnedJointBindings;
    std::vector<PendingDispatch> m_PendingDispatches;

    // Monotonic across instance resets so the renderer never mistakes a rebuilt instance for an
    // already-uploaded pose when the scene pointer stays the same (for example, scene reload).
    uint64_t m_NextPoseVersion = 1;
};

}
