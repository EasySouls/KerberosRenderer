#include "AnimationSystem.hpp"

#include "Assets/AssetManager.hpp"
#include "Renderer/Animation/Skeleton.hpp"
#include "Profiling/Profilers.hpp"
#include "Scene/Components/AnimationComponents.hpp"
#include "Scene/Entity.hpp"
#include "VulkanContext.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <unordered_set>

import Kerberos;

namespace Kerberos {

void AnimationSystem::Initialize(const Owner<DescriptorAllocator>& allocator)
{
    KBR_TRACY_FUNCTION();

    m_DescriptorAllocator = allocator.get();

    AllocateSkinMatricesBuffer();

    m_Snapshot.SkinMatrixBuffer = m_SkinMatricesBuffers[0].Handle;
}

void AnimationSystem::Shutdown()
{
    KBR_TRACY_FUNCTION();

    Reset();

    for (auto& buffer : m_SkinMatricesBuffers)
    {
        buffer.Reset();
    }
    m_SkinMatricesBuffers.clear();

    m_Snapshot = {};
}

void AnimationSystem::AllocateSkinMatricesBuffer()
{
    KBR_TRACY_FUNCTION();

    auto& context = VulkanContext::Get();

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = static_cast<VkDeviceSize>(MaxSkinMatrixJoints) * sizeof(BoneMatrix);
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
        VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

    for (int i = 0; i < VulkanContext::MaxFramesInFlight; ++i)
    {
        m_SkinMatricesBuffers.emplace_back();

        VkBuffer buffer = nullptr;
        if (vmaCreateBuffer(context.GetAllocator().get(),
                            &bufferInfo,
                            &allocInfo,
                            &buffer,
                            &m_SkinMatricesBuffers[i].Allocation,
                            nullptr) != VK_SUCCESS)
        {
            KBRAssert(false, "Failed to create skin matrices buffer");
            return;
        }

        m_SkinMatricesBuffers[i].Handle = vk::Buffer(buffer);
        vmaMapMemory(context.GetAllocator().get(), m_SkinMatricesBuffers[i].Allocation, &m_SkinMatricesBuffers[i].MappedData);
        m_SkinMatricesBuffers[i].Size = bufferInfo.size;
        m_SkinMatricesBuffers[i].DeviceAddress =
            context.GetDevice().getBufferAddress({ .buffer = m_SkinMatricesBuffers[i].Handle });

        context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkBuffer>(m_SkinMatricesBuffers[i].Handle)),
                                   vk::ObjectType::eBuffer,
                                   std::format("Animation Skin Matrices Buffer {}", i));
        context.SetObjectDebugName(m_SkinMatricesBuffers[i].Allocation, std::format("Animation Skin Matrices Buffer Allocation {}", i));

        std::memset(m_SkinMatricesBuffers[i].MappedData, 0, static_cast<size_t>(m_SkinMatricesBuffers[i].Size));
    }
}

void AnimationSystem::BeginFrame(const Ref<Scene>& scene, const uint32_t frameIndex, const float dt, const bool paused)
{
    KBR_TRACY_FUNCTION();

    m_FrameSkeletons.clear();

    if (!scene)
    {
        m_Snapshot.SkinMatrixBuffer = m_SkinMatricesBuffers[frameIndex].Handle;
        m_Snapshot.Skeletons = std::span<const SkeletonFrame>{};
        return;
    }

    AdvanceAnimators(scene, dt, paused);

    // Resolve which animator drives each skeleton entity. An animator may live on the skeleton
    // entity itself or reference another entity through SkeletonEntity.
    std::unordered_map<UUID, UUID> animatorBySkeleton;
    const auto animatorView = scene->m_Registry.view<AnimatorComponent>();
    for (const auto entity : animatorView)
    {
        const auto& animator = animatorView.get<AnimatorComponent>(entity);
        const UUID animatorUUID = scene->m_Registry.get<IDComponent>(entity).ID;

        UUID skeletonUUID = animator.SkeletonEntity;
        if (skeletonUUID.IsValid())
        {
            const Entity skeletonEntity = scene->GetEntityByUUID(skeletonUUID);
            if (!skeletonEntity || !skeletonEntity.HasComponent<SkeletonComponent>())
                skeletonUUID = UUID::Invalid();
        }

        if (!skeletonUUID.IsValid())
        {
            const Entity self{ entity, scene.get() };
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

    const auto skeletonView = scene->m_Registry.view<SkeletonComponent>();
    for (const auto entity : skeletonView)
    {
        auto& component = skeletonView.get<SkeletonComponent>(entity);

        if (!component.Skeleton && component.SkeletonAsset.IsValid())
            component.Skeleton = AssetManager::AsAsset<Skeleton>(component.SkeletonAsset);

        if (!component.Skeleton)
        {
            Log::CoreWarn("SkeletonComponent on entity {} has no resolvable Skeleton asset", static_cast<uint32_t>(entity));
            continue;
        }

        const UUID entityUUID = scene->m_Registry.get<IDComponent>(entity).ID;
        const uint32_t jointCount = static_cast<uint32_t>(component.Skeleton->GetJoints().size());

        auto it = m_Instances.find(entityUUID);
        if (it != m_Instances.end() &&
            (it->second.SkeletonAsset != component.SkeletonAsset ||
             it->second.Skeleton.get() != component.Skeleton.get() ||
             it->second.JointCount != jointCount))
        {
            // The skeleton asset was replaced or reloaded. 
            // The old slice is not reclaimed individually.
            m_Instances.erase(it);
            it = m_Instances.end();
        }

        if (it == m_Instances.end())
        {
            if (jointCount == 0 || m_NextSkinMatrix + jointCount > MaxSkinMatrixJoints)
            {
                Log::CoreError("Skin matrix buffer exhausted while creating skeleton instance for entity {}",
                               static_cast<uint32_t>(entity));
                continue;
            }

            SkeletonInstanceState instance{};
            instance.Id = m_NextInstanceId++;
            instance.Entity = entityUUID;
            instance.SkeletonAsset = component.SkeletonAsset;
            instance.Skeleton = component.Skeleton;
            instance.JointCount = jointCount;
            instance.FirstSkinMatrix = m_NextSkinMatrix;
            m_NextSkinMatrix += jointCount;

            it = m_Instances.emplace(entityUUID, std::move(instance)).first;
        }
        else
        {
            // Keep resolved refs fresh if the asset was reloaded.
            it->second.Skeleton = component.Skeleton;
            it->second.SkeletonAsset = component.SkeletonAsset;
        }

        liveSkeletons.insert(entityUUID);

        SkeletonInstanceState& instance = it->second;

        UUID animatorUUID = UUID::Invalid();
        const AnimatorComponent* animator = nullptr;

        const auto animatorIt = animatorBySkeleton.find(entityUUID);
        if (animatorIt != animatorBySkeleton.end())
        {
            animatorUUID = animatorIt->second;
            const Entity animatorEntity = scene->GetEntityByUUID(animatorUUID);
            if (animatorEntity && animatorEntity.HasComponent<AnimatorComponent>())
                animator = &animatorEntity.GetComponent<AnimatorComponent>();
        }

        EvaluateSkeletonInstance(instance, animatorUUID, animator, frameIndex);

        m_FrameSkeletons.push_back({
            .Id = instance.Id,
            .Entity = entityUUID,
            .FirstSkinMatrix = instance.FirstSkinMatrix,
            .JointCount = instance.JointCount,
            .SkinMatrixVersion = instance.SkinMatrixVersion,
        });
    }

    for (auto it = m_Instances.begin(); it != m_Instances.end();)
    {
        if (!liveSkeletons.contains(it->first))
        {
            // Slices are not reclaimed individually; Reset() returns all of them.
            it = m_Instances.erase(it);
        }
        else
        {
            ++it;
        }
    }

    m_Snapshot.SkinMatrixBuffer = m_SkinMatricesBuffers[frameIndex].Handle;
    m_Snapshot.Skeletons = std::span<const SkeletonFrame>(m_FrameSkeletons);
}

void AnimationSystem::ResolveAnimatorClips(AnimatorComponent& animator)
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

        if (!loadedClip || loadedClip->GetHandle() != handle)
            loadedClip = AssetManager::AsAsset<AnimationClip>(handle);
    }
}

void AnimationSystem::AdvanceAnimators(const Ref<Scene>& scene, const float dt, const bool paused)
{
    KBR_TRACY_FUNCTION();

    const auto animatorView = scene->m_Registry.view<AnimatorComponent>();
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
}

void AnimationSystem::EvaluateSkeletonInstance(SkeletonInstanceState& instance,
                                                const UUID animatorEntity,
                                                const AnimatorComponent* animator,
                                                const uint32_t frameIndex)
{
    KBR_TRACY_FUNCTION();

    instance.AnimatorEntity = animatorEntity;

    Ref<AnimationClip> activeClip = nullptr;
    int32_t activeClipIndex = -1;

    if (animator && animator->ActiveClip >= 0 &&
        static_cast<size_t>(animator->ActiveClip) < animator->LoadedClips.size())
    {
        activeClip = animator->LoadedClips[static_cast<size_t>(animator->ActiveClip)];
        if (activeClip)
            activeClipIndex = animator->ActiveClip;
    }

    if (activeClip && instance.Skeleton)
    {
        const float time = animator->CurrentTime;

        if (!instance.HasEvaluatedPose ||
            instance.EvaluatedClip != activeClipIndex ||
            instance.EvaluatedClipPtr != activeClip.get() ||
            instance.EvaluatedAnimator != animatorEntity ||
            instance.EvaluatedTime != time)
        {
            const ClipTrackCache& cache = GetClipTrackCache(instance.Skeleton, activeClip);
            EvaluatePose(*instance.Skeleton, *activeClip, cache, time, instance.Pose);

            instance.EvaluatedAnimator = animatorEntity;
            instance.EvaluatedClip = activeClipIndex;
            instance.EvaluatedClipPtr = activeClip.get();
            instance.EvaluatedTime = time;
            instance.HasEvaluatedPose = true;
            ++instance.SkinMatrixVersion;
        }
    }
    else if (!instance.HasEvaluatedPose || instance.EvaluatedClip >= 0)
    {
        // No animator or no resolvable active clip: keep the skeleton in its rest pose.
        EvaluateRestPose(*instance.Skeleton, instance.Pose);

        instance.EvaluatedAnimator = UUID::Invalid();
        instance.EvaluatedClip = -1;
        instance.EvaluatedClipPtr = nullptr;
        instance.EvaluatedTime = 0.0f;
        instance.HasEvaluatedPose = true;
        ++instance.SkinMatrixVersion;
    }

    if (instance.FrameVersions[frameIndex] != instance.SkinMatrixVersion)
        UploadSkinMatrices(instance, frameIndex);
}

void AnimationSystem::UploadSkinMatrices(SkeletonInstanceState& instance, const uint32_t frameIndex) const
{
    KBR_TRACY_FUNCTION();

    if (!instance.Skeleton || instance.Pose.SkinMatrices.size() != instance.JointCount)
        return;

    const VulkanBuffer& buffer = m_SkinMatricesBuffers[frameIndex];
    if (buffer.MappedData == nullptr)
        return;

    auto* destination = static_cast<std::byte*>(buffer.MappedData) +
        static_cast<size_t>(instance.FirstSkinMatrix) * sizeof(BoneMatrix);

    for (size_t jointIndex = 0; jointIndex < instance.Pose.SkinMatrices.size(); ++jointIndex)
    {
        const BoneMatrix bone = ToBoneMatrix(instance.Pose.SkinMatrices[jointIndex]);
        std::memcpy(destination + jointIndex * sizeof(BoneMatrix), &bone, sizeof(BoneMatrix));
    }

    const VkDeviceSize offset = static_cast<VkDeviceSize>(instance.FirstSkinMatrix) * sizeof(BoneMatrix);
    const VkDeviceSize size = static_cast<VkDeviceSize>(instance.Pose.SkinMatrices.size()) * sizeof(BoneMatrix);
    vmaFlushAllocation(VulkanContext::Get().GetAllocator().get(), buffer.Allocation, offset, size);

    instance.FrameVersions[frameIndex] = instance.SkinMatrixVersion;
}

const ClipTrackCache& AnimationSystem::GetClipTrackCache(const Ref<Skeleton>& skeleton,
                                                         const Ref<AnimationClip>& clip)
{
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

void AnimationSystem::AddPasses(RenderGraph::Graph& graph, const uint32_t frameIndex)
{
    // Pose evaluation and skin matrix upload happen on the CPU in BeginFrame. 
    // May be implement evaluation and precompute compute passes later.
    (void)graph;
    (void)frameIndex;
}

const SkinningFrameSnapshot& AnimationSystem::GetSkinningSnapshot() const
{
    return m_Snapshot;
}

vk::Buffer AnimationSystem::GetSkinMatricesBuffer(const uint32_t frameIndex) const
{
    KBRAssert(frameIndex < m_SkinMatricesBuffers.size(),"Invalid frame index for skin matrices buffer");
    return m_SkinMatricesBuffers[frameIndex].Handle;
}

vk::DeviceSize AnimationSystem::GetSkinMatricesBufferSize(const uint32_t frameIndex) const
{
    KBRAssert(frameIndex < m_SkinMatricesBuffers.size(), "Invalid frame index for skin matrices buffer");
    return m_SkinMatricesBuffers[frameIndex].Size;
}

std::vector<vk::Buffer> AnimationSystem::GetSkinMatricesBufferHandles() const
{
    std::vector<vk::Buffer> handles;
    handles.reserve(m_SkinMatricesBuffers.size());

    for (const VulkanBuffer& buffer : m_SkinMatricesBuffers)
        handles.push_back(buffer.Handle);

    return handles;
}

std::vector<vk::DeviceSize> AnimationSystem::GetSkinMatricesBufferSizes() const
{
    std::vector<vk::DeviceSize> sizes;
    sizes.reserve(m_SkinMatricesBuffers.size());

    for (const VulkanBuffer& buffer : m_SkinMatricesBuffers)
        sizes.push_back(buffer.Size);

    return sizes;
}

void AnimationSystem::RecompileShaders() const
{
}

void AnimationSystem::Reset()
{
    m_Instances.clear();
    m_ClipCaches.clear();
    m_WarnedAnimators.clear();
    m_WarnedSkeletons.clear();
    m_FrameSkeletons.clear();
    m_NextSkinMatrix = 0;
    m_NextInstanceId = 0;

    m_Snapshot.SkinMatrixBuffer = m_SkinMatricesBuffers.empty() ? nullptr : m_SkinMatricesBuffers[0].Handle;
    m_Snapshot.Skeletons = std::span<const SkeletonFrame>{};
}

BoneMatrix AnimationSystem::ToBoneMatrix(const glm::mat4& matrix)
{
    BoneMatrix result{};

    // glm matrices are column-major: row r is (matrix[0][r], matrix[1][r], matrix[2][r], matrix[3][r]).
    for (uint32_t column = 0; column < 4; ++column)
    {
        result.R0[column] = matrix[column][0];
        result.R1[column] = matrix[column][1];
        result.R2[column] = matrix[column][2];
    }

    return result;
}

}
