#pragma once

#include "Buffer.hpp"
#include "ComputePipeline.hpp"
#include "Core/Core.hpp"
#include "DescriptorAllocator.hpp"
#include "GraphicsPipeline.hpp"
#include "Textures/Texture2D.hpp"
#include "Vulkan.hpp"

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <vector>
#include <vma/vk_mem_alloc.h>

namespace Kerberos {

class Scene;

struct alignas(16) ParticleFrameData
{
    glm::mat4 ViewProj{ 0.0f };
    glm::vec3 CameraUp{ 0.0f, 1.0f, 0.0f };
    float DeltaTime{ 0.0f };
    glm::vec3 CameraRight{ 1.0f, 0.0f, 0.0f };
    float Time{ 0.0f };
    glm::vec3 CameraPosition{ 0.0f, 0.0f, 0.0f };
    float NearPlane{ 0.1f };
    glm::vec2 ViewportSize{ 800.0f, 600.0f };
    float FarPlane{ 1000.0f };
    uint8_t _pad[4];
};

class ParticleSystem
{
public:
    ParticleSystem();
    ~ParticleSystem();

    void Initialize(vk::Format colorFormat,
                    vk::Format depthFormat,
                    vk::ImageView sceneDepthImageView,
                    const Owner<DescriptorAllocator>& allocator);

    void Update(const Ref<Scene>& scene,
                float dt,
                const vk::raii::CommandBuffer& cmd,
                uint32_t frameIndex,
                const ParticleFrameData& frameData,
                DescriptorAllocator& frameAllocator);

    void RecordDraw(const vk::raii::CommandBuffer& cmd, uint32_t frameIndex) const;

    void OnResize(uint32_t width, uint32_t height, vk::ImageView depthImageView);

private:
    void SetupDescriptors();
    void SetupPipelines(vk::Format colorFormat, vk::Format depthFormat);

    void AllocateParticleFrameBuffers();
    void AllocateIndirectDrawBuffers();
    void AllocateDescriptorBuffers();

    void CreateDefaultParticleTexture();

private:
    static constexpr size_t MaxParticles = 1'000'000;

    DescriptorAllocator* m_DescriptorAllocator = nullptr;

    StorageBuffer m_ParticlePoolBuffer;
    StorageBuffer m_DeadListBuffer;
    StorageBuffer m_AliveListBuffer;
    StorageBuffer m_CountersBuffer;

    struct VulkanBuffer
    {
        vk::Buffer Handle;
        VmaAllocation allocation;
        void* MappedData = nullptr;
        vk::DeviceAddress DeviceAddress = 0;
    };
    std::vector<VulkanBuffer> m_ParticleFrameBuffers;
    std::vector<VulkanBuffer> m_IndirectDrawBuffers;
    std::vector<StorageBuffer> m_SpawnRequestBuffers;

    ShaderResourceSet m_ParticleSet;
    std::vector<ShaderResourceSet> m_SpawnSets;
    ShaderResourceSet m_TextureSet;

    vk::raii::DescriptorSetLayout m_ParticleBuffersLayout = nullptr;
    vk::raii::DescriptorSetLayout m_SpawnRequestsLayout = nullptr;
    vk::raii::DescriptorSetLayout m_TextureLayout = nullptr;

    vk::raii::PipelineLayout m_ComputePipelineLayout = nullptr;
    vk::raii::PipelineLayout m_GraphicsPipelineLayout = nullptr;

    vk::raii::Sampler m_ParticleSampler = nullptr;
    vk::raii::Sampler m_PointSampler = nullptr;
    vk::ImageView m_SceneDepthImageView = nullptr;

    Ref<Texture2D> m_DefaultParticleTexture = nullptr;

    Ref<ComputePipeline> m_SpawnPipeline;
    Ref<ComputePipeline> m_PrepareSimulatePipeline;
    Ref<ComputePipeline> m_UpdatePipeline;
    Ref<GraphicsPipeline> m_RenderPipeline;
};

} // namespace Kerberos