#include "AnimationSystem.hpp"

#include "Renderer/Animation/Skeleton.hpp"
#include "Profiling/Profilers.hpp"
#include "Scene/Entity.hpp"
#include "VulkanContext.hpp"

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

void AnimationSystem::BeginFrame(const Ref<Scene>& scene, const uint32_t frameIndex)
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

    const std::vector<AnimationInstance>& animations = scene->GetAnimationRuntime().GetInstances();
    for (const AnimationInstance& animation : animations)
    {
        if (!animation.Skeleton || !animation.HasPosed)
            continue;

        const uint32_t jointCount = animation.JointCount;
        if (jointCount == 0 || animation.Pose.SkinMatrices.size() != jointCount)
            continue;

        auto it = m_Instances.find(animation.SkeletonEntity);
        if (it != m_Instances.end() &&
            (it->second.SkeletonAsset != animation.SkeletonAsset ||
             it->second.Skeleton.get() != animation.Skeleton.get() ||
             it->second.JointCount != jointCount))
        {
            // The CPU runtime reset its pose for a replaced/reloaded skeleton. The old slice is
            // not reclaimed individually; Reset() returns all slices.
            m_Instances.erase(it);
            it = m_Instances.end();
        }

        if (it == m_Instances.end())
        {
            if (m_NextSkinMatrix + jointCount > MaxSkinMatrixJoints)
            {
                Log::CoreError("Skin matrix buffer exhausted while creating GPU skeleton instance for entity {}",
                               static_cast<uint32_t>(animation.SkeletonEntity));
                continue;
            }

            SkeletonInstanceState instance{};
            instance.Id = m_NextInstanceId++;
            instance.Entity = animation.SkeletonEntity;
            instance.SkeletonAsset = animation.SkeletonAsset;
            instance.Skeleton = animation.Skeleton;
            instance.JointCount = jointCount;
            instance.FirstSkinMatrix = m_NextSkinMatrix;
            m_NextSkinMatrix += jointCount;

            it = m_Instances.emplace(animation.SkeletonEntity, std::move(instance)).first;
        }
        else
        {
            it->second.Skeleton = animation.Skeleton;
            it->second.SkeletonAsset = animation.SkeletonAsset;
        }

        SkeletonInstanceState& instance = it->second;
        liveSkeletons.insert(animation.SkeletonEntity);

        if (instance.FrameVersions[frameIndex] != animation.PoseVersion)
            UploadSkinMatrices(instance, animation, frameIndex);

        m_FrameSkeletons.push_back({
            .Id = instance.Id,
            .Entity = animation.SkeletonEntity,
            .FirstSkinMatrix = instance.FirstSkinMatrix,
            .JointCount = instance.JointCount,
            .SkinMatrixVersion = animation.PoseVersion,
        });
    }

    for (auto it = m_Instances.begin(); it != m_Instances.end();)
    {
        if (!liveSkeletons.contains(it->first))
            it = m_Instances.erase(it);
        else
            ++it;
    }

    m_Snapshot.SkinMatrixBuffer = m_SkinMatricesBuffers[frameIndex].Handle;
    m_Snapshot.Skeletons = std::span<const SkeletonFrame>(m_FrameSkeletons);
}

void AnimationSystem::UploadSkinMatrices(SkeletonInstanceState& instance,
                                         const AnimationInstance& animation,
                                         const uint32_t frameIndex) const
{
    KBR_TRACY_FUNCTION();

    const VulkanBuffer& buffer = m_SkinMatricesBuffers[frameIndex];
    if (animation.Pose.SkinMatrices.size() != instance.JointCount || buffer.MappedData == nullptr)
        return;

    auto* destination = static_cast<std::byte*>(buffer.MappedData) +
        static_cast<size_t>(instance.FirstSkinMatrix) * sizeof(BoneMatrix);

    for (size_t jointIndex = 0; jointIndex < animation.Pose.SkinMatrices.size(); ++jointIndex)
    {
        const BoneMatrix bone = ToBoneMatrix(animation.Pose.SkinMatrices[jointIndex]);
        std::memcpy(destination + jointIndex * sizeof(BoneMatrix), &bone, sizeof(BoneMatrix));
    }

    const VkDeviceSize offset = static_cast<VkDeviceSize>(instance.FirstSkinMatrix) * sizeof(BoneMatrix);
    const VkDeviceSize size = static_cast<VkDeviceSize>(animation.Pose.SkinMatrices.size()) * sizeof(BoneMatrix);
    vmaFlushAllocation(VulkanContext::Get().GetAllocator().get(), buffer.Allocation, offset, size);

    instance.FrameVersions[frameIndex] = animation.PoseVersion;
}

void AnimationSystem::AddPasses([[maybe_unused]] RenderGraph::Graph& graph,
                                [[maybe_unused]] const uint32_t frameIndex)
{
    // The CPU runtime already produced the final pose before this frame was recorded. The only
    // remaining work is the skinning compute pass, which SkinningSystem owns.
}

const SkinningFrameSnapshot& AnimationSystem::GetSkinningSnapshot() const
{
    return m_Snapshot;
}

vk::Buffer AnimationSystem::GetSkinMatricesBuffer(const uint32_t frameIndex) const
{
    KBRAssert(frameIndex < m_SkinMatricesBuffers.size(), "Invalid frame index for skin matrices buffer");
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

} // namespace Kerberos
