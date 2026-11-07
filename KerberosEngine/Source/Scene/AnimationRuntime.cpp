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
                // Close windows before dropping the old clip so gameplay resources release.
                CloseActiveDurationEvents(existing, existing.LastEventClip, existing.LastEventTime);

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
            loadedClip = nullptr;
            continue;
        }

        // Resolve when the handle changed, when the clip has not been loaded yet, or when the
        // asset manager evicted the cached artifact (e.g. after reimporting source metadata).
        if (!loadedClip || loadedClip->GetHandle() != handle || !AssetManager::IsAssetLoaded(handle))
            loadedClip = AssetManager::AsAsset<AnimationClip>(handle);
    }
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

        Ref<AnimationClip> activeClip = nullptr;
        if (animator.ActiveClip >= 0 &&
            static_cast<size_t>(animator.ActiveClip) < animator.LoadedClips.size())
        {
            activeClip = animator.LoadedClips[static_cast<size_t>(animator.ActiveClip)];
        }

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
            CloseActiveDurationEvents(instance, instance.LastEventClip, instance.LastEventTime);
            instance.LastEventClip = nullptr;
            continue;
        }

        const Entity animatorEntity = m_Scene->TryGetEntityByUUID(instance.AnimatorEntity);
        if (!animatorEntity || !animatorEntity.HasComponent<AnimatorComponent>())
        {
            CloseActiveDurationEvents(instance, instance.LastEventClip, instance.LastEventTime);
            instance.LastEventClip = nullptr;
            continue;
        }

        const AnimatorComponent& animator = animatorEntity.GetComponent<AnimatorComponent>();

        Ref<AnimationClip> activeClip = nullptr;
        if (animator.ActiveClip >= 0 &&
            static_cast<size_t>(animator.ActiveClip) < animator.LoadedClips.size())
        {
            activeClip = animator.LoadedClips[static_cast<size_t>(animator.ActiveClip)];
        }

        const AnimationClip* activeClipPtr = activeClip.get();
        const float currentTime = animator.CurrentTime;

        if (instance.LastEventClip.get() != activeClipPtr)
        {
            // Do not replay events from the previous clip, but close its active windows so
            // gameplay resources are released.
            CloseActiveDurationEvents(instance, instance.LastEventClip, currentTime);
            instance.LastEventClip = activeClip;
            instance.LastEventTime = currentTime;
            instance.LastPlaybackRevision = animator.PlaybackRevision;
            continue;
        }

        if (instance.LastPlaybackRevision != animator.PlaybackRevision)
        {
            CloseActiveDurationEvents(instance, activeClip, currentTime);
            instance.LastPlaybackRevision = animator.PlaybackRevision;
            instance.LastEventTime = currentTime;
            continue;
        }

        if (paused)
        {
            // Keep active windows across a pause. Resuming from the same time must not replay
            // missed events.
            instance.LastEventTime = currentTime;
            continue;
        }

        if (!activeClip)
        {
            CloseActiveDurationEvents(instance, instance.LastEventClip, currentTime);
            instance.LastEventClip = nullptr;
            instance.LastEventTime = currentTime;
            continue;
        }

        if (animator.PlaybackSpeed <= 0.0f)
        {
            // Reverse playback does not emit point events or reverse windows in this version.
            CloseActiveDurationEvents(instance, instance.LastEventClip, currentTime);
            instance.LastEventTime = currentTime;
            continue;
        }

        if (!animator.IsPlaying)
        {
            const float duration = activeClip->GetDuration();

            // Natural end of a non-looping clip: close windows so listeners can release effects.
            if (!animator.IsLooping && !instance.ActiveEvents.empty() &&
                (currentTime >= duration || currentTime <= 0.0f))
            {
                CloseActiveDurationEvents(instance, activeClip, currentTime);
            }

            instance.LastEventTime = currentTime;
            continue;
        }

        const float fromTime = instance.LastEventTime;
        const float toTime = currentTime;
        if (fromTime == toTime)
            continue;

        std::vector<AnimationEventHit> hits;
        CollectPointEvents(*activeClip, fromTime, toTime, activeClip->GetDuration(),
                           animator.IsLooping, instance.ClipWeight, hits);
        UpdateDurationEvents(*activeClip, fromTime, toTime, activeClip->GetDuration(),
                             animator.IsLooping, instance.ClipWeight, instance.ActiveEvents, hits);

        for (const AnimationEventHit& hit : hits)
        {
            m_PendingDispatches.push_back({
                .Entity = instance.AnimatorEntity,
                .Clip = activeClipPtr,
                .Event = hit.Event,
                .EventIndex = hit.EventIndex,
                .Phase = hit.Phase,
                .Weight = instance.ClipWeight,
                .Time = hit.Time,
            });
        }

        instance.LastEventClip = activeClip;
        instance.LastEventTime = toTime;
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

        const AnimatorComponent* animator = nullptr;
        if (instance.AnimatorEntity.IsValid())
        {
            const Entity animatorEntity = m_Scene->TryGetEntityByUUID(instance.AnimatorEntity);
            if (animatorEntity && animatorEntity.HasComponent<AnimatorComponent>())
                animator = &animatorEntity.GetComponent<AnimatorComponent>();
        }

        Ref<AnimationClip> activeClip = nullptr;
        int32_t activeClipIndex = -1;

        if (animator && animator->ActiveClip >= 0 &&
            static_cast<size_t>(animator->ActiveClip) < animator->LoadedClips.size())
        {
            activeClip = animator->LoadedClips[static_cast<size_t>(animator->ActiveClip)];
            if (activeClip)
                activeClipIndex = animator->ActiveClip;
        }

        const float sampledTime = animator ? animator->CurrentTime : 0.0f;
        const bool animatorChanged = instance.LastEvaluatedAnimator != instance.AnimatorEntity;

        if (activeClip)
        {
            if (!instance.HasPosed ||
                animatorChanged ||
                instance.LastClipIndex != activeClipIndex ||
                instance.LastClip != activeClip.get() ||
                instance.PreviousClockTime != sampledTime)
            {
                const ClipTrackCache& cache = GetClipTrackCache(instance.Skeleton, activeClip);
                Kerberos::EvaluatePose(*instance.Skeleton, *activeClip, cache, sampledTime, instance.Pose);

                instance.LastEvaluatedAnimator = instance.AnimatorEntity;
                instance.LastClipIndex = activeClipIndex;
                instance.LastClip = activeClip.get();
                instance.PreviousClockTime = sampledTime;
                instance.HasPosed = true;
                instance.PoseVersion = m_NextPoseVersion++;
            }
        }
        else if (!instance.HasPosed || instance.LastClipIndex >= 0 || animatorChanged)
        {
            // No animator or no resolvable active clip: keep the skeleton in its rest pose.
            EvaluateRestPose(*instance.Skeleton, instance.Pose);

            instance.LastEvaluatedAnimator = instance.AnimatorEntity;
            instance.LastClipIndex = -1;
            instance.LastClip = nullptr;
            instance.PreviousClockTime = sampledTime;
            instance.HasPosed = true;
            instance.PoseVersion = m_NextPoseVersion++;
        }
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
            instance.Pose.Global.size() != joints.size())
        {
            if (m_WarnedJointBindings.insert(instance.SkeletonEntity).second)
            {
                Log::CoreWarn("Skeleton entity {} cannot publish joint transforms: {} joint entities, "
                              "{} bones, {} posed globals",
                              instance.SkeletonEntity, skeletonComponent.JointEntities.size(),
                              joints.size(), instance.Pose.Global.size());
            }
            continue;
        }

        glm::mat4 baseWorld;
        if (const auto it = baseWorldBySkeleton.find(instance.SkeletonEntity);
            it != baseWorldBySkeleton.end())
        {
            baseWorld = it->second;
        }
        else
        {
            baseWorld = skeletonEntity.GetComponent<TransformComponent>().WorldTransform;

            int32_t skeletonJoint = -1;
            for (size_t jointIndex = 0; jointIndex < skeletonComponent.JointEntities.size(); ++jointIndex)
            {
                if (skeletonComponent.JointEntities[jointIndex] == instance.SkeletonEntity)
                {
                    skeletonJoint = static_cast<int32_t>(jointIndex);
                    break;
                }
            }

            if (skeletonJoint >= 0)
            {
                // The skeleton entity is a joint, so its world includes that joint's rest
                // transform. Remove it to recover the model space the pose is expressed in.
                std::vector<glm::mat4> restGlobals(joints.size(), glm::mat4(1.0f));
                for (size_t jointIndex = 0; jointIndex < joints.size(); ++jointIndex)
                {
                    const SkeletonJoint& joint = joints[jointIndex];
                    const glm::mat4 local = JointRestLocal(joint);
                    restGlobals[jointIndex] = joint.Parent >= 0
                        ? restGlobals[static_cast<size_t>(joint.Parent)] * local
                        : local;
                }
                baseWorld *= glm::inverse(restGlobals[static_cast<size_t>(skeletonJoint)]);
            }
        }

        std::vector<glm::mat4> jointWorlds(joints.size(), glm::mat4(1.0f));
        for (size_t jointIndex = 0; jointIndex < joints.size(); ++jointIndex)
        {
            jointWorlds[jointIndex] = baseWorld * instance.Pose.Global[jointIndex];

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
    return instance.HasPosed ? &instance.Pose : nullptr;
}

bool AnimationRuntime::TryGetJointGlobalTransform(const UUID skeletonEntity, const int32_t joint, glm::mat4& out) const
{
    const SkeletonPose* pose = FindPose(skeletonEntity);
    if (!pose || joint < 0 || static_cast<size_t>(joint) >= pose->Global.size())
        return false;

    out = pose->Global[static_cast<size_t>(joint)];
    return true;
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

void AnimationRuntime::CloseActiveDurationEvents(AnimationInstance& instance,
                                                 const Ref<AnimationClip>& clip,
                                                 const float time)
{
    KBR_TRACY_FUNCTION();

    if (!instance.ActiveEvents.empty() && clip)
    {
        const std::vector<AnimationEvent>& events = clip->GetEvents();
        const UUID target = instance.AnimatorEntity.IsValid()
            ? instance.AnimatorEntity
            : instance.SkeletonEntity;

        for (const ActiveAnimationEvent& window : instance.ActiveEvents)
        {
            if (window.EventIndex < 0 || static_cast<size_t>(window.EventIndex) >= events.size())
                continue;

            m_PendingDispatches.push_back({
                .Entity = target,
                .Clip = clip.get(),
                .Event = &events[static_cast<size_t>(window.EventIndex)],
                .EventIndex = window.EventIndex,
                .Phase = AnimationEventPhase::End,
                .Weight = instance.ClipWeight,
                .Time = time,
            });
        }
    }

    instance.ActiveEvents.clear();
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
