#include "AnimationSystem.hpp"

#include "Assets/AssetManager.hpp"
#include "Renderer/Animation/Skeleton.hpp"
#include "Profiling/Profilers.hpp"
#include "Scene/Components/AnimationComponents.hpp"
#include "VulkanContext.hpp"

#include <cstring>
#include <unordered_set>

import Kerberos;

namespace Kerberos {

void AnimationSystem::Initialize(const Owner<DescriptorAllocator>& allocator)
{
    m_DescriptorAllocator = allocator.get();

    AllocateSkinMatricesBuffer();

    m_Snapshot.SkinMatrixBuffer = m_SkinMatricesBuffer.Handle;
}

void AnimationSystem::Shutdown()
{
    Reset();
    m_SkinMatricesBuffer.Reset();
    m_Snapshot = {};
}

void AnimationSystem::AllocateSkinMatricesBuffer()
{
    auto& context = VulkanContext::Get();

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = static_cast<VkDeviceSize>(MaxSkinMatrixJoints) * sizeof(BoneMatrix);
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
        VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

    VkBuffer buffer = nullptr;
    if (vmaCreateBuffer(context.GetAllocator().get(),
                        &bufferInfo,
                        &allocInfo,
                        &buffer,
                        &m_SkinMatricesBuffer.Allocation,
                        nullptr) != VK_SUCCESS)
    {
        KBRAssert(false, "Failed to create skin matrices buffer");
        return;
    }

    m_SkinMatricesBuffer.Handle = vk::Buffer(buffer);
    vmaMapMemory(context.GetAllocator().get(), m_SkinMatricesBuffer.Allocation, &m_SkinMatricesBuffer.MappedData);
    m_SkinMatricesBuffer.Size = bufferInfo.size;
    m_SkinMatricesBuffer.DeviceAddress =
        context.GetDevice().getBufferAddress({ .buffer = m_SkinMatricesBuffer.Handle });

    context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkBuffer>(m_SkinMatricesBuffer.Handle)),
                               vk::ObjectType::eBuffer,
                               "Animation Skin Matrices Buffer");
    context.SetObjectDebugName(m_SkinMatricesBuffer.Allocation, "Animation Skin Matrices Buffer Allocation");

    std::memset(m_SkinMatricesBuffer.MappedData, 0, static_cast<size_t>(m_SkinMatricesBuffer.Size));
}

void AnimationSystem::BeginFrame(const Ref<Scene>& scene, float dt, bool paused)
{
    KBR_TRACY_FUNCTION();

    (void)dt;
    (void)paused;

    m_FrameSkeletons.clear();

    if (!scene)
    {
        m_Snapshot.SkinMatrixBuffer = m_SkinMatricesBuffer.Handle;
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
            WriteBindPose(instance);
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

    m_Snapshot.SkinMatrixBuffer = m_SkinMatricesBuffer.Handle;
    m_Snapshot.Skeletons = std::span<const SkeletonFrame>(m_FrameSkeletons);
}

void AnimationSystem::WriteBindPose(const SkeletonInstanceState& instance)
{
    if (!instance.Skeleton || m_SkinMatricesBuffer.MappedData == nullptr)
        return;

    const auto& joints = instance.Skeleton->GetJoints();
    auto* destination = static_cast<std::byte*>(m_SkinMatricesBuffer.MappedData) +
        static_cast<size_t>(instance.FirstSkinMatrix) * sizeof(BoneMatrix);

    for (size_t i = 0; i < joints.size(); ++i)
    {
        const BoneMatrix bone = ToBoneMatrix(joints[i].InverseBind);
        std::memcpy(destination + i * sizeof(BoneMatrix), &bone, sizeof(BoneMatrix));
    }

    const VkDeviceSize offset = static_cast<VkDeviceSize>(instance.FirstSkinMatrix) * sizeof(BoneMatrix);
    const VkDeviceSize size = static_cast<VkDeviceSize>(joints.size()) * sizeof(BoneMatrix);
    vmaFlushAllocation(VulkanContext::Get().GetAllocator().get(), m_SkinMatricesBuffer.Allocation, offset, size);

    m_Snapshot.SkinMatrixBuffer = m_SkinMatricesBuffer.Handle;
}

void AnimationSystem::AddPasses(RenderGraph::Graph& graph, uint32_t frameIndex)
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

    m_Snapshot.SkinMatrixBuffer = m_SkinMatricesBuffer.Handle;
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
