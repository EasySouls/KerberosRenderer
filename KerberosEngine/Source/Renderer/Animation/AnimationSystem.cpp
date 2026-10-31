#include "AnimationSystem.hpp"

#include "Assets/AssetManager.hpp"
#include "Renderer/Animation/Skeleton.hpp"
#include "Profiling/Profilers.hpp"
#include "Scene/Components/AnimationComponents.hpp"
#include "Scene/Entity.hpp"
#include "VulkanContext.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <cstring>
#include <unordered_set>
#include <format>

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

    std::unordered_set<UUID> liveSkeletons;

    const auto view = scene->m_Registry.view<SkeletonComponent>();
    for (const auto entity : view)
    {
        auto& component = view.get<SkeletonComponent>(entity);

        if (!component.Skeleton && component.SkeletonAsset.IsValid())
            component.Skeleton = AssetManager::AsAsset<Skeleton>(component.SkeletonAsset);

        if (!component.Skeleton)
        {
            Log::CoreWarn("SkeletonComponent on entity {} has no resolvable Skeleton asset", static_cast<uint32_t>(entity));
            continue;
        }

        const UUID entityUUID = scene->m_Registry.get<IDComponent>(entity).ID;

        auto it = m_Instances.find(entityUUID);
        if (it == m_Instances.end())
        {
            SkeletonInstanceState instance{};
            instance.Id = m_NextInstanceId++;
            instance.Entity = entityUUID;
            instance.SkeletonAsset = component.SkeletonAsset;
            instance.Skeleton = component.Skeleton;
            instance.JointCount = static_cast<uint32_t>(component.Skeleton->GetJoints().size());
            instance.FirstSkinMatrix = m_NextSkinMatrix;

            if (instance.JointCount == 0 || m_NextSkinMatrix + instance.JointCount > MaxSkinMatrixJoints)
            {
                Log::CoreError("Skin matrix buffer exhausted while creating skeleton instance for entity {}",
                               static_cast<uint32_t>(entity));
                continue;
            }

            m_NextSkinMatrix += instance.JointCount;
            WriteBindPose(instance, frameIndex);
            instance.SkinMatrixVersion = 1;

            it = m_Instances.emplace(entityUUID, instance).first;
        }
        else
        {
            // Keep resolved refs fresh if the asset was reloaded.
            it->second.Skeleton = component.Skeleton;
        }

        liveSkeletons.insert(entityUUID);

        m_FrameSkeletons.push_back({
            .Id = it->second.Id,
            .Entity = entityUUID,
            .FirstSkinMatrix = it->second.FirstSkinMatrix,
            .JointCount = it->second.JointCount,
            .SkinMatrixVersion = it->second.SkinMatrixVersion,
        });
    }

    for (auto it = m_Instances.begin(); it != m_Instances.end();)
    {
        if (!liveSkeletons.contains(it->first))
        {
            // Slices are not reclaimed individually for the bind-pose milestone; Reset() returns all of them.
            it = m_Instances.erase(it);
        }
        else
        {
            ++it;
        }
    }

    m_Snapshot.SkinMatrixBuffer = m_SkinMatricesBuffers[frameIndex].Handle;
    m_Snapshot.Skeletons = std::span<const SkeletonFrame>(m_FrameSkeletons);

    const auto animatorView = scene->m_Registry.view<AnimatorComponent>();
    for (const auto entity : animatorView)
    {
        auto& animator = animatorView.get<AnimatorComponent>(entity);
        
        if (animator.IsPlaying && !paused)
        {
            animator.CurrentTime += dt * animator.PlaybackSpeed;
        }

        const Ref<AnimationClip> activeClip =
            (animator.ActiveClip >= 0 && static_cast<size_t>(animator.ActiveClip) < animator.LoadedClips.size())
                ? animator.LoadedClips[static_cast<size_t>(animator.ActiveClip)]
                : nullptr;

        const float duration = activeClip ? activeClip->GetDuration() : 0.0f;

        if (duration >= 0.0f)
        {
            if (animator.IsLooping)
            {
                animator.CurrentTime = std::fmod(animator.CurrentTime, duration);
                if (animator.CurrentTime < 0.0f)
                {
                    animator.CurrentTime += duration;
                }
            }
            else
            {
                animator.CurrentTime = std::clamp(animator.CurrentTime, 0.0f, duration);
                if (animator.CurrentTime >= duration)
                {
                    animator.IsPlaying = false;
                }
            }
        }
    }
}

void AnimationSystem::WriteBindPose(const SkeletonInstanceState& instance, const uint32_t frameIndex)
{
    KBR_TRACY_FUNCTION();

    if (!instance.Skeleton || m_SkinMatricesBuffers[frameIndex].MappedData == nullptr)
        return;

    const auto& joints = instance.Skeleton->GetJoints();
    auto* destination = static_cast<std::byte*>(m_SkinMatricesBuffers[frameIndex].MappedData) +
        static_cast<size_t>(instance.FirstSkinMatrix) * sizeof(BoneMatrix);

    // Bind pose bone matrices are jointGlobal * inverseBind. With consistent glTF
    // rest transforms and inverse-bind matrices this evaluates to identity, but
    // computing it from the rest pose keeps the path correct for skeletal data
    // whose bind pose is not the model-space origin.
    std::vector<glm::mat4> globalTransforms(joints.size(), glm::mat4(1.0f));
    for (size_t i = 0; i < joints.size(); ++i)
    {
        const SkeletonJoint& joint = joints[i];
        const glm::mat4 local =
            glm::translate(glm::mat4(1.0f), joint.RestTranslation) *
            glm::toMat4(joint.RestRotation) *
            glm::scale(glm::mat4(1.0f), joint.RestScale);
        const glm::mat4 parent = joint.Parent >= 0 && static_cast<size_t>(joint.Parent) < i
            ? globalTransforms[static_cast<size_t>(joint.Parent)]
            : glm::mat4(1.0f);
        globalTransforms[i] = parent * local;

        const BoneMatrix bone = ToBoneMatrix(globalTransforms[i] * joint.InverseBind);
        std::memcpy(destination + i * sizeof(BoneMatrix), &bone, sizeof(BoneMatrix));
    }

    const VkDeviceSize offset = static_cast<VkDeviceSize>(instance.FirstSkinMatrix) * sizeof(BoneMatrix);
    const VkDeviceSize size = static_cast<VkDeviceSize>(joints.size()) * sizeof(BoneMatrix);
    vmaFlushAllocation(VulkanContext::Get().GetAllocator().get(), m_SkinMatricesBuffers[frameIndex].Allocation, offset, size);

    m_Snapshot.SkinMatrixBuffer = m_SkinMatricesBuffers[frameIndex].Handle;
}

void AnimationSystem::AddPasses(RenderGraph::Graph& graph, const uint32_t frameIndex)
{
    // The bind-pose milestone fills the skin matrices on the CPU during BeginFrame, so there is no
    // animation or precompute pass yet. Evaluation/precompute passes will be added here later.
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

void AnimationSystem::RecompileShaders() const
{
    // No animation pipelines exist in the bind-pose milestone.
}

void AnimationSystem::Reset()
{
    m_Instances.clear();
    m_FrameSkeletons.clear();
    m_NextSkinMatrix = 0;
    m_NextInstanceId = 0;

    m_Snapshot.SkinMatrixBuffer = m_SkinMatricesBuffers[0].Handle;
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
