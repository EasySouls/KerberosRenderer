#include "AnimationRuntime.hpp"
#include "Scene.hpp"

#include "Assets/AssetManager.hpp"
#include "Profiling/Profilers.hpp"
#include "Scene/Components/AnimationComponents.hpp"
#include "Scene/Entity.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>

import Kerberos;

namespace Kerberos {

namespace
{
glm::mat4 JointRestLocal(const SkeletonJoint& joint)
{
    return glm::translate(glm::mat4(1.0f), joint.RestTranslation) *
        glm::toMat4(glm::normalize(joint.RestRotation)) *
        glm::scale(glm::mat4(1.0f), joint.RestScale);
}

// Propagates a joint's animated world transform through non-skeletal descendants. Joints,
// skinned meshes, and sockets are already driven by their own animation paths.
void UpdateAttachmentTransforms(Scene* scene, const Entity entity, const glm::mat4& parentWorld)
{
    if (!entity || !entity.HasComponent<TransformComponent>() ||
        entity.HasComponent<JointComponent>() ||
        entity.HasComponent<SkinnedMeshComponent>() ||
        entity.HasComponent<SocketComponent>())
    {
        return;
    }

    auto& transform = entity.GetComponent<TransformComponent>();
    transform.WorldTransform = parentWorld * transform.GetTransform();

    for (const Entity child : scene->GetChildren(entity))
        UpdateAttachmentTransforms(scene, child, transform.WorldTransform);
}

}

AnimationRuntime::AnimationRuntime(Scene& scene)
    : m_Scene(&scene)
{
}

void AnimationRuntime::SyncInstances()
{
    KBR_TRACY_FUNCTION();

    if (!m_Scene)
        return;

    // Resolve which animator drives each skeleton entity. An animator may live on the skeleton
    // entity itself or reference another entity through SkeletonEntity.
    std::unordered_map<UUID, UUID> animatorBySkeleton;
    const auto animatorView = m_Scene->m_Registry.view<AnimatorComponent>();
    for (const auto entity : animatorView)
    {
        const auto& animator = animatorView.get<AnimatorComponent>(entity);
        const UUID animatorUUID = m_Scene->m_Registry.get<IDComponent>(entity).ID;

        UUID skeletonUUID = animator.SkeletonEntity;
        if (skeletonUUID.IsValid())
        {
            const Entity skeletonEntity = m_Scene->GetEntityByUUID(skeletonUUID);
            if (!skeletonEntity || !skeletonEntity.HasComponent<SkeletonComponent>())
                skeletonUUID = UUID::Invalid();
        }

        if (!skeletonUUID.IsValid())
        {
            const Entity self{ entity, m_Scene };
            if (self.HasComponent<SkeletonComponent>())
                skeletonUUID = animatorUUID;
        }

        if (!skeletonUUID.IsValid())
        {
            if (m_WarnedAnimators.insert(animatorUUID).second)
            {
                Log::CoreWarn("AnimatorComponent on entity {} does not target a skeleton entity",
                              static_cast<uint32_t>(entity));
            }
            continue;
        }

        const auto [it, inserted] = animatorBySkeleton.emplace(skeletonUUID, animatorUUID);
        if (!inserted && m_WarnedSkeletons.insert(skeletonUUID).second)
        {
            Log::CoreWarn("Multiple AnimatorComponents target skeleton entity {}; using {}",
                          skeletonUUID, it->second);
        }
    }

    std::unordered_set<UUID> liveSkeletons;

    const auto skeletonView = m_Scene->m_Registry.view<SkeletonComponent>();
    for (const auto entity : skeletonView)
    {
        auto& component = skeletonView.get<SkeletonComponent>(entity);

        if (!component.Skeleton && component.SkeletonAsset.IsValid())
            component.Skeleton = AssetManager::AsAsset<Skeleton>(component.SkeletonAsset);

        if (!component.Skeleton)
        {
            Log::CoreWarn("SkeletonComponent on entity {} has no resolvable Skeleton asset",
                          static_cast<uint32_t>(entity));
            continue;
        }

        const UUID entityUUID = m_Scene->m_Registry.get<IDComponent>(entity).ID;
        const uint32_t jointCount = static_cast<uint32_t>(component.Skeleton->GetJoints().size());

        size_t instanceIndex = static_cast<size_t>(-1);
        const auto mapIt = m_InstanceByEntity.find(entityUUID);

        if (mapIt != m_InstanceByEntity.end())
        {
            instanceIndex = mapIt->second;
            AnimationInstance& existing = m_Instances[instanceIndex];

            const bool skeletonChanged =
                existing.SkeletonAsset != component.SkeletonAsset ||
                existing.Skeleton.get() != component.Skeleton.get() ||
                existing.JointCount != jointCount;

            if (skeletonChanged)
            {
                // Close windows before dropping the old clips so gameplay resources release.
                CloseActiveDurationEvents(existing, existing.Current, existing.Current.Time);
                CloseActiveDurationEvents(existing, existing.Previous, existing.Previous.Time);

                // Reset the runtime state but keep the vector slot. The renderer will detect the
                // skeleton change and rebuild its GPU slice.
                existing = AnimationInstance{};
            }

            existing.SkeletonEntity = entityUUID;
            existing.SkeletonAsset = component.SkeletonAsset;
            existing.Skeleton = component.Skeleton;
            existing.JointCount = jointCount;
        }
        else
        {
            AnimationInstance instance{};
            instance.SkeletonEntity = entityUUID;
            instance.SkeletonAsset = component.SkeletonAsset;
            instance.Skeleton = component.Skeleton;
            instance.JointCount = jointCount;

            m_Instances.push_back(std::move(instance));
            instanceIndex = m_Instances.size() - 1;
            m_InstanceByEntity[entityUUID] = instanceIndex;
        }

        if (const auto animatorIt = animatorBySkeleton.find(entityUUID); animatorIt != animatorBySkeleton.end())
            m_Instances[instanceIndex].AnimatorEntity = animatorIt->second;
        else
            m_Instances[instanceIndex].AnimatorEntity = UUID::Invalid();

        liveSkeletons.insert(entityUUID);
    }

    for (size_t index = 0; index < m_Instances.size();)
    {
        if (!liveSkeletons.contains(m_Instances[index].SkeletonEntity))
        {
            m_Instances[index] = std::move(m_Instances.back());
            m_Instances.pop_back();
        }
        else
        {
            ++index;
        }
    }

    m_InstanceByEntity.clear();
    for (size_t index = 0; index < m_Instances.size(); ++index)
        m_InstanceByEntity[m_Instances[index].SkeletonEntity] = index;
}

void AnimationRuntime::ResolveAnimatorClips(AnimatorComponent& animator)
{
    KBR_TRACY_FUNCTION();

    if (animator.LoadedClips.size() != animator.AnimationClips.size())
        animator.LoadedClips.resize(animator.AnimationClips.size());

    for (size_t clipIndex = 0; clipIndex < animator.AnimationClips.size(); ++clipIndex)
    {
        const AssetHandle handle = animator.AnimationClips[clipIndex];
        Ref<AnimationClip>& loadedClip = animator.LoadedClips[clipIndex];

        if (!handle.IsValid())
        {
            if (loadedClip && loadedClip->GetHandle().IsValid())
                loadedClip = nullptr;
            continue;
        }

        // Resolve when the handle changed, when the clip has not been loaded yet, or when the
        // asset manager evicted the cached artifact (e.g. after reimporting source metadata).
        if (!loadedClip || loadedClip->GetHandle() != handle || !AssetManager::IsAssetLoaded(handle))
            loadedClip = AssetManager::AsAsset<AnimationClip>(handle);
    }
}

Ref<AnimationClip> AnimationRuntime::ResolveAnimatorClip(const AnimatorComponent& animator, const int32_t clipIndex)
{
    if (clipIndex < 0 || static_cast<size_t>(clipIndex) >= animator.AnimationClips.size())
        return nullptr;

    const size_t index = static_cast<size_t>(clipIndex);
    if (index < animator.LoadedClips.size() && animator.LoadedClips[index])
        return animator.LoadedClips[index];

    const AssetHandle handle = animator.AnimationClips[index];
    return handle.IsValid() ? AssetManager::AsAsset<AnimationClip>(handle) : nullptr;
}

void AnimationRuntime::AdvanceClocks(const float dt, const bool paused)
{
    KBR_TRACY_FUNCTION();

    if (!m_Scene)
        return;

    // Clear first so interruption End events emitted while syncing/advancing survive to dispatch.
    m_PendingDispatches.clear();

    SyncInstances();

    const auto animatorView = m_Scene->m_Registry.view<AnimatorComponent>();
    for (const auto entity : animatorView)
    {
        auto& animator = animatorView.get<AnimatorComponent>(entity);

        ResolveAnimatorClips(animator);

        const Ref<AnimationClip> activeClip = ResolveAnimatorClip(animator, animator.ActiveClip);

        PlaybackTime playback{
            .Time = animator.CurrentTime,
            .IsPlaying = animator.IsPlaying,
        };

        AdvancePlaybackTime(playback,
                            activeClip ? activeClip->GetDuration() : 0.0f,
                            animator.IsLooping,
                            animator.PlaybackSpeed,
                            dt,
                            animator.IsPlaying && !paused);

        animator.CurrentTime = playback.Time;
        animator.IsPlaying = playback.IsPlaying;
    }

    for (AnimationInstance& instance : m_Instances)
    {
        if (!instance.AnimatorEntity.IsValid())
        {
            CloseActiveDurationEvents(instance, instance.Current, instance.Current.Time);
            CloseActiveDurationEvents(instance, instance.Previous, instance.Previous.Time);
            instance.HasPrevious = false;
            instance.PreviousIsSnapshot = false;
            continue;
        }

        const Entity animatorEntity = m_Scene->TryGetEntityByUUID(instance.AnimatorEntity);
        if (!animatorEntity || !animatorEntity.HasComponent<AnimatorComponent>())
        {
            CloseActiveDurationEvents(instance, instance.Current, instance.Current.Time);
            CloseActiveDurationEvents(instance, instance.Previous, instance.Previous.Time);
            instance.HasPrevious = false;
            instance.PreviousIsSnapshot = false;
            continue;
        }

        AnimatorComponent& animator = animatorEntity.GetComponent<AnimatorComponent>();

        // Apply cross-fade requests and script-driven seeks before advancing the fade. 
        // The authored clock was already advanced above for the animator's active clip.
        SyncAnimatorState(instance, animator);

        instance.Current.Time = animator.CurrentTime;

        AdvanceCrossFade(instance, animator, dt, paused);

        ProcessClipEvents(instance, instance.Current, animator, paused);
        if (instance.HasPrevious && !instance.PreviousIsSnapshot)
            ProcessClipEvents(instance, instance.Previous, animator, paused);

        // The outgoing clip contributes nothing once the fade completes. Close any remaining
        // duration windows so gameplay resources are released on the first completed frame.
        if (instance.HasPrevious &&
            ComputeCrossFadeWeight(instance.FadeElapsed, instance.FadeDuration) >= 1.0f)
        {
            CloseActiveDurationEvents(instance, instance.Previous, instance.Previous.Time);
            instance.Previous = ClipPlaybackState{};
            instance.HasPrevious = false;
            instance.PreviousIsSnapshot = false;
            instance.FadeElapsed = 0.0f;
            instance.FadeDuration = 0.0f;
            instance.Current.Weight = 1.0f;
        }
    }

    static const auto phaseRank = [](const AnimationEventPhase phase)
    {
        switch (phase)
        {
        case AnimationEventPhase::End: return 0;
        case AnimationEventPhase::Begin: return 1;
        case AnimationEventPhase::Tick: return 2;
        case AnimationEventPhase::Point: return 3;
        }
        return 4;
    };

    std::ranges::sort(m_PendingDispatches,
                      [](const DispatchedAnimationEvent& a, const DispatchedAnimationEvent& b)
                      {
                          if (a.Time != b.Time)
                              return a.Time < b.Time;

                          const int aRank = phaseRank(a.Phase);
                          const int bRank = phaseRank(b.Phase);
                          if (aRank != bRank)
                              return aRank < bRank;

                          if (a.Entity != b.Entity)
                              return static_cast<uint64_t>(a.Entity) < static_cast<uint64_t>(b.Entity);

                          return a.EventIndex < b.EventIndex;
                      });
}

void AnimationRuntime::SyncAnimatorState(AnimationInstance& instance, AnimatorComponent& animator)
{
    const Ref<AnimationClip> activeClip = ResolveAnimatorClip(animator, animator.ActiveClip);
    const int32_t activeIndex = activeClip ? animator.ActiveClip : -1;

    if (animator.PendingCrossFadeClip >= 0)
    {
        const int32_t requestedIndex = animator.PendingCrossFadeClip;
        const float requestedDuration = animator.PendingCrossFadeDuration;
        const bool requestedRestart = animator.PendingCrossFadeRestart;

        animator.PendingCrossFadeClip = -1;
        animator.PendingCrossFadeDuration = 0.0f;
        animator.PendingCrossFadeRestart = true;

        if (const Ref<AnimationClip> requestedClip = ResolveAnimatorClip(animator, requestedIndex))
        {
            ApplyPendingCrossFade(instance, animator, requestedClip, requestedIndex,
                                  requestedDuration, requestedRestart);
        }
        else
        {
            Log::CoreWarn("AnimatorComponent on entity {} requested an unresolvable cross-fade clip {}",
                          instance.AnimatorEntity, requestedIndex);
            SnapToActiveClip(instance, animator, activeClip, activeIndex);
        }

        // The request already defines the new playback epoch, so the revision check must not
        // close the outgoing clip's windows on the following clock advance.
        instance.LastPlaybackRevision = animator.PlaybackRevision;
        return;
    }

    const bool clipChanged = instance.Current.ClipIndex != activeIndex ||
        instance.Current.Clip.get() != activeClip.get();

    if (clipChanged)
    {
        // Direct Play/SetActiveClip changes snap instead of blending.
        SnapToActiveClip(instance, animator, activeClip, activeIndex);
        instance.LastPlaybackRevision = animator.PlaybackRevision;
        return;
    }

    if (instance.LastPlaybackRevision != animator.PlaybackRevision)
    {
        // Seek/stop/reverse: suppress events across the jump and release active windows.
        CloseActiveDurationEvents(instance, instance.Current, animator.CurrentTime);
        CloseActiveDurationEvents(instance, instance.Previous, instance.Previous.Time);
        instance.Current.LastEventTime = animator.CurrentTime;
        instance.Previous.LastEventTime = instance.Previous.Time;
        instance.LastPlaybackRevision = animator.PlaybackRevision;
    }
}

void AnimationRuntime::ApplyPendingCrossFade(AnimationInstance& instance,
                                             AnimatorComponent& animator,
                                             const Ref<AnimationClip>& newClip,
                                             const int32_t newClipIndex,
                                             const float duration,
                                             const bool restart)
{
    KBR_TRACY_FUNCTION();

    const int32_t oldIndex = instance.Current.ClipIndex;
    const Ref<AnimationClip> oldClip = instance.Current.Clip;
    const float oldTime = instance.Current.Time;

    const bool interrupting = instance.HasPrevious && instance.FadeDuration > 0.0f &&
        instance.FadeElapsed < instance.FadeDuration;

    if (interrupting)
    {
        // A second cross-fade during an in-flight one must start from the current blended pose,
        // not from the old raw clip. Capture the composed local pose as the outgoing snapshot.
        CloseActiveDurationEvents(instance, instance.Current, instance.Current.Time);
        CloseActiveDurationEvents(instance, instance.Previous, instance.Previous.Time);

        if (instance.Skeleton &&
            instance.AnimationPose.Local.size() == instance.Skeleton->GetJoints().size())
        {
            instance.PreviousPose.Local = instance.AnimationPose.Local;
        }
        else if (instance.Skeleton)
        {
            EvaluateRestPoseLocal(*instance.Skeleton, instance.PreviousPose);
        }
        else
        {
            instance.PreviousPose.Local.clear();
        }

        instance.Previous = ClipPlaybackState{};
        instance.PreviousIsSnapshot = true;
        instance.HasPrevious = true;
    }
    else if (oldClip)
    {
        // The outgoing clip keeps its clock and duration-event windows so it can still emit
        // weighted events while it fades out.
        instance.Previous = std::move(instance.Current);
        instance.PreviousIsSnapshot = false;
        instance.HasPrevious = true;
    }
    else
    {
        if (instance.Skeleton)
            EvaluateRestPoseLocal(*instance.Skeleton, instance.PreviousPose);
        else
            instance.PreviousPose.Local.clear();

        instance.Previous = ClipPlaybackState{};
        instance.PreviousIsSnapshot = true;
        instance.HasPrevious = true;
    }

    // A restart always begins at zero. Re-requesting the same clip without restart preserves
    // its current time so the fade is a no-op blend.
    float startTime = 0.0f;
    if (!restart && oldClip.get() == newClip.get() && oldIndex == newClipIndex)
        startTime = oldTime;

    instance.Current = ClipPlaybackState{};
    instance.Current.ClipIndex = newClipIndex;
    instance.Current.Clip = newClip;
    instance.Current.Time = startTime;
    instance.Current.LastEventTime = startTime;
    instance.Current.Weight = 0.0f;

    instance.FadeElapsed = 0.0f;
    instance.FadeDuration = std::max(0.0f, duration);

    animator.ActiveClip = newClipIndex;
    animator.CurrentTime = startTime;
    animator.PlaybackRevision++;

    if (instance.FadeDuration <= 0.0f)
    {
        // Zero-duration cross-fade means snap to the new clip.
        CloseActiveDurationEvents(instance, instance.Previous, instance.Previous.Time);
        instance.Previous = ClipPlaybackState{};
        instance.HasPrevious = false;
        instance.PreviousIsSnapshot = false;
        instance.Current.Weight = 1.0f;
    }
}

void AnimationRuntime::SnapToActiveClip(AnimationInstance& instance,
                                        const AnimatorComponent& animator,
                                        const Ref<AnimationClip>& activeClip,
                                        const int32_t activeClipIndex)
{
    CloseActiveDurationEvents(instance, instance.Current, animator.CurrentTime);
    CloseActiveDurationEvents(instance, instance.Previous, instance.Previous.Time);

    instance.Previous = ClipPlaybackState{};
    instance.HasPrevious = false;
    instance.PreviousIsSnapshot = false;
    instance.FadeElapsed = 0.0f;
    instance.FadeDuration = 0.0f;

    instance.Current = ClipPlaybackState{};
    instance.Current.ClipIndex = activeClipIndex;
    instance.Current.Clip = activeClip;
    instance.Current.Time = animator.CurrentTime;
    instance.Current.LastEventTime = animator.CurrentTime;
    instance.Current.Weight = 1.0f;
}

void AnimationRuntime::AdvanceCrossFade(AnimationInstance& instance,
                                         const AnimatorComponent& animator,
                                         const float dt,
                                         const bool paused)
{
    if (!instance.HasPrevious)
    {
        instance.Current.Weight = 1.0f;
        instance.FadeElapsed = 0.0f;
        instance.FadeDuration = 0.0f;
        return;
    }

    if (!instance.PreviousIsSnapshot && instance.Previous.Clip)
    {
        PlaybackTime previous{
            .Time = instance.Previous.Time,
            .IsPlaying = animator.IsPlaying,
        };

        AdvancePlaybackTime(previous,
                            instance.Previous.Clip->GetDuration(),
                            animator.IsLooping,
                            animator.PlaybackSpeed,
                            dt,
                            animator.IsPlaying && !paused);

        instance.Previous.Time = previous.Time;
    }

    // A scene pause freezes the blend. Pausing through the animator freezes the clip clocks but
    // lets an in-flight fade finish so the pose settles on the incoming clip.
    if (!paused && dt > 0.0f)
        instance.FadeElapsed += dt;

    const float weight = ComputeCrossFadeWeight(instance.FadeElapsed, instance.FadeDuration);
    instance.Current.Weight = weight;
    instance.Previous.Weight = 1.0f - weight;
}

void AnimationRuntime::ProcessClipEvents(AnimationInstance& instance,
                                          ClipPlaybackState& state,
                                          const AnimatorComponent& animator,
                                          const bool paused)
{
    KBR_TRACY_FUNCTION();

    if (!state.Clip)
    {
        CloseActiveDurationEvents(instance, state, state.Time);
        state.LastEventTime = state.Time;
        return;
    }

    const float currentTime = state.Time;

    if (paused)
    {
        // Keep active windows across a pause. Resuming from the same time must not replay
        // missed events.
        state.LastEventTime = currentTime;
        return;
    }

    if (state.Weight <= 1.0e-5f)
    {
        // A fully faded-out contributor must not keep firing point events or windows.
        CloseActiveDurationEvents(instance, state, currentTime);
        state.LastEventTime = currentTime;
        return;
    }

    if (animator.PlaybackSpeed <= 0.0f)
    {
        // Reverse playback does not emit point events or reverse windows in this version.
        CloseActiveDurationEvents(instance, state, currentTime);
        state.LastEventTime = currentTime;
        return;
    }

    if (!animator.IsPlaying)
    {
        const float duration = state.Clip->GetDuration();

        // Natural end of a non-looping clip: close windows so listeners can release effects.
        if (!animator.IsLooping && !state.ActiveEvents.empty() &&
            (currentTime >= duration || currentTime <= 0.0f))
        {
            CloseActiveDurationEvents(instance, state, currentTime);
        }

        state.LastEventTime = currentTime;
        return;
    }

    const float fromTime = state.LastEventTime;
    const float toTime = currentTime;
    if (fromTime == toTime)
        return;

    std::vector<AnimationEventHit> hits;
    CollectPointEvents(*state.Clip, fromTime, toTime, state.Clip->GetDuration(),
                       animator.IsLooping, state.Weight, hits);
    UpdateDurationEvents(*state.Clip, fromTime, toTime, state.Clip->GetDuration(),
                         animator.IsLooping, state.Weight, state.ActiveEvents, hits);

    for (const AnimationEventHit& hit : hits)
    {
        m_PendingDispatches.push_back({
            .Entity = instance.AnimatorEntity,
            .Clip = state.Clip.get(),
            .Event = hit.Event,
            .EventIndex = hit.EventIndex,
            .Phase = hit.Phase,
            .Weight = state.Weight,
            .Time = hit.Time,
        });
    }

    state.LastEventTime = toTime;
}

void AnimationRuntime::EvaluatePose()
{
    KBR_TRACY_FUNCTION();

    if (!m_Scene)
        return;

    // Re-sync here as well as in AdvanceClocks: the post-script evaluation must see skeletons
    // spawned or destroyed by scripts in the same frame.
    SyncInstances();

    for (AnimationInstance& instance : m_Instances)
    {
        if (!instance.Skeleton)
            continue;

        AnimatorComponent* animator = nullptr;
        if (instance.AnimatorEntity.IsValid())
        {
            const Entity animatorEntity = m_Scene->TryGetEntityByUUID(instance.AnimatorEntity);
            if (animatorEntity && animatorEntity.HasComponent<AnimatorComponent>())
                animator = &animatorEntity.GetComponent<AnimatorComponent>();
        }

        if (animator)
        {
            // Scripts may have requested a cross-fade or changed the active clip after the
            // pre-script evaluation. Apply it now so this frame's pose, events, physics, and
            // rendering all observe the same state.
            SyncAnimatorState(instance, *animator);
            instance.Current.Time = animator->CurrentTime;
        }
        else if (instance.Current.Clip || instance.Current.ClipIndex >= 0 || instance.HasPrevious)
        {
            CloseActiveDurationEvents(instance, instance.Current, instance.Current.Time);
            CloseActiveDurationEvents(instance, instance.Previous, instance.Previous.Time);
            instance.Current = ClipPlaybackState{};
            instance.Previous = ClipPlaybackState{};
            instance.HasPrevious = false;
            instance.PreviousIsSnapshot = false;
            instance.FadeElapsed = 0.0f;
            instance.FadeDuration = 0.0f;
        }

        const size_t jointCount = instance.Skeleton->GetJoints().size();

        // Sample the incoming clip (or the rest pose) into local space.
        if (instance.Current.Clip)
        {
            const ClipTrackCache& cache = GetClipTrackCache(instance.Skeleton, instance.Current.Clip);
            EvaluatePoseLocal(*instance.Skeleton, *instance.Current.Clip, cache,
                              instance.Current.Time, instance.CurrentPose);
        }
        else
        {
            EvaluateRestPoseLocal(*instance.Skeleton, instance.CurrentPose);
        }

        const bool hasPrevious = instance.HasPrevious;
        const float fadeWeight = hasPrevious
            ? ComputeCrossFadeWeight(instance.FadeElapsed, instance.FadeDuration)
            : 1.0f;

        if (hasPrevious && fadeWeight < 1.0f)
        {
            // Sample the outgoing clip, or reuse the interrupted-pose snapshot captured when a
            // second cross-fade replaced an in-flight blend.
            if (instance.PreviousIsSnapshot)
            {
                if (instance.PreviousPose.Local.size() != jointCount)
                    EvaluateRestPoseLocal(*instance.Skeleton, instance.PreviousPose);
            }
            else if (instance.Previous.Clip)
            {
                const ClipTrackCache& cache = GetClipTrackCache(instance.Skeleton, instance.Previous.Clip);
                EvaluatePoseLocal(*instance.Skeleton, *instance.Previous.Clip, cache,
                                  instance.Previous.Time, instance.PreviousPose);
            }
            else
            {
                EvaluateRestPoseLocal(*instance.Skeleton, instance.PreviousPose);
            }

            // fadeWeight is the incoming clip's weight, so blend previous -> current.
            BlendPose(instance.PreviousPose, instance.CurrentPose, fadeWeight, instance.AnimationPose);
        }
        else
        {
            instance.AnimationPose.Local = instance.CurrentPose.Local;
        }

        // Derive globals and skin matrices exactly once from the composed local pose. 
        // Physics and IK will later operate on FinalPose.
        // AnimationPose remains the clip-only layer.
        instance.FinalPose.Local = instance.AnimationPose.Local;
        ComputeGlobalAndSkinMatrices(*instance.Skeleton, instance.FinalPose);

        const AnimationClip* currentClip = instance.Current.Clip.get();
        const AnimationClip* previousClip =
            instance.PreviousIsSnapshot ? nullptr : instance.Previous.Clip.get();
        const float effectiveFadeWeight = hasPrevious ? fadeWeight : 1.0f;

        const bool changed =
            !instance.HasPosed ||
            instance.LastEvaluatedAnimator != instance.AnimatorEntity ||
            instance.LastSampledClip != currentClip ||
            instance.LastSampledPreviousClip != previousClip ||
            instance.LastSampledHadPrevious != instance.HasPrevious ||
            instance.LastSampledTime != instance.Current.Time ||
            instance.LastSampledPreviousTime != instance.Previous.Time ||
            instance.LastSampledFadeWeight != effectiveFadeWeight;

        if (changed)
            instance.PoseVersion = m_NextPoseVersion++;

        instance.LastEvaluatedAnimator = instance.AnimatorEntity;
        instance.LastSampledClip = currentClip;
        instance.LastSampledPreviousClip = previousClip;
        instance.LastSampledHadPrevious = instance.HasPrevious;
        instance.LastSampledTime = instance.Current.Time;
        instance.LastSampledPreviousTime = instance.Previous.Time;
        instance.LastSampledFadeWeight = effectiveFadeWeight;
        instance.HasPosed = true;
    }

    UpdateJointTransforms();
    UpdateSockets();
}

void AnimationRuntime::UpdateJointTransforms() const
{
    KBR_TRACY_FUNCTION();

    if (!m_Scene)
        return;

    // The pose's Global matrices live in the skinned mesh's model space. A skinned mesh bound to
    // the skeleton is the most reliable anchor for that space; scenes without one fall back to the
    // skeleton entity, correcting for the case where the skeleton entity is itself a joint.
    std::unordered_map<UUID, glm::mat4> baseWorldBySkeleton;
    const auto skinnedView = m_Scene->m_Registry.view<SkinnedMeshComponent, TransformComponent>();
    for (const auto entity : skinnedView)
    {
        const auto& skinned = skinnedView.get<SkinnedMeshComponent>(entity);
        if (!skinned.SkeletonEntity.IsValid() || baseWorldBySkeleton.contains(skinned.SkeletonEntity))
            continue;

        baseWorldBySkeleton.emplace(skinned.SkeletonEntity,
                                    skinnedView.get<TransformComponent>(entity).WorldTransform);
    }

    for (const AnimationInstance& instance : m_Instances)
    {
        if (!instance.HasPosed || !instance.Skeleton)
            continue;

        const Entity skeletonEntity = m_Scene->TryGetEntityByUUID(instance.SkeletonEntity);
        if (!skeletonEntity || !skeletonEntity.HasComponent<SkeletonComponent>() ||
            !skeletonEntity.HasComponent<TransformComponent>())
        {
            continue;
        }

        const auto& skeletonComponent = skeletonEntity.GetComponent<SkeletonComponent>();
        const auto& joints = instance.Skeleton->GetJoints();
        if (skeletonComponent.JointEntities.size() != joints.size() ||
            instance.FinalPose.Global.size() != joints.size())
        {
            if (m_WarnedJointBindings.insert(instance.SkeletonEntity).second)
            {
                Log::CoreWarn("Skeleton entity {} cannot publish joint transforms: {} joint entities, "
                              "{} bones, {} posed globals",
                              instance.SkeletonEntity, skeletonComponent.JointEntities.size(),
                              joints.size(), instance.FinalPose.Global.size());
            }
            continue;
        }

        glm::mat4 baseWorld;
        if (const auto it = baseWorldBySkeleton.find(instance.SkeletonEntity);
            it != baseWorldBySkeleton.end())
        {
            baseWorld = it->second;
        }
        else if (!TryGetSkeletonWorldTransform(instance.SkeletonEntity, baseWorld))
        {
            continue;
        }

        std::vector<glm::mat4> jointWorlds(joints.size(), glm::mat4(1.0f));
        for (size_t jointIndex = 0; jointIndex < joints.size(); ++jointIndex)
        {
            jointWorlds[jointIndex] = baseWorld * instance.FinalPose.Global[jointIndex];

            const Entity jointEntity = m_Scene->TryGetEntityByUUID(skeletonComponent.JointEntities[jointIndex]);
            if (!jointEntity || !jointEntity.HasComponent<TransformComponent>())
                continue;

            jointEntity.GetComponent<TransformComponent>().WorldTransform = jointWorlds[jointIndex];
        }

        // Drive rigid attachments (weapons, props, garment pieces) parented under joint entities.
        for (size_t jointIndex = 0; jointIndex < joints.size(); ++jointIndex)
        {
            const Entity jointEntity = m_Scene->TryGetEntityByUUID(skeletonComponent.JointEntities[jointIndex]);
            if (!jointEntity)
                continue;

            for (const Entity child : m_Scene->GetChildren(jointEntity))
                UpdateAttachmentTransforms(m_Scene, child, jointWorlds[jointIndex]);
        }
    }
}

void AnimationRuntime::UpdateSockets() const
{
    KBR_TRACY_FUNCTION();

    if (!m_Scene)
        return;

    const auto view = m_Scene->m_Registry.view<SocketComponent, TransformComponent>();
    for (const auto entity : view)
    {
        auto& socket = view.get<SocketComponent>(entity);
        auto& transform = view.get<TransformComponent>(entity);

        if (!socket.SkeletonEntity.IsValid() || socket.JointName.empty())
            continue;

        const SkeletonPose* pose = FindPose(socket.SkeletonEntity);
        if (!pose)
            continue;

        const Entity skeletonEntity = m_Scene->TryGetEntityByUUID(socket.SkeletonEntity);
        if (!skeletonEntity || !skeletonEntity.HasComponent<SkeletonComponent>() ||
            !skeletonEntity.HasComponent<TransformComponent>())
        {
            continue;
        }

        const SkeletonComponent& skeletonComponent = skeletonEntity.GetComponent<SkeletonComponent>();
        if (!skeletonComponent.Skeleton)
            continue;

        const int32_t jointIndex = skeletonComponent.Skeleton->GetJointIndex(socket.JointName);
        if (jointIndex < 0 || static_cast<size_t>(jointIndex) >= pose->Global.size())
            continue;

        const glm::mat4 localOffset =
            glm::translate(glm::mat4(1.0f), socket.LocalTranslation) *
            glm::toMat4(glm::normalize(glm::quat(socket.LocalRotation))) *
            glm::scale(glm::mat4(1.0f), socket.LocalScale);

        // Joint transforms are published by UpdateJointTransforms, which also accounts for the
        // skeleton entity itself being a joint (a common glTF layout).
        glm::mat4 jointWorld = skeletonEntity.GetComponent<TransformComponent>().WorldTransform *
            pose->Global[static_cast<size_t>(jointIndex)];
        if (static_cast<size_t>(jointIndex) < skeletonComponent.JointEntities.size())
        {
            const Entity jointEntity =
                m_Scene->TryGetEntityByUUID(skeletonComponent.JointEntities[static_cast<size_t>(jointIndex)]);
            if (jointEntity && jointEntity.HasComponent<TransformComponent>())
                jointWorld = jointEntity.GetComponent<TransformComponent>().WorldTransform;
        }

        const glm::mat4 socketWorld = jointWorld * localOffset;

        // Socket entities are derived transforms: keep the authored local TRS untouched and
        // update only the world transform plus descendants. 
        // UpdateChildTransforms updates its
        // own entity as well, so call it on children rather than on the socket entity.
        transform.WorldTransform = socketWorld;
        const Entity socketEntity{ entity, m_Scene };
        for (const Entity child : m_Scene->GetChildren(socketEntity))
            m_Scene->UpdateChildTransforms(child, socketWorld);
    }
}

void AnimationRuntime::FinalizePose([[maybe_unused]] const float dt) const
{
    KBR_TRACY_FUNCTION();

    // Reserved for post-physics work: ragdoll blending, inverse kinematics, root motion, and
    // socket/attachment updates. The renderer uploads whatever pose is final after this call.
    UpdateJointTransforms();
    UpdateSockets();
}

void AnimationRuntime::Reset()
{
    m_Instances.clear();
    m_InstanceByEntity.clear();
    m_ClipCaches.clear();
    m_WarnedAnimators.clear();
    m_WarnedSkeletons.clear();
    m_WarnedJointBindings.clear();
    m_PendingDispatches.clear();
}

const std::vector<AnimationInstance>& AnimationRuntime::GetInstances() const
{
    return m_Instances;
}

const SkeletonPose* AnimationRuntime::FindPose(const UUID skeletonEntity) const
{
    KBR_TRACY_FUNCTION();

    const auto it = m_InstanceByEntity.find(skeletonEntity);
    if (it == m_InstanceByEntity.end())
        return nullptr;

    const AnimationInstance& instance = m_Instances[it->second];
    return instance.HasPosed ? &instance.FinalPose : nullptr;
}

bool AnimationRuntime::TryGetJointGlobalTransform(const UUID skeletonEntity, const int32_t joint, glm::mat4& out) const
{
    const SkeletonPose* pose = FindPose(skeletonEntity);
    if (!pose || joint < 0 || static_cast<size_t>(joint) >= pose->Global.size())
        return false;

    out = pose->Global[static_cast<size_t>(joint)];
    return true;
}

bool AnimationRuntime::TryGetSkeletonWorldTransform(const UUID skeletonEntityId, glm::mat4& out) const
{
    if (!m_Scene)
        return false;

    const Entity skeletonEntity = m_Scene->TryGetEntityByUUID(skeletonEntityId);
    if (!skeletonEntity || !skeletonEntity.HasComponent<SkeletonComponent>() ||
        !skeletonEntity.HasComponent<TransformComponent>())
    {
        return false;
    }

    const auto& skeletonComponent = skeletonEntity.GetComponent<SkeletonComponent>();
    if (!skeletonComponent.Skeleton)
        return false;

    // Anchor to a skinned mesh bound to this skeleton when available.
    const auto skinnedView = m_Scene->m_Registry.view<SkinnedMeshComponent, TransformComponent>();
    for (const auto entity : skinnedView)
    {
        const auto& skinned = skinnedView.get<SkinnedMeshComponent>(entity);
        if (skinned.SkeletonEntity == skeletonEntityId)
        {
            out = skinnedView.get<TransformComponent>(entity).WorldTransform;
            return true;
        }
    }

    out = skeletonEntity.GetComponent<TransformComponent>().WorldTransform;

    // The skeleton entity may itself be a joint. It's world transform then includes that joint's
    // rest transform. Remove it to recover the model space the pose globals are expressed in.
    const auto& joints = skeletonComponent.Skeleton->GetJoints();
    int32_t skeletonJoint = -1;
    for (size_t jointIndex = 0; jointIndex < skeletonComponent.JointEntities.size(); ++jointIndex)
    {
        if (skeletonComponent.JointEntities[jointIndex] == skeletonEntityId)
        {
            skeletonJoint = static_cast<int32_t>(jointIndex);
            break;
        }
    }

    if (skeletonJoint >= 0)
    {
        std::vector<glm::mat4> restGlobals(joints.size(), glm::mat4(1.0f));
        for (size_t jointIndex = 0; jointIndex < joints.size(); ++jointIndex)
        {
            const SkeletonJoint& joint = joints[jointIndex];
            const glm::mat4 local = JointRestLocal(joint);
            restGlobals[jointIndex] = joint.Parent >= 0
                ? restGlobals[static_cast<size_t>(joint.Parent)] * local
                : local;
        }
        out *= glm::inverse(restGlobals[static_cast<size_t>(skeletonJoint)]);
    }

    return true;
}

void AnimationRuntime::ApplyRagdollPose(const UUID skeletonEntity,
                                        const std::vector<JointPose>& physicsLocal,
                                        const float weight)
{
    KBR_TRACY_FUNCTION();

    if (!m_Scene)
        return;

    const auto it = m_InstanceByEntity.find(skeletonEntity);
    if (it == m_InstanceByEntity.end())
        return;

    AnimationInstance& instance = m_Instances[it->second];
    if (!instance.Skeleton || !instance.HasPosed)
        return;

    const size_t jointCount = instance.Skeleton->GetJoints().size();
    if (physicsLocal.size() != jointCount || instance.AnimationPose.Local.size() != jointCount)
        return;

    const float physicsWeight = std::clamp(weight, 0.0f, 1.0f);
    if (physicsWeight <= 0.0f)
        return;

    if (instance.PhysicsPose.Local.size() != jointCount)
        instance.PhysicsPose.Local.resize(jointCount);

    // Only bump the pose version when the physics contribution actually changes. A sleeping
    // ragdoll still blends every frame (EvaluatePose reset FinalPose to the animation layer),
    // but the renderer skips the upload while the result is unchanged.
    bool changed = instance.LastRagdollWeight != physicsWeight;
    for (size_t jointIndex = 0; jointIndex < jointCount; ++jointIndex)
    {
        const JointPose& sampled = physicsLocal[jointIndex];
        JointPose& cached = instance.PhysicsPose.Local[jointIndex];

        if (!changed)
        {
            changed =
                glm::distance2(cached.Translation, sampled.Translation) > 1.0e-8f ||
                glm::distance2(cached.Scale, sampled.Scale) > 1.0e-8f ||
                std::abs(glm::dot(cached.Rotation, sampled.Rotation)) < 1.0f - 1.0e-6f;
        }

        cached = sampled;
    }

    instance.LastRagdollWeight = physicsWeight;

    BlendPose(instance.AnimationPose, instance.PhysicsPose, physicsWeight, instance.FinalPose);
    ComputeGlobalAndSkinMatrices(*instance.Skeleton, instance.FinalPose);

    if (changed)
        instance.PoseVersion = m_NextPoseVersion++;
}

bool AnimationRuntime::TryGetMarkerTime(const UUID skeletonEntity, const std::string_view name, float& outTime) const
{
    // Marker data is added with the animation event authoring milestone. The query surface is
    // stable so gameplay/editor code can rely on it once clip markers exist.
    (void)skeletonEntity;
    (void)name;
    (void)outTime;
    return false;
}

bool AnimationRuntime::GetNextMarker(const UUID skeletonEntity, const float afterTime, AnimationMarker& out) const
{
    (void)skeletonEntity;
    (void)afterTime;
    (void)out;
    return false;
}

const ClipTrackCache& AnimationRuntime::GetClipTrackCache(const Ref<Skeleton>& skeleton,
                                                          const Ref<AnimationClip>& clip)
{
    KBR_TRACY_FUNCTION();

    const uint32_t jointCount = static_cast<uint32_t>(skeleton->GetJoints().size());
    const Skeleton* skeletonPtr = skeleton.get();
    const AssetHandle clipHandle = clip->GetHandle();

    const auto it = m_ClipCaches.find(clip.get());
    if (it != m_ClipCaches.end() &&
        it->second.Skeleton == skeletonPtr &&
        it->second.ClipHandle == clipHandle &&
        it->second.JointCount == jointCount)
    {
        return it->second.Cache;
    }

    ClipCacheEntry entry{};
    entry.Cache = BuildClipTrackCache(*skeleton, *clip);
    entry.Skeleton = skeletonPtr;
    entry.ClipHandle = clipHandle;
    entry.JointCount = jointCount;

    if (entry.Cache.IgnoredTrackCount > 0)
    {
        Log::CoreWarn("Animation clip '{}' has {} track(s) that do not match a {}-joint skeleton",
                      clip->GetName(), entry.Cache.IgnoredTrackCount, jointCount);
    }

    auto [insertedIt, inserted] = m_ClipCaches.insert_or_assign(clip.get(), std::move(entry));
    return insertedIt->second.Cache;
}

void AnimationRuntime::CloseActiveDurationEvents(const AnimationInstance& instance,
                                                 ClipPlaybackState& state,
                                                 const float time)
{
    KBR_TRACY_FUNCTION();

    if (!state.ActiveEvents.empty() && state.Clip)
    {
        const std::vector<AnimationEvent>& events = state.Clip->GetEvents();
        const UUID target = instance.AnimatorEntity.IsValid()
            ? instance.AnimatorEntity
            : instance.SkeletonEntity;

        for (const ActiveAnimationEvent& window : state.ActiveEvents)
        {
            if (window.EventIndex < 0 || static_cast<size_t>(window.EventIndex) >= events.size())
                continue;

            m_PendingDispatches.push_back({
                .Entity = target,
                .Clip = state.Clip.get(),
                .Event = &events[static_cast<size_t>(window.EventIndex)],
                .EventIndex = window.EventIndex,
                .Phase = AnimationEventPhase::End,
                .Weight = state.Weight,
                .Time = time,
            });
        }
    }

    state.ActiveEvents.clear();
}

const std::vector<AnimationRuntime::PendingDispatch>& AnimationRuntime::GetPendingDispatches() const
{
    return m_PendingDispatches;
}

void AnimationRuntime::ClearPendingDispatches()
{
    m_PendingDispatches.clear();
}

} // namespace Kerberos
