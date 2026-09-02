#include "Renderer.hpp"

#include "Buffer.hpp"
#include "ColliderDebugHelpers.hpp"
#include "ComputePipeline.hpp"
#include "DescriptorAllocator.hpp"
#include "GraphicsPipeline.hpp"
#include "GrassSystem.hpp"
#include "MaterialRegistry.hpp"
#include "ModelLoader.hpp"
#include "ParticleSystem.hpp"
#include "RayTracingSceneCache.hpp"
#include "SMAA/SMAAAreaTex.hpp"
#include "SMAA/SMAASearchTex.hpp"
#include "Scene/Components/PhysicsComponents.hpp"
#include "Shaders/Shader.hpp"
#include "SkyboxUtils.hpp"
#include "TextureManager.hpp"
#include "Utils.hpp"
#include "VulkanContext.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_inverse.hpp>
#include <limits>
#include <numbers>

#include "Material.hpp"

import Kerberos;

namespace {
using namespace Kerberos;

glm::mat4 GetWorldTransformWithoutScale(const TransformComponent& transform)
{
    const glm::vec3 position = glm::vec3(transform.WorldTransform[3]);

    glm::mat3 rotation = glm::mat3(transform.WorldTransform);
    for (int i = 0; i < 3; ++i) {
        const float length = glm::length(rotation[i]);
        rotation[i] = length > 1e-6f ? rotation[i] / length : glm::vec3(0.0f);
    }

    glm::mat4 rotationMatrix(1.0f);
    rotationMatrix[0] = glm::vec4(rotation[0], 0.0f);
    rotationMatrix[1] = glm::vec4(rotation[1], 0.0f);
    rotationMatrix[2] = glm::vec4(rotation[2], 0.0f);
    rotationMatrix[3] = glm::vec4(position, 1.0f);
    return rotationMatrix;
}

struct ShadowMap
{
    constexpr static int CascadeCount = 4;

    vk::raii::Image Image = nullptr;
    vk::raii::DeviceMemory ImageMemory = nullptr;
    vk::raii::ImageView ReadImageView = nullptr;
    std::array<vk::raii::ImageView, ShadowMap::CascadeCount> WriteImageViews{ nullptr, nullptr, nullptr, nullptr };
    vk::Format Format = vk::Format::eUndefined;
    vk::raii::PipelineLayout PipelineLayout = nullptr;
    Ref<GraphicsPipeline> Pipeline = nullptr;

    // Settings
    uint32_t Size = 2048;
    bool EnablePCF = true;

    // Calculated at runtime
    glm::vec3 LightPosForCalculation{ 0.0f, 1.0f, 0.0f };
};

struct Skybox
{
    Ref<Mesh> SkyboxMesh = nullptr;
    Ref<TextureCube> SkyboxTexture = nullptr;
    bool IsSkyboxDirty = true; // Flag to indicate if the skybox needs to be re-rendered
    bool ShowSkybox = true;
    // Generated at runtime
    Ref<Texture2D> LutBrdfTexture = nullptr;
    Ref<TextureCube> IrradianceCubeTexture = nullptr;
    Ref<TextureCube> PrefilteredCubeTexture = nullptr;
};

struct ImageData
{
    vk::raii::Image Image = nullptr;
    vk::raii::DeviceMemory ImageMemory = nullptr;
    vk::raii::ImageView ImageView = nullptr;
    vk::Format Format = vk::Format::eUndefined;
};

struct PickingReadbackSlot
{
    vk::raii::Buffer Buffer = nullptr;
    vk::raii::DeviceMemory Memory = nullptr;
    void* MappedData = nullptr;
    bool Pending = false;
    uint64_t TimelineValue = 0;
};

struct MousePickingReadback
{
    std::array<PickingReadbackSlot, Renderer::MousePickingReadbackFrameLag> Slots{};
    uint32_t WriteIndex = 0;
    vk::raii::Semaphore TimelineSemaphore = nullptr;
    uint64_t TimelineValue = 0;
    uint64_t PendingTimelineSignalValue = 0;
    bool RequestPending = false;
    glm::uvec2 RequestedPixel{ 0, 0 };
    std::optional<uint32_t> LatestEntityID;
};

struct DescriptorSetLayouts
{
    vk::raii::DescriptorSetLayout scene = nullptr;
    vk::raii::DescriptorSetLayout composite = nullptr;
    vk::raii::DescriptorSetLayout gtao = nullptr;
    vk::raii::DescriptorSetLayout crossBilateralBlur = nullptr;
    vk::raii::DescriptorSetLayout tonemappingResolve = nullptr;
    vk::raii::DescriptorSetLayout fxaa = nullptr;
    vk::raii::DescriptorSetLayout bloom = nullptr;
};

struct SceneUniformData
{
    glm::mat4 projection{ 0.f };
    glm::mat4 view{ 0.f };
    std::array<glm::mat4, ShadowMap::CascadeCount> lightSpaceMatrices{ glm::mat4(0.0f) };
    glm::vec4 cascadeSplits{ 0.f };
    alignas(16) glm::vec3 camPos{ 0.f };
    uint32_t lightCount = 0;
    glm::vec2 viewportSize{ 0.0f, 0.0f };
    glm::vec3 cameraRight{ 0.f };
    float deltaTime = 0.0f;
    glm::vec3 cameraUp{ 0.f };
    float time = 0.0f;
};

struct GlobalLighting
{
    alignas(16) glm::vec4 sunLight{ 0.0f, 0.0f, 0.0f, 0.0f };
    float exposure = 1.0f;
    float gamma = 2.2f;
};

struct PerObjectData
{
    alignas(16) glm::mat4 model{ 0.f };
    alignas(16) glm::mat4 worldNormal{ 0.f };
    alignas(16) Material::UniformBlock material;
    uint8_t _Padding1[4];
    alignas(16) uint32_t entityID = std::numeric_limits<uint32_t>::max();
    uint8_t _Padding2[12];
};

struct SkyboxData
{
    glm::mat4 projection{ 0.f };
    glm::mat4 model{ 0.f };
};

struct WBOITData
{
    ImageData AccumulationImage;
    ImageData RevealageImage;
    ImageData DistortionImage;
    Ref<GraphicsPipeline> MainPipeline = nullptr;
};

struct CrossBilateralBlurConstants
{
    glm::vec2 inverseViewportSize{ 0.0f, 0.0f };
    glm::vec2 direction{ 0.0f, 0.0f };
};

struct TonemappingResolvePushConstants
{
    glm::vec2 inverseScreenSize{ 0.0f, 0.0f };
    float bloomIntensity = 0.05f;
    float tonemapOperator = 0.0f; // 0 = Uncharted, 1 = Reinhard, 2 = ACES Filmic, 3 = ACES Fitted
    uint32_t needsLuma = 1u;      // 1 if using FXAA/spatial AA, 0 otherwise
};

struct FXAAPushConstants
{
    glm::vec2 inverseViewportSize{ 0.0f, 0.0f };
};

struct SMAAData
{
    ImageData EdgesImage;
    ImageData BlendImage;

    // SMAA lookup textures
    ImageData AreaTexture;
    ImageData SearchTexture;

    vk::raii::PipelineLayout EdgeDetectionPipelineLayout = nullptr;
    Ref<GraphicsPipeline> EdgeDetectionPipeline = nullptr;

    vk::raii::PipelineLayout BlendWeightPipelineLayout = nullptr;
    Ref<GraphicsPipeline> BlendWeightPipeline = nullptr;

    vk::raii::PipelineLayout NeighborhoodBlendingPipelineLayout = nullptr;
    Ref<GraphicsPipeline> NeighborhoodBlendingPipeline = nullptr;

    struct DescriptorSetLayouts
    {
        vk::raii::DescriptorSetLayout EdgeDetection = nullptr;
        vk::raii::DescriptorSetLayout BlendWeight = nullptr;
        vk::raii::DescriptorSetLayout NeighbourhoodBlend = nullptr;
    };
    SMAAData::DescriptorSetLayouts DescriptorSetLayouts{};

    struct DescriptorSets
    {
        vk::raii::DescriptorSet EdgeDetection = nullptr;
        vk::raii::DescriptorSet BlendWeight = nullptr;
        vk::raii::DescriptorSet NeighbourhoodBlend = nullptr;
    };
    std::array<SMAAData::DescriptorSets, VulkanContext::MaxFramesInFlight> DescriptorSets{};

    struct PushConstants
    {
        glm::vec2 InverseViewportSize{ 0.0f, 0.0f };
        glm::vec2 ViewportSize{ 0.0f, 0.0f };
    };
};

struct BloomData
{
    vk::raii::Image Image = nullptr;
    vk::raii::DeviceMemory ImageMemory = nullptr;
    std::vector<vk::raii::ImageView> ImageViews{};
    std::vector<glm::vec2> MipSizes{};
    vk::Format Format = vk::Format::eUndefined;

    constexpr static uint32_t MaxMipLevels = 10;
    constexpr static uint32_t DefaultMipLevels = 7;
    uint32_t MipLevels = DefaultMipLevels;

    float FilterRadius = 1.0f;
    float Intensity = 1.0f;
    BloomMode Mode = BloomMode::BrightPassPrefilter;
    float Threshold = 1.0f;
    float Knee = 0.1f;
    float MaxBrightness = 20.0f;

    vk::raii::PipelineLayout DownsamplePipelineLayout = nullptr;
    Ref<ComputePipeline> DownsamplePipeline = nullptr;
    vk::raii::PipelineLayout UpsamplePipelineLayout = nullptr;
    Ref<ComputePipeline> UpsamplePipeline = nullptr;

    struct DownsamplePushConstants
    {
        glm::vec2 srcTexelSize{ 0.0f, 0.0f };
        float threshold = 0.0f;
        float knee = 0.0f;
        uint32_t enablePrefilter = 0;
        uint32_t isExtract = 0;
        float maxBrightness = 0.0f;
    };

    struct UpsamplePushConstants
    {
        float filterRadius = 1.0f;
    };

    vk::raii::DescriptorSet ExtractSet = nullptr;
    std::vector<vk::raii::DescriptorSet> DownsampleSets;
    std::vector<vk::raii::DescriptorSet> UpsampleSets;
};

struct UniformBufferObject
{
    Ref<UniformBuffer> scene;
    Ref<UniformBuffer> globalLighting;
    Ref<UniformBuffer> perObject;
    Ref<UniformBuffer> skybox;
    Ref<UniformBuffer> gtao;
};

struct StorageBuffers
{
    Ref<StorageBuffer> Lights;
};

struct DescriptorSets
{
    vk::raii::DescriptorSet scene = nullptr;
    vk::raii::DescriptorSet skybox = nullptr;
    vk::raii::DescriptorSet resolve = nullptr;
    vk::raii::DescriptorSet gtao = nullptr;
    vk::raii::DescriptorSet crossBilateralBlurHorizontal = nullptr;
    vk::raii::DescriptorSet crossBilateralBlurVertical = nullptr;
    vk::raii::DescriptorSet tonemappingResolve = nullptr;
    vk::raii::DescriptorSet fxaa = nullptr;
};

struct PendingSceneRender
{
    Ref<Scene> Scene = nullptr;
    glm::mat4 View{ 1.0f };
    glm::mat4 Projection{ 1.0f };
    glm::vec3 CameraPosition{ 0.0f };
    glm::vec3 CameraUp{ 0.0f, 1.0f, 0.0f };
    glm::vec3 CameraRight{ 1.0f, 0.0f, 0.0f };
    std::function<std::pair<std::vector<glm::mat4>, glm::vec4>(const glm::vec3&,
                                                               const std::function<glm::vec4(float)>&)>
        CalculateLightSpaceMatricesFunc;
    float DeltaTime = 0.0f;
    float NearPlane = 0.0f;
    float FarPlane = 0.0f;
    bool IsValid = false;
};

enum class GPUTimestampQuery : uint32_t
{
    FrameBegin = 0,
    ParticlesSimulateBegin,
    ParticlesSimulateEnd,
    DepthPrePassBegin,
    DepthPrePassEnd,
    ShadowBegin,
    ShadowEnd,
    OpaqueBegin,
    OpaqueEnd,
    GrassBegin,
    GrassEnd,
    ParticlesDrawBegin,
    ParticlesDrawEnd,
    TransparentBegin,
    TransparentEnd,
    TransparencyResolveBegin,
    TransparencyResolveEnd,
    BloomPassBegin,
    BloomPassEnd,
    AmbientOcclusionPassBegin,
    AmbientOcclusionPassEnd,
    TonemappingPassBegin,
    TonemappingPassEnd,
    AntialiasingPassBegin,
    AntialiasingPassEnd,
    FrameEnd,
    Count
};

struct RendererData
{
    struct ColliderLineBuffer
    {
        vk::raii::Buffer Buffer = nullptr;
        vk::raii::DeviceMemory Memory = nullptr;
        void* MappedData = nullptr;
    };

    Owner<DescriptorAllocator> PersistentDescriptorAllocator = nullptr;
    std::array<Owner<DescriptorAllocator>, VulkanContext::MaxFramesInFlight> FrameDescriptorAllocators{ nullptr };

    vk::raii::DescriptorPool DescriptorPool = nullptr;
    DescriptorSetLayouts DescriptorSetLayouts;

    TextureManager TextureManager{};
    MaterialRegistry MaterialRegistry{};

    DepthBias DepthBias;
    ShadowMap ShadowMap;
    Skybox Skybox;
    WBOITData Transparency;
    BloomData Bloom;

    ImageData ColorImage;
    ImageData DepthImage;
    ImageData NormalImage;
    ImageData ResolveImage;
    ImageData TonemappedImage;
    ImageData CompositeImage;
    ImageData PickingImage;
    ImageData GTAOImage;
    ImageData GTAOScratchImage;

    vk::ImageLayout PickingImageLayout = vk::ImageLayout::eUndefined;

    vk::raii::PipelineLayout PBRPipelineLayout = nullptr;
    Ref<GraphicsPipeline> DepthPrePassPipeline = nullptr;
    Ref<GraphicsPipeline> PBROpaquePipeline = nullptr;
    Ref<GraphicsPipeline> PBROpaquePipelinePCF = nullptr;
    Ref<GraphicsPipeline> SkyboxPipeline = nullptr;
    Ref<GraphicsPipeline> NormalDebugPipeline = nullptr;
    Ref<GraphicsPipeline> ColliderLinesPipeline = nullptr;
    Ref<GraphicsPipeline> PBRRayQueryShadowsPipeline = nullptr;
    Ref<GraphicsPipeline> PBRRayQuerySoftShadowsPipeline = nullptr;

    vk::raii::PipelineLayout GTAOPipelineLayout = nullptr;
    Ref<ComputePipeline> GTAOPipeline = nullptr;

    vk::raii::PipelineLayout CrossBilateralBlurPipelineLayout = nullptr;
    Ref<ComputePipeline> CrossBilateralBlurPipeline = nullptr;

    vk::raii::PipelineLayout TransparencyResolvePipelineLayout = nullptr;
    Ref<GraphicsPipeline> TransparencyResolvePipeline = nullptr;

    vk::raii::PipelineLayout TonemappingResolvePipelineLayout = nullptr;
    Ref<GraphicsPipeline> TonemappingResolvePipeline = nullptr;

    vk::raii::PipelineLayout FXAAPipelineLayout = nullptr;
    Ref<GraphicsPipeline> FXAAPipeline = nullptr;
    Ref<GraphicsPipeline> NoopPostProcessPipeline = nullptr;

    vk::raii::Sampler ColorSampler = nullptr;
    vk::raii::Sampler ShadowMapSampler = nullptr;
    vk::raii::Sampler PointSampler = nullptr;
    vk::raii::Sampler LinearSampler = nullptr;

    SceneUniformData SceneUniformData{};
    GlobalLighting GlobalLightingData{};
    PerObjectData PerObjectData{};
    SkyboxData SkyboxData{};
    GTAOConstants GTAOData{};
    SMAAData SMAAResources{};

    std::array<UniformBufferObject, VulkanContext::MaxFramesInFlight> UniformBuffers{};
    std::array<StorageBuffers, VulkanContext::MaxFramesInFlight> StorageBuffers{};

    std::array<DescriptorSets, VulkanContext::MaxFramesInFlight> DescriptorSets{};
    std::array<ColliderLineBuffer, VulkanContext::MaxFramesInFlight> ColliderLineBuffers{};

    // Dynamic uniform buffer related members
    VkDeviceSize MinUniformBufferOffsetAlignment = 0;
    uint64_t DynamicAlignment = 0;

    vk::DescriptorSet ColorOutputDescriptorSet = nullptr;
    std::array<vk::DescriptorSet, ShadowMap::CascadeCount> ShadowMapDescriptorSet = { nullptr };

    PendingSceneRender PendingRender{};

    MousePickingReadback MousePickingReadback{};

    std::vector<vk::raii::QueryPool> GPUTimestampQueryPools;
    float GPUTimestampPeriodNanoseconds = 0.0f;
    bool SupportsGPUTimestamps = false;
    GPUTimings LatestGPUTimings{};
    RenderStatistics LatestRenderStatistics{};

    bool SupportsPipelineStatistics = false;
    std::vector<vk::raii::QueryPool> PipelineStatisticsQueryPools;
    std::vector<vk::raii::QueryPool> MeshPipelineStatisticsQueryPools;
    PipelineStatistics LatestPipelineStatistics{};

    RayTracingSceneCache RayTracingCache{};
    ParticleSystem ParticleSystem{};
    GrassSystem GrassSystem{};

    glm::vec2 OutputSize{ 1280.0f, 720.0f };

    constexpr static uint32_t TemporalSequenceLength = 8;

    vk::ImageLayout GTAOImageLayout = vk::ImageLayout::eUndefined;
    bool PreviousUseGTAO = true;

    // Settings
    bool DisplayDebugNormals = false;
    bool DisplayPhysicsColliders = false;

    bool UseRayQueryBasedShadows = false;
    bool UseRayQueryBasedSoftShadows = false;
    bool UseGTAO = true;
    bool UseBlurredGTAO = true;

    bool UseFrustumCulling = true;
    bool FreezeFrustum = false;
    Frustum FrozenFrustum{};
    uint32_t AllObjectCount = 0;
    uint32_t VisibleObjectCount = 0;
    uint32_t CulledObjectCount = 0;

    AntiAliasingMode AntiAliasingMode = AntiAliasingMode::FXAA;
    TonemappingOperator TonemappingOperator = TonemappingOperator::ACES;
};

} // namespace

namespace Kerberos {

static Owner<RendererData> s_Data = nullptr;

void Renderer::Init()
{
    KBRAssert(s_Data == nullptr, "Renderer is already initialized!");
    Log::CoreInfo("Initializing Renderer...");

    s_Data = CreateOwner<RendererData>();

    s_Data->PersistentDescriptorAllocator = CreateOwner<DescriptorAllocator>(1000);
    for (uint32_t i = 0; i < VulkanContext::MaxFramesInFlight; ++i) {
        s_Data->FrameDescriptorAllocators[i] = CreateOwner<DescriptorAllocator>(1000);
    }

    Log::CoreInfo("Size of SceneUniformData: {} bytes", sizeof(SceneUniformData));
    Log::CoreInfo("Size of GlobalLighting: {} bytes", sizeof(GlobalLighting));
    Log::CoreInfo("Size of PerObjectData: {} bytes", sizeof(PerObjectData));
    Log::CoreInfo("Size of SkyboxData: {} bytes", sizeof(SkyboxData));
    Log::CoreInfo("Size of GTAOConstants: {} bytes", sizeof(GTAOConstants));
    Log::CoreInfo("Size of material UniformBlock: {} bytes", sizeof(Material::UniformBlock));

    CreateDefaultMaterials();

    // Setup initial directional light which we will use to generate the shadow map
    s_Data->GlobalLightingData.sunLight = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);

    s_Data->TextureManager.Initialize();

    CreateResources();
}

void Renderer::Shutdown()
{
    KBRAssert(s_Data != nullptr, "Renderer not initialized!");

    VulkanContext::Get().WaitIdle();

    VulkanContext::DestroyImGuiDescriptorSet(s_Data->ColorOutputDescriptorSet);
    for (auto& descriptorSet : s_Data->ShadowMapDescriptorSet) {
        VulkanContext::DestroyImGuiDescriptorSet(descriptorSet);
    }

    for (auto& slot : s_Data->MousePickingReadback.Slots) {
        if (slot.Memory != nullptr && slot.MappedData) {
            slot.Memory.unmapMemory();
            slot.MappedData = nullptr;
        }
    }

    for (auto& [Buffer, Memory, MappedData] : s_Data->ColliderLineBuffers) {
        if (Memory != nullptr && MappedData) {
            Memory.unmapMemory();
            MappedData = nullptr;
        }
    }

    s_Data.reset();
    s_Data = nullptr;
}

void Renderer::RenderSceneEditor(const Ref<Scene>& scene, const Camera& camera, const float dt)
{
    RenderScene(
        scene,
        camera.GetViewMatrix(),
        camera.GetProjectionMatrix(),
        camera.GetPosition(),
        [&camera](const glm::vec3& lightDir, const std::function<glm::vec4(float)>& getCascadeSplits) {
            return camera.GetLightSpaceMatrices(lightDir, getCascadeSplits);
        },
        dt,
        camera.GetNearClip(),
        camera.GetFarClip());
}

void Renderer::RenderSceneRuntime(const Ref<Scene>& scene,
                                  const Camera& mainCamera,
                                  const glm::mat4& mainCameraTransform,
                                  const float dt)
{
    const glm::vec3 camPos = mainCameraTransform[3];
    RenderScene(
        scene,
        mainCamera.GetViewMatrix(),
        mainCamera.GetProjectionMatrix(),
        camPos,
        [&mainCamera](const glm::vec3& lightDir, const std::function<glm::vec4(float)>& getCascadeSplits) {
            return mainCamera.GetLightSpaceMatrices(lightDir, getCascadeSplits);
        },
        dt,
        mainCamera.GetNearClip(),
        mainCamera.GetFarClip());
}

void Renderer::RenderScene(
    const Ref<Scene>& scene,
    const glm::mat4& view,
    const glm::mat4& projection,
    const glm::vec3& camPos,
    const std::function<std::pair<std::vector<glm::mat4>, glm::vec4>(
        const glm::vec3&, const std::function<glm::vec4(float)>&)>& calculateLightSpaceMatricesFunc,
    const float dt,
    const float nearPlane,
    const float farPlane)
{
    KBRAssert(!s_Data->PendingRender.IsValid, "Scene has already been queued for rendering!");

    glm::mat4 invView = glm::inverse(view);

    s_Data->PendingRender.Scene = scene;
    s_Data->PendingRender.View = view;
    s_Data->PendingRender.Projection = projection;
    s_Data->PendingRender.CameraPosition = camPos;
    s_Data->PendingRender.CameraRight = glm::normalize(glm::vec3(invView[0]));
    s_Data->PendingRender.CameraUp = glm::normalize(glm::vec3(invView[1]));
    s_Data->PendingRender.CalculateLightSpaceMatricesFunc = calculateLightSpaceMatricesFunc;
    s_Data->PendingRender.DeltaTime = dt;
    s_Data->PendingRender.NearPlane = nearPlane;
    s_Data->PendingRender.FarPlane = farPlane;
    s_Data->PendingRender.IsValid = scene != nullptr;
}

void Renderer::RecordQueuedSceneRender(const vk::raii::CommandBuffer& cmd)
{
    KBRAssert(s_Data->PendingRender.IsValid, "No pending scene render to record!");

    if (!s_Data->PendingRender.IsValid || !s_Data->PendingRender.Scene)
        return;

    auto& context = VulkanContext::Get();
    const uint32_t frameIndex = context.GetCurrentFrameIndex();
    const uint32_t frameCount = context.GetFrameCount();
    const uint32_t temporalIndex = frameCount % RendererData::TemporalSequenceLength + 1;

    DescriptorAllocator& frameDescriptorAllocator = *s_Data->FrameDescriptorAllocators[frameIndex];
    frameDescriptorAllocator.Reset();

    constexpr size_t frameArenaSize = 1024 * 256;       // 256 KB frame budget
    std::array<std::byte, frameArenaSize> frameArenaBuffer;
    std::pmr::monotonic_buffer_resource frameArena(frameArenaBuffer.data(), frameArenaBuffer.size());

    if (IsUsingAccelerationStructures()) {
        s_Data->RayTracingCache.BuildAccelerationStructures(s_Data->PendingRender.Scene, cmd, frameIndex);

        const auto& tlas = s_Data->RayTracingCache.GetTLAS(frameIndex);
        const vk::WriteDescriptorSetAccelerationStructureKHR asInfo{ .accelerationStructureCount = 1,
                                                                     .pAccelerationStructures = &tlas };
        const std::vector asWrite = { vk::WriteDescriptorSet{
            .pNext = &asInfo,
            .dstSet = *s_Data->DescriptorSets[frameIndex].scene,
            .dstBinding = 7,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eAccelerationStructureKHR,
        } };
        context.GetDevice().updateDescriptorSets(asWrite, {});
    }

    auto [allObjects, uniqueMaterials] =
        GetRenderObjectsAndUniqueMaterialsFromScene(*s_Data->PendingRender.Scene.get(), &frameArena);
    const auto colliderLineVertices = s_Data->DisplayPhysicsColliders
                                          ? GetColliderLineVerticesFromScene(*s_Data->PendingRender.Scene.get())
                                          : std::vector<LineVertex>{};

    // Update all per-object uniform buffers once
    for (const auto& renderObject : allObjects) {
        const auto& [Transform, Mesh, Material, EntityID, WorldAABB, UBOIndex, DebugName] = renderObject;

        Ref<Kerberos::Material> material = Material;
        if (material == nullptr)
            material = s_Data->MaterialRegistry.Get("DebugPink");

        UpdatePerObjectUniformBuffer(frameIndex, UBOIndex, Transform, *material, EntityID);
    }

    RenderObjectContainer renderObjects;
    renderObjects.reserve(allObjects.size());

    if (!s_Data->UseFrustumCulling) {
        renderObjects = allObjects;
    }
    else {

        Frustum frustum;
        if (s_Data->FreezeFrustum) {
            frustum = s_Data->FrozenFrustum;
        }
        else {
            frustum = Frustum::CreateFromViewProjection(s_Data->PendingRender.Projection * s_Data->PendingRender.View);
        }

        renderObjects = FrustumCullRenderObjects(allObjects, frustum, &frameArena);

        s_Data->FrozenFrustum = frustum;
    }

    s_Data->AllObjectCount = static_cast<uint32_t>(allObjects.size());
    s_Data->VisibleObjectCount = static_cast<uint32_t>(renderObjects.size());
    s_Data->CulledObjectCount = s_Data->AllObjectCount - s_Data->VisibleObjectCount;

    auto& renderStatistics = s_Data->LatestRenderStatistics;
    renderStatistics = {};
    renderStatistics.RenderObjectCount = static_cast<uint32_t>(renderObjects.size());
    renderStatistics.UniqueMaterialCount = static_cast<uint32_t>(uniqueMaterials.size());
    renderStatistics.ColliderLineVertexCount = static_cast<uint32_t>(colliderLineVertices.size());

    for (const auto& renderObject : renderObjects) {
        if (!renderObject.Mesh)
            continue;

        const uint32_t vertexCount = static_cast<uint32_t>(renderObject.Mesh->GetVertices().size());
        const uint32_t indexCount = static_cast<uint32_t>(renderObject.Mesh->GetIndices().size());

        renderStatistics.VertexCount += vertexCount;
        renderStatistics.IndexCount += indexCount;
        renderStatistics.FaceCount += indexCount / 3;
    }

    renderStatistics.IsValid = true;

    s_Data->MaterialRegistry.SyncWithCurrentMaterials(uniqueMaterials);
    s_Data->MaterialRegistry.ResolveAllMaterialIndices(s_Data->TextureManager);

    ResolveGPUTimings(frameIndex);
    ResolvePipelineStatistics(frameIndex);

    ResetQueryPools(cmd, frameIndex);

    cmd.beginQuery(s_Data->PipelineStatisticsQueryPools[frameIndex], 0, {});

    WriteGPUTimestamp(cmd, frameIndex, static_cast<uint32_t>(GPUTimestampQuery::FrameBegin));

    const uint32_t currentImage = frameIndex;

    const DirectionalLight sunlight = s_Data->PendingRender.Scene->GetSunlight();
    s_Data->GlobalLightingData.sunLight = glm::vec4(sunlight.Direction, sunlight.Intensity);

    const auto getCascadeSplits = [](const float farPlane) -> glm::vec4 {
        return { farPlane / 25.0f, farPlane / 10.0f, farPlane / 2.0f, farPlane };
    };

    const auto [lightSpaceMatrices, cascadeSplits] =
        s_Data->PendingRender.CalculateLightSpaceMatricesFunc(s_Data->GlobalLightingData.sunLight, getCascadeSplits);

    const std::vector<GPULight> gpuLights = GetLightsFromScene(*s_Data->PendingRender.Scene.get());
    const uint32_t lightCount = static_cast<uint32_t>(gpuLights.size());

    UpdateLights(currentImage, gpuLights);
    UpdateSceneUniformBuffers(currentImage,
                              s_Data->PendingRender.View,
                              s_Data->PendingRender.Projection,
                              s_Data->PendingRender.CameraPosition,
                              temporalIndex,
                              lightSpaceMatrices,
                              cascadeSplits,
                              lightCount,
                              s_Data->PendingRender.DeltaTime);

    static float time = 0.0f;
    time += s_Data->PendingRender.DeltaTime;

    const ParticleFrameData particleFrameData{
        .ViewProj = s_Data->PendingRender.Projection * s_Data->PendingRender.View,
        .CameraUp = s_Data->PendingRender.CameraUp,
        .DeltaTime = s_Data->PendingRender.DeltaTime,
        .CameraRight = s_Data->PendingRender.CameraRight,
        .Time = time,
        .CameraPosition = s_Data->PendingRender.CameraPosition,
        .NearPlane = s_Data->PendingRender.NearPlane,
        .ViewportSize = s_Data->OutputSize,
        .FarPlane = s_Data->PendingRender.FarPlane,
    };
    WriteGPUTimestamp(cmd, frameIndex, static_cast<uint32_t>(GPUTimestampQuery::ParticlesSimulateBegin));
    s_Data->ParticleSystem.Update(s_Data->PendingRender.Scene,
                                  s_Data->PendingRender.DeltaTime,
                                  cmd,
                                  frameIndex,
                                  particleFrameData,
                                  frameDescriptorAllocator);
    WriteGPUTimestamp(cmd, frameIndex, static_cast<uint32_t>(GPUTimestampQuery::ParticlesSimulateEnd));

    // Depth Pre-pass
    {
        WriteGPUTimestamp(cmd, frameIndex, static_cast<uint32_t>(GPUTimestampQuery::DepthPrePassBegin));

        const vk::ImageMemoryBarrier2 depthBarrier = {
            .srcStageMask =
                vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
            .srcAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
            .dstStageMask =
                vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
            .dstAccessMask =
                vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = vk::ImageLayout::eDepthAttachmentOptimal,
            .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
            .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
            .image = s_Data->DepthImage.Image,
            .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eDepth,
                                  .baseMipLevel = 0,
                                  .levelCount = 1,
                                  .baseArrayLayer = 0,
                                  .layerCount = 1 }
        };

        const vk::ImageMemoryBarrier2 normalBarrier = {
            .srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            .srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            .dstAccessMask = vk::AccessFlagBits2::eColorAttachmentRead | vk::AccessFlagBits2::eColorAttachmentWrite,
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
            .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
            .image = s_Data->NormalImage.Image,
            .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eColor,
                                  .baseMipLevel = 0,
                                  .levelCount = 1,
                                  .baseArrayLayer = 0,
                                  .layerCount = 1 }
        };

        const std::array barriers = { depthBarrier, normalBarrier };

        const vk::DependencyInfo dependencyInfo = { .dependencyFlags = {},
                                                    .imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size()),
                                                    .pImageMemoryBarriers = barriers.data() };

        cmd.pipelineBarrier2(dependencyInfo);

        vk::RenderingAttachmentInfo depthAttachmentInfo{ .imageView = s_Data->DepthImage.ImageView,
                                                         .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
                                                         .loadOp = vk::AttachmentLoadOp::eClear,
                                                         .storeOp = vk::AttachmentStoreOp::eStore,
                                                         .clearValue = vk::ClearDepthStencilValue{ .depth = 1.0f,
                                                                                                   .stencil = 0 } };

        vk::RenderingAttachmentInfo normalAttachmentInfo{ .imageView = s_Data->NormalImage.ImageView,
                                                          .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
                                                          .loadOp = vk::AttachmentLoadOp::eClear,
                                                          .storeOp = vk::AttachmentStoreOp::eStore,
                                                          .clearValue = vk::ClearColorValue{
                                                              std::array{ 0.5f, 0.5f, 1.0f, 1.0f } } };

        const vk::Viewport viewport{ .x = 0.0f,
                                     .y = 0.0f,
                                     .width = s_Data->OutputSize.x,
                                     .height = s_Data->OutputSize.y,
                                     .minDepth = 0.0f,
                                     .maxDepth = 1.0f };

        const vk::Rect2D renderArea{ .offset = vk::Offset2D{ .x = 0, .y = 0 },
                                     .extent = vk::Extent2D{ .width = static_cast<uint32_t>(s_Data->OutputSize.x),
                                                             .height = static_cast<uint32_t>(s_Data->OutputSize.y) } };

        const vk::RenderingInfo depthPrePassRenderingInfo{ .renderArea = renderArea,
                                                           .layerCount = 1,
                                                           .colorAttachmentCount = 1,
                                                           .pColorAttachments = &normalAttachmentInfo,
                                                           .pDepthAttachment = &depthAttachmentInfo };

        BeginRenderPassDebugLabel(cmd, "Depth Pre-Pass");
        cmd.beginRendering(depthPrePassRenderingInfo);
        cmd.setViewport(0, viewport);
        cmd.setScissor(0, renderArea);

        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *s_Data->DepthPrePassPipeline->GetVulkanPipeline());

        for (const auto& renderObject : renderObjects) {
            if (renderObject.Material != nullptr && renderObject.Material->IsTransparent())
                continue;

            uint32_t dynamicOffset = static_cast<uint32_t>(renderObject.UBOIndex * s_Data->DynamicAlignment);

            cmd.bindDescriptorSets(
                vk::PipelineBindPoint::eGraphics,
                *s_Data->PBRPipelineLayout,
                0,
                { s_Data->DescriptorSets[currentImage].scene, s_Data->TextureManager.GetGlobalDescriptorSet() },
                { dynamicOffset });

            renderObject.Mesh->Draw(cmd);
        }

        cmd.endRendering();
        EndRenderPassDebugLabel(cmd);

        WriteGPUTimestamp(cmd, frameIndex, static_cast<uint32_t>(GPUTimestampQuery::DepthPrePassEnd));
    }

    WriteGPUTimestamp(cmd, frameIndex, static_cast<uint32_t>(GPUTimestampQuery::ShadowBegin));

    RenderShadowPass(cmd, frameIndex, allObjects, &frameArena);

    WriteGPUTimestamp(cmd, frameIndex, static_cast<uint32_t>(GPUTimestampQuery::ShadowEnd));

    // GTAO compute pass
    {
        WriteGPUTimestamp(cmd, frameIndex, static_cast<uint32_t>(GPUTimestampQuery::AmbientOcclusionPassBegin));

        const auto getSrcStageAccessForLayout =
            [](const vk::ImageLayout layout) -> std::pair<vk::PipelineStageFlags2, vk::AccessFlags2> {
            switch (layout) {
            case vk::ImageLayout::eShaderReadOnlyOptimal:
                return { vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderRead };
            case vk::ImageLayout::eGeneral:
                return { vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageWrite };
            case vk::ImageLayout::eTransferDstOptimal:
                return { vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite };
            case vk::ImageLayout::eUndefined:
            default:
                return { vk::PipelineStageFlagBits2::eTopOfPipe, {} };
            }
        };

        if (s_Data->UseGTAO) {
            // Transition GTAO output image to storage image from sampled image for compute shader write
            // Transition normal and depth images to shader read optimal layout
            {
                const auto [aoSrcStageMask, aoSrcAccessMask] = getSrcStageAccessForLayout(s_Data->GTAOImageLayout);
                const vk::ImageMemoryBarrier2 aoBarrier{ .srcStageMask = aoSrcStageMask,
                                                         .srcAccessMask = aoSrcAccessMask,
                                                         .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
                                                         .dstAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
                                                         .oldLayout = s_Data->GTAOImageLayout,
                                                         .newLayout = vk::ImageLayout::eGeneral,
                                                         .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
                                                         .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
                                                         .image = s_Data->GTAOImage.Image,
                                                         .subresourceRange = { .aspectMask =
                                                                                   vk::ImageAspectFlagBits::eColor,
                                                                               .baseMipLevel = 0,
                                                                               .levelCount = 1,
                                                                               .baseArrayLayer = 0,
                                                                               .layerCount = 1 } };

                const vk::ImageMemoryBarrier2 normalTextureBarrier{
                    .srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput |
                                    vk::PipelineStageFlagBits2::eLateFragmentTests,
                    .srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
                    .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
                    .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
                    .oldLayout = vk::ImageLayout::eColorAttachmentOptimal,
                    .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
                    .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
                    .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
                    .image = s_Data->NormalImage.Image,
                    .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eColor,
                                          .baseMipLevel = 0,
                                          .levelCount = 1,
                                          .baseArrayLayer = 0,
                                          .layerCount = 1 }
                };

                const vk::ImageMemoryBarrier2 depthBarrier{
                    .srcStageMask = vk::PipelineStageFlagBits2::eLateFragmentTests,
                    .srcAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
                    .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
                    .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
                    .oldLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal,
                    .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
                    .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
                    .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
                    .image = s_Data->DepthImage.Image,
                    .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eDepth,
                                          .baseMipLevel = 0,
                                          .levelCount = 1,
                                          .baseArrayLayer = 0,
                                          .layerCount = 1 }
                };

                const std::array barriers = { aoBarrier, normalTextureBarrier, depthBarrier };

                const vk::DependencyInfo dependencyInfo = { .dependencyFlags = {},
                                                            .imageMemoryBarrierCount =
                                                                static_cast<uint32_t>(barriers.size()),
                                                            .pImageMemoryBarriers = barriers.data() };

                cmd.pipelineBarrier2(dependencyInfo);
            }

            BeginRenderPassDebugLabel(cmd, "GTAO Compute Pass");

            cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *s_Data->GTAOPipeline->GetVulkanPipeline());
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                                   *s_Data->GTAOPipelineLayout,
                                   0,
                                   { s_Data->DescriptorSets[currentImage].gtao },
                                   {});

            const uint32_t groupX = (static_cast<uint32_t>(s_Data->OutputSize.x) + 7) / 8;
            const uint32_t groupY = (static_cast<uint32_t>(s_Data->OutputSize.y) + 7) / 8;

            cmd.dispatch(groupX, groupY, 1);

            EndRenderPassDebugLabel(cmd);

            if (s_Data->UseBlurredGTAO) {
                // Cross-bilateral blur horizontal pass

                {
                    const vk::ImageMemoryBarrier2 mainImageToShaderReadBarrier{
                        .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
                        .srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
                        .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
                        .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
                        .oldLayout = vk::ImageLayout::eGeneral,
                        .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
                        .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
                        .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
                        .image = s_Data->GTAOImage.Image,
                        .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eColor,
                                              .baseMipLevel = 0,
                                              .levelCount = 1,
                                              .baseArrayLayer = 0,
                                              .layerCount = 1 }
                    };

                    const vk::ImageMemoryBarrier2 scratchBarrier{
                        .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
                        .srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
                        .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
                        .dstAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
                        .oldLayout = vk::ImageLayout::eUndefined,
                        .newLayout = vk::ImageLayout::eGeneral,
                        .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
                        .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
                        .image = s_Data->GTAOScratchImage.Image,
                        .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eColor,
                                              .baseMipLevel = 0,
                                              .levelCount = 1,
                                              .baseArrayLayer = 0,
                                              .layerCount = 1 }
                    };

                    const std::array barriers = { mainImageToShaderReadBarrier, scratchBarrier };
                    const vk::DependencyInfo dependencyInfo = { .dependencyFlags = {},
                                                                .imageMemoryBarrierCount =
                                                                    static_cast<uint32_t>(barriers.size()),
                                                                .pImageMemoryBarriers = barriers.data() };
                    cmd.pipelineBarrier2(dependencyInfo);
                }

                BeginRenderPassDebugLabel(cmd, "GTAO Horizontal Blur");

                cmd.bindPipeline(vk::PipelineBindPoint::eCompute,
                                 *s_Data->CrossBilateralBlurPipeline->GetVulkanPipeline());
                cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                                       *s_Data->CrossBilateralBlurPipelineLayout,
                                       0,
                                       { s_Data->DescriptorSets[currentImage].crossBilateralBlurHorizontal },
                                       {});

                CrossBilateralBlurConstants pushConstants;
                pushConstants.inverseViewportSize = glm::vec2(1.0f) / s_Data->OutputSize;
                pushConstants.direction = { 1.0f, 0.0f };
                cmd.pushConstants<CrossBilateralBlurConstants>(
                    *s_Data->CrossBilateralBlurPipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, { pushConstants });

                cmd.dispatch(groupX, groupY, 1);

                EndRenderPassDebugLabel(cmd);

                // Cross-bilateral blur vertical pass

                BeginRenderPassDebugLabel(cmd, "GTAO Vertical Blur");

                {
                    // Transition scratch image to shader read and main image to general for compute shader write
                    const vk::ImageMemoryBarrier2 mainImageToGeneralBarrier{
                        .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
                        .srcAccessMask = vk::AccessFlagBits2::eShaderRead,
                        .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
                        .dstAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
                        .oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
                        .newLayout = vk::ImageLayout::eGeneral,
                        .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
                        .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
                        .image = s_Data->GTAOImage.Image,
                        .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eColor,
                                              .baseMipLevel = 0,
                                              .levelCount = 1,
                                              .baseArrayLayer = 0,
                                              .layerCount = 1 }
                    };
                    const vk::ImageMemoryBarrier2 scratchImageToShaderReadBarrier{
                        .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
                        .srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
                        .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
                        .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
                        .oldLayout = vk::ImageLayout::eGeneral,
                        .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
                        .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
                        .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
                        .image = s_Data->GTAOScratchImage.Image,
                        .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eColor,
                                              .baseMipLevel = 0,
                                              .levelCount = 1,
                                              .baseArrayLayer = 0,
                                              .layerCount = 1 }
                    };
                    const std::array barriers = { mainImageToGeneralBarrier, scratchImageToShaderReadBarrier };
                    const vk::DependencyInfo dependencyInfo = { .dependencyFlags = {},
                                                                .imageMemoryBarrierCount =
                                                                    static_cast<uint32_t>(barriers.size()),
                                                                .pImageMemoryBarriers = barriers.data() };
                    cmd.pipelineBarrier2(dependencyInfo);
                }

                cmd.bindPipeline(vk::PipelineBindPoint::eCompute,
                                 *s_Data->CrossBilateralBlurPipeline->GetVulkanPipeline());
                cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                                       *s_Data->CrossBilateralBlurPipelineLayout,
                                       0,
                                       { s_Data->DescriptorSets[currentImage].crossBilateralBlurVertical },
                                       {});

                pushConstants.direction = { 0.0f, 1.0f };
                cmd.pushConstants<CrossBilateralBlurConstants>(
                    *s_Data->CrossBilateralBlurPipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, { pushConstants });

                cmd.dispatch(groupX, groupY, 1);

                EndRenderPassDebugLabel(cmd);
            }

            // Transition ao image from storage image to sampled image
            // and transition depth image back to depth attachment
            {
                const vk::ImageMemoryBarrier2 aoBarrier{ .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
                                                         .srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
                                                         .dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
                                                         .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
                                                         .oldLayout = vk::ImageLayout::eGeneral,
                                                         .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
                                                         .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
                                                         .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
                                                         .image = s_Data->GTAOImage.Image,
                                                         .subresourceRange = { .aspectMask =
                                                                                   vk::ImageAspectFlagBits::eColor,
                                                                               .baseMipLevel = 0,
                                                                               .levelCount = 1,
                                                                               .baseArrayLayer = 0,
                                                                               .layerCount = 1 } };

                const vk::ImageMemoryBarrier2 depthBarrier{
                    .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
                    .srcAccessMask = vk::AccessFlagBits2::eShaderRead,
                    .dstStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                                    vk::PipelineStageFlagBits2::eLateFragmentTests,
                    .dstAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentRead |
                                     vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
                    .oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
                    .newLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal,
                    .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
                    .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
                    .image = s_Data->DepthImage.Image,
                    .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eDepth,
                                          .baseMipLevel = 0,
                                          .levelCount = 1,
                                          .baseArrayLayer = 0,
                                          .layerCount = 1 }
                };

                const std::array barriers = { aoBarrier, depthBarrier };

                const vk::DependencyInfo dependencyInfo = { .dependencyFlags = {},
                                                            .imageMemoryBarrierCount =
                                                                static_cast<uint32_t>(barriers.size()),
                                                            .pImageMemoryBarriers = barriers.data() };

                cmd.pipelineBarrier2(dependencyInfo);
            }

            s_Data->GTAOImageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        }
        else if (s_Data->PreviousUseGTAO || s_Data->GTAOImageLayout == vk::ImageLayout::eUndefined) {
            // GTAO disabled: clear once to neutral AO (1.0) so PBR keeps full ambient lighting.
            const auto [aoSrcStageMask, aoSrcAccessMask] = getSrcStageAccessForLayout(s_Data->GTAOImageLayout);
            const vk::ImageMemoryBarrier2 toTransferBarrier{ .srcStageMask = aoSrcStageMask,
                                                             .srcAccessMask = aoSrcAccessMask,
                                                             .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
                                                             .dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
                                                             .oldLayout = s_Data->GTAOImageLayout,
                                                             .newLayout = vk::ImageLayout::eTransferDstOptimal,
                                                             .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
                                                             .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
                                                             .image = s_Data->GTAOImage.Image,
                                                             .subresourceRange = { .aspectMask =
                                                                                       vk::ImageAspectFlagBits::eColor,
                                                                                   .baseMipLevel = 0,
                                                                                   .levelCount = 1,
                                                                                   .baseArrayLayer = 0,
                                                                                   .layerCount = 1 } };
            const vk::DependencyInfo toTransferDependencyInfo{ .dependencyFlags = {},
                                                               .imageMemoryBarrierCount = 1,
                                                               .pImageMemoryBarriers = &toTransferBarrier };
            cmd.pipelineBarrier2(toTransferDependencyInfo);

            constexpr vk::ClearColorValue clearNoAO{ std::array<float, 4>{ 1.0f, 0.0f, 0.0f, 0.0f } };
            constexpr std::array clearRanges = { vk::ImageSubresourceRange{ .aspectMask =
                                                                                vk::ImageAspectFlagBits::eColor,
                                                                            .baseMipLevel = 0,
                                                                            .levelCount = 1,
                                                                            .baseArrayLayer = 0,
                                                                            .layerCount = 1 } };
            cmd.clearColorImage(*s_Data->GTAOImage.Image, vk::ImageLayout::eTransferDstOptimal, clearNoAO, clearRanges);

            const vk::ImageMemoryBarrier2 toShaderReadBarrier{
                .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
                .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
                .dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
                .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
                .oldLayout = vk::ImageLayout::eTransferDstOptimal,
                .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
                .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
                .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
                .image = s_Data->GTAOImage.Image,
                .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eColor,
                                      .baseMipLevel = 0,
                                      .levelCount = 1,
                                      .baseArrayLayer = 0,
                                      .layerCount = 1 }
            };
            const vk::DependencyInfo toShaderReadDependencyInfo{ .dependencyFlags = {},
                                                                 .imageMemoryBarrierCount = 1,
                                                                 .pImageMemoryBarriers = &toShaderReadBarrier };
            cmd.pipelineBarrier2(toShaderReadDependencyInfo);

            s_Data->GTAOImageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        }

        s_Data->PreviousUseGTAO = s_Data->UseGTAO;

        WriteGPUTimestamp(cmd, frameIndex, static_cast<uint32_t>(GPUTimestampQuery::AmbientOcclusionPassEnd));
    }

    // Transition shadow map image layout for shader read
    {
        vk::ImageMemoryBarrier2 barrier = { .srcStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                                                            vk::PipelineStageFlagBits2::eLateFragmentTests,
                                            .srcAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
                                            .dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
                                            .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
                                            .oldLayout = vk::ImageLayout::eDepthAttachmentOptimal,
                                            .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
                                            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                            .image = s_Data->ShadowMap.Image,
                                            .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eDepth,
                                                                  .baseMipLevel = 0,
                                                                  .levelCount = 1,
                                                                  .baseArrayLayer = 0,
                                                                  .layerCount = ShadowMap::CascadeCount } };
        const vk::DependencyInfo dependencyInfo = { .dependencyFlags = {},
                                                    .imageMemoryBarrierCount = 1,
                                                    .pImageMemoryBarriers = &barrier };
        cmd.pipelineBarrier2(dependencyInfo);
    }

    // Transition picking image to color attachment optimal
    {
        const vk::PipelineStageFlags2 srcStageMask = s_Data->PickingImageLayout == vk::ImageLayout::eTransferSrcOptimal
                                                         ? vk::PipelineStageFlagBits2::eTransfer
                                                         : vk::PipelineStageFlagBits2::eColorAttachmentOutput;
        const vk::AccessFlags2 srcAccessMask = s_Data->PickingImageLayout == vk::ImageLayout::eTransferSrcOptimal
                                                   ? vk::AccessFlagBits2::eTransferRead
                                                   : vk::AccessFlagBits2::eColorAttachmentWrite;

        vk::ImageMemoryBarrier2 barrier = { .srcStageMask = srcStageMask,
                                            .srcAccessMask = srcAccessMask,
                                            .dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                                            .dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
                                            .oldLayout = vk::ImageLayout::eUndefined,
                                            //.oldLayout = s_Data->PickingImageLayout == vk::ImageLayout::eUndefined ?
                                            // vk::ImageLayout::eUndefined : s_Data->PickingImageLayout,
                                            .newLayout = vk::ImageLayout::eColorAttachmentOptimal,
                                            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                            .image = s_Data->PickingImage.Image,
                                            .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eColor,
                                                                  .baseMipLevel = 0,
                                                                  .levelCount = 1,
                                                                  .baseArrayLayer = 0,
                                                                  .layerCount = 1 } };
        const vk::DependencyInfo dependencyInfo = { .dependencyFlags = {},
                                                    .imageMemoryBarrierCount = 1,
                                                    .pImageMemoryBarriers = &barrier };
        cmd.pipelineBarrier2(dependencyInfo);
        s_Data->PickingImageLayout = vk::ImageLayout::eColorAttachmentOptimal;
    }

    // Transition color image layout for color attachment
    {
        vk::ImageMemoryBarrier2 barrier = { .srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                                            .srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
                                            .dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                                            .dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
                                            .oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
                                            .newLayout = vk::ImageLayout::eColorAttachmentOptimal,
                                            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                            .image = s_Data->ColorImage.Image,
                                            .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eColor,
                                                                  .baseMipLevel = 0,
                                                                  .levelCount = 1,
                                                                  .baseArrayLayer = 0,
                                                                  .layerCount = 1 } };
        const vk::DependencyInfo dependencyInfo = { .dependencyFlags = {},
                                                    .imageMemoryBarrierCount = 1,
                                                    .pImageMemoryBarriers = &barrier };
        cmd.pipelineBarrier2(dependencyInfo);
    }

    // Transition depth image to depth attachment optimal
    {
        vk::ImageMemoryBarrier2 barrier = { .srcStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                                                            vk::PipelineStageFlagBits2::eLateFragmentTests,
                                            .srcAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
                                            .dstStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                                                            vk::PipelineStageFlagBits2::eLateFragmentTests,
                                            .dstAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentRead |
                                                             vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
                                            .oldLayout = vk::ImageLayout::eDepthAttachmentOptimal,
                                            .newLayout = vk::ImageLayout::eDepthAttachmentOptimal,
                                            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                            .image = s_Data->DepthImage.Image,
                                            .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eDepth,
                                                                  .baseMipLevel = 0,
                                                                  .levelCount = 1,
                                                                  .baseArrayLayer = 0,
                                                                  .layerCount = 1 } };
        const vk::DependencyInfo dependencyInfo = { .dependencyFlags = {},
                                                    .imageMemoryBarrierCount = 1,
                                                    .pImageMemoryBarriers = &barrier };
        cmd.pipelineBarrier2(dependencyInfo);
    }

    const vk::Viewport viewport{ .x = 0.0f,
                                 .y = 0.0f,
                                 .width = s_Data->OutputSize.x,
                                 .height = s_Data->OutputSize.y,
                                 .minDepth = 0.0f,
                                 .maxDepth = 1.0f };

    const vk::Rect2D renderArea{ .offset = vk::Offset2D{ .x = 0, .y = 0 },
                                 .extent = vk::Extent2D{ .width = static_cast<uint32_t>(s_Data->OutputSize.x),
                                                         .height = static_cast<uint32_t>(s_Data->OutputSize.y) } };

    // Render opaque objects
    {
        WriteGPUTimestamp(cmd, frameIndex, static_cast<uint32_t>(GPUTimestampQuery::OpaqueBegin));

        vk::RenderingAttachmentInfo colorAttachmentInfo{ .imageView = s_Data->ColorImage.ImageView,
                                                         .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
                                                         .loadOp = vk::AttachmentLoadOp::eClear,
                                                         .storeOp = vk::AttachmentStoreOp::eStore,
                                                         .clearValue = vk::ClearColorValue{
                                                             std::array{ 0.0f, 0.0f, 0.0f, 1.0f } } };

        vk::RenderingAttachmentInfo pickingAttachmentInfo{ .imageView = s_Data->PickingImage.ImageView,
                                                           .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
                                                           .loadOp = vk::AttachmentLoadOp::eClear,
                                                           .storeOp = vk::AttachmentStoreOp::eStore,
                                                           .clearValue = vk::ClearColorValue{ std::array{
                                                               std::numeric_limits<uint32_t>::max(), 0u, 0u, 0u } } };

        const std::array<vk::RenderingAttachmentInfo, 2> colorAttachments = { colorAttachmentInfo,
                                                                              pickingAttachmentInfo };

        vk::RenderingAttachmentInfo depthAttachmentInfo{
            .imageView = s_Data->DepthImage.ImageView,
            .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
            .loadOp = vk::AttachmentLoadOp::eLoad,
            .storeOp = vk::AttachmentStoreOp::eStore,
        };

        const vk::RenderingInfo renderingInfo{ .renderArea = renderArea,
                                               .layerCount = 1,
                                               .colorAttachmentCount = static_cast<uint32_t>(colorAttachments.size()),
                                               .pColorAttachments = colorAttachments.data(),
                                               .pDepthAttachment = &depthAttachmentInfo };

        BeginRenderPassDebugLabel(cmd, "Opaque Pass");
        cmd.beginRendering(renderingInfo);

        cmd.setViewport(0, viewport);

        cmd.setScissor(0, renderArea);

        if (s_Data->Skybox.ShowSkybox && s_Data->Skybox.SkyboxMesh) {
            s_Data->SkyboxPipeline->Bind(cmd);
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                   *s_Data->PBRPipelineLayout,
                                   0,
                                   *s_Data->DescriptorSets[currentImage].skybox,
                                   { 0 });

            s_Data->Skybox.SkyboxMesh->Draw(cmd);
        }

        {
            if (s_Data->UseRayQueryBasedShadows) {
                const auto& pipeline = GetUseRayQueryBasedSoftShadows() ? s_Data->PBRRayQuerySoftShadowsPipeline
                                                                        : s_Data->PBRRayQueryShadowsPipeline;
                pipeline->Bind(cmd);
            }
            else {
                const auto& opaquePipeline =
                    GetIsPCFEnabledForShadowMap() ? s_Data->PBROpaquePipelinePCF : s_Data->PBROpaquePipeline;
                opaquePipeline->Bind(cmd);
            }

            for (const auto& renderObject : renderObjects) {
                if (renderObject.Material != nullptr && renderObject.Material->IsTransparent())
                    continue;

                uint32_t dynamicOffset = static_cast<uint32_t>(renderObject.UBOIndex * s_Data->DynamicAlignment);

                cmd.bindDescriptorSets(
                    vk::PipelineBindPoint::eGraphics,
                    *s_Data->PBRPipelineLayout,
                    0,
                    { s_Data->DescriptorSets[currentImage].scene, s_Data->TextureManager.GetGlobalDescriptorSet() },
                    { dynamicOffset });

                renderObject.Mesh->Draw(cmd);
            }
        }

        if (s_Data->DisplayDebugNormals) {
            s_Data->NormalDebugPipeline->Bind(cmd);

            for (const auto& renderObject : renderObjects) {
                const uint32_t dynamicOffset = static_cast<uint32_t>(renderObject.UBOIndex * s_Data->DynamicAlignment);

                cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                       *s_Data->PBRPipelineLayout,
                                       0,
                                       { s_Data->DescriptorSets[currentImage].scene },
                                       { dynamicOffset });

                renderObject.Mesh->Draw(cmd);
            }
        }

        cmd.endRendering();
        EndRenderPassDebugLabel(cmd);

        WriteGPUTimestamp(cmd, frameIndex, static_cast<uint32_t>(GPUTimestampQuery::OpaqueEnd));

        Log::CoreTrace("Opaque pass done!");
    }

    RenderParticles(cmd, frameIndex);

    cmd.endQuery(s_Data->PipelineStatisticsQueryPools[frameIndex], 0);

    cmd.beginQuery(s_Data->MeshPipelineStatisticsQueryPools[frameIndex], 0, {});

    RenderGrass(cmd, frameIndex);

    cmd.endQuery(s_Data->MeshPipelineStatisticsQueryPools[frameIndex], 0);

    cmd.beginQuery(s_Data->PipelineStatisticsQueryPools[frameIndex], 1, {});

    // Transfer accumulation, revealage and distortion images to color attachment optimal for they will be render
    // targets
    {
        std::array<vk::ImageMemoryBarrier2, 3> barriers;
        for (size_t i = 0; i < barriers.size(); ++i) {
            barriers[i] = { .srcStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
                            .srcAccessMask = vk::AccessFlagBits2::eShaderRead,

                            .dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                            .dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,

                            .oldLayout = vk::ImageLayout::eUndefined,
                            .newLayout = vk::ImageLayout::eColorAttachmentOptimal,

                            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                            .image = (i == 0)   ? s_Data->Transparency.AccumulationImage.Image
                                     : (i == 1) ? s_Data->Transparency.RevealageImage.Image
                                                : s_Data->Transparency.DistortionImage.Image,
                            .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eColor,
                                                  .baseMipLevel = 0,
                                                  .levelCount = 1,
                                                  .baseArrayLayer = 0,
                                                  .layerCount = 1 } };
        }

        const vk::DependencyInfo dependencyInfo = { .dependencyFlags = {},
                                                    .imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size()),
                                                    .pImageMemoryBarriers = barriers.data() };
        cmd.pipelineBarrier2(dependencyInfo);
    }

    // Render transparent objects
    {
        WriteGPUTimestamp(cmd, frameIndex, static_cast<uint32_t>(GPUTimestampQuery::TransparentBegin));

        vk::RenderingAttachmentInfo accumulationAttachmentInfo{
            .imageView = s_Data->Transparency.AccumulationImage.ImageView,
            .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .loadOp = vk::AttachmentLoadOp::eClear,
            .storeOp = vk::AttachmentStoreOp::eStore,
            .clearValue = vk::ClearColorValue{ std::array{ 0.0f, 0.0f, 0.0f, 0.0f } }
        };

        vk::RenderingAttachmentInfo revealageAttachmentInfo{ .imageView = s_Data->Transparency.RevealageImage.ImageView,
                                                             .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
                                                             .loadOp = vk::AttachmentLoadOp::eClear,
                                                             .storeOp = vk::AttachmentStoreOp::eStore,
                                                             .clearValue = vk::ClearColorValue{
                                                                 std::array{ 1.0f, 1.0f, 1.0f, 1.0f } } };

        vk::RenderingAttachmentInfo distortionAttachmentInfo{
            .imageView = s_Data->Transparency.DistortionImage.ImageView,
            .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .loadOp = vk::AttachmentLoadOp::eClear,
            .storeOp = vk::AttachmentStoreOp::eStore,
            .clearValue = vk::ClearColorValue{ std::array{ 0.0f, 0.0f, 0.0f, 0.0f } }
        };

        const std::array<vk::RenderingAttachmentInfo, 3> colorAttachments = { accumulationAttachmentInfo,
                                                                              revealageAttachmentInfo,
                                                                              distortionAttachmentInfo };

        vk::RenderingAttachmentInfo depthAttachmentInfo{
            .imageView = s_Data->DepthImage.ImageView,
            .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
            .loadOp = vk::AttachmentLoadOp::eLoad,
            .storeOp = vk::AttachmentStoreOp::eDontCare,
        };

        const vk::RenderingInfo renderingInfo{ .renderArea = renderArea,
                                               .layerCount = 1,
                                               .colorAttachmentCount = static_cast<uint32_t>(colorAttachments.size()),
                                               .pColorAttachments = colorAttachments.data(),
                                               .pDepthAttachment = &depthAttachmentInfo };

        BeginRenderPassDebugLabel(cmd, "Transparent Pass");
        cmd.beginRendering(renderingInfo);

        cmd.setViewport(0, viewport);

        cmd.setScissor(0, renderArea);

        s_Data->Transparency.MainPipeline->Bind(cmd);

        for (const auto& renderObject : renderObjects) {
            if (renderObject.Material != nullptr && !renderObject.Material->IsTransparent())
                continue;

            // In the case of opaque objects, we want to render them with default magenta material,
            // but of course they shouldn't be rendered in the transparent pass, so we skip them here.
            if (renderObject.Material == nullptr)
                continue;

            uint32_t dynamicOffset = static_cast<uint32_t>(renderObject.UBOIndex * s_Data->DynamicAlignment);

            cmd.bindDescriptorSets(
                vk::PipelineBindPoint::eGraphics,
                *s_Data->PBRPipelineLayout,
                0,
                { s_Data->DescriptorSets[currentImage].scene, s_Data->TextureManager.GetGlobalDescriptorSet() },
                { dynamicOffset });

            renderObject.Mesh->Draw(cmd);
        }

        cmd.endRendering();
        EndRenderPassDebugLabel(cmd);

        WriteGPUTimestamp(cmd, frameIndex, static_cast<uint32_t>(GPUTimestampQuery::TransparentEnd));

        Log::CoreTrace("Transparent pass done!");
    }

    if (s_Data->DisplayPhysicsColliders && !colliderLineVertices.empty()) {
        constexpr uint32_t maxVertexCount = ColliderDebugHelpers::ColliderDebugMaxVertices;
        const uint32_t vertexCount =
            std::min<uint32_t>(static_cast<uint32_t>(colliderLineVertices.size()), maxVertexCount);
        if (vertexCount > 0) {
            auto& lineBuffer = s_Data->ColliderLineBuffers[currentImage];
            std::memcpy(lineBuffer.MappedData, colliderLineVertices.data(), sizeof(LineVertex) * vertexCount);

            vk::RenderingAttachmentInfo colorAttachmentInfo{
                .imageView = s_Data->ColorImage.ImageView,
                .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
                .loadOp = vk::AttachmentLoadOp::eLoad,
                .storeOp = vk::AttachmentStoreOp::eStore,
            };

            vk::RenderingAttachmentInfo depthAttachmentInfo{
                .imageView = s_Data->DepthImage.ImageView,
                .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
                .loadOp = vk::AttachmentLoadOp::eLoad,
                .storeOp = vk::AttachmentStoreOp::eDontCare,
            };

            const vk::RenderingInfo renderingInfo{ .renderArea = renderArea,
                                                   .layerCount = 1,
                                                   .colorAttachmentCount = 1,
                                                   .pColorAttachments = &colorAttachmentInfo,
                                                   .pDepthAttachment = &depthAttachmentInfo };

            BeginRenderPassDebugLabel(cmd, "Collider Debug Pass");
            cmd.beginRendering(renderingInfo);
            cmd.setViewport(0, viewport);
            cmd.setScissor(0, renderArea);

            s_Data->ColliderLinesPipeline->Bind(cmd);
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                   *s_Data->PBRPipelineLayout,
                                   0,
                                   { s_Data->DescriptorSets[currentImage].scene },
                                   { 0 });
            cmd.bindVertexBuffers(0, *lineBuffer.Buffer, { 0 });
            cmd.draw(vertexCount, 1, 0, 0);

            cmd.endRendering();
            EndRenderPassDebugLabel(cmd);
        }
    }

    // Transition all images needed for the resolve pass to shader read optimal (color, depth, accumulation, revealage,
    // distortion) and transition resolve image to color attachment optimal
    {
        std::array<vk::ImageMemoryBarrier2, 6> barriers;
        std::array<vk::ImageLayout, 6> oldLayouts = {
            vk::ImageLayout::eColorAttachmentOptimal, // Color image
            vk::ImageLayout::eDepthAttachmentOptimal, // Depth image
            vk::ImageLayout::eColorAttachmentOptimal, // Accumulation image
            vk::ImageLayout::eColorAttachmentOptimal, // Revealage image
            vk::ImageLayout::eColorAttachmentOptimal, // Distortion image
            vk::ImageLayout::eShaderReadOnlyOptimal   // Resolve image
        };
        std::array<vk::ImageLayout, 6> newLayouts = {
            vk::ImageLayout::eShaderReadOnlyOptimal, // Color image
            vk::ImageLayout::eShaderReadOnlyOptimal, // Depth image
            vk::ImageLayout::eShaderReadOnlyOptimal, // Accumulation image
            vk::ImageLayout::eShaderReadOnlyOptimal, // Revealage image
            vk::ImageLayout::eShaderReadOnlyOptimal, // Distortion image
            vk::ImageLayout::eColorAttachmentOptimal // Resolve image
        };
        std::array<vk::AccessFlags2, 6> srcAccessMasks = {
            vk::AccessFlagBits2::eColorAttachmentWrite,        // Color image
            vk::AccessFlagBits2::eDepthStencilAttachmentWrite, // Depth image
            vk::AccessFlagBits2::eColorAttachmentWrite,        // Accumulation image
            vk::AccessFlagBits2::eColorAttachmentWrite,        // Revealage image
            vk::AccessFlagBits2::eColorAttachmentWrite,        // Distortion image
            vk::AccessFlagBits2::eColorAttachmentWrite         // Resolve image
        };
        std::array<vk::PipelineStageFlags2, 6> srcStageMasks = {
            vk::PipelineStageFlagBits2::eColorAttachmentOutput, // Color image
            vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                vk::PipelineStageFlagBits2::eLateFragmentTests, // Depth image
            vk::PipelineStageFlagBits2::eColorAttachmentOutput, // Accumulation image
            vk::PipelineStageFlagBits2::eColorAttachmentOutput, // Revealage image
            vk::PipelineStageFlagBits2::eColorAttachmentOutput, // Distortion image
            vk::PipelineStageFlagBits2::eColorAttachmentOutput  // Resolve image
        };
        std::array<vk::AccessFlags2, 6> dstAccessMasks = {
            vk::AccessFlagBits2::eShaderRead,          // Color image
            vk::AccessFlagBits2::eShaderRead,          // Depth image
            vk::AccessFlagBits2::eShaderRead,          // Accumulation image
            vk::AccessFlagBits2::eShaderRead,          // Revealage image
            vk::AccessFlagBits2::eShaderRead,          // Distortion image
            vk::AccessFlagBits2::eColorAttachmentWrite // Resolve image
        };
        std::array<vk::PipelineStageFlags2, 6> dstStageMasks = {
            vk::PipelineStageFlagBits2::eFragmentShader,       // Color image
            vk::PipelineStageFlagBits2::eFragmentShader,       // Depth image
            vk::PipelineStageFlagBits2::eFragmentShader,       // Accumulation image
            vk::PipelineStageFlagBits2::eFragmentShader,       // Revealage image
            vk::PipelineStageFlagBits2::eFragmentShader,       // Distortion image
            vk::PipelineStageFlagBits2::eColorAttachmentOutput // Resolve image
        };
        std::array<vk::Image, 6> images = { *s_Data->ColorImage.Image,
                                            *s_Data->DepthImage.Image,
                                            *s_Data->Transparency.AccumulationImage.Image,
                                            *s_Data->Transparency.RevealageImage.Image,
                                            *s_Data->Transparency.DistortionImage.Image,
                                            *s_Data->ResolveImage.Image };

        for (size_t i = 0; i < barriers.size(); ++i) {
            barriers[i] = { .srcStageMask = srcStageMasks[i],
                            .srcAccessMask = srcAccessMasks[i],
                            .dstStageMask = dstStageMasks[i],
                            .dstAccessMask = dstAccessMasks[i],
                            .oldLayout = oldLayouts[i],
                            .newLayout = newLayouts[i],
                            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                            .image = images[i],
                            .subresourceRange = { .aspectMask = i == 1 ? vk::ImageAspectFlagBits::eDepth
                                                                       : vk::ImageAspectFlagBits::eColor,
                                                  .baseMipLevel = 0,
                                                  .levelCount = 1,
                                                  .baseArrayLayer = 0,
                                                  .layerCount = 1 } };
        }

        const vk::DependencyInfo dependencyInfo = { .dependencyFlags = {},
                                                    .imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size()),
                                                    .pImageMemoryBarriers = barriers.data() };
        cmd.pipelineBarrier2(dependencyInfo);
    }

    // Resolve transparency
    {
        WriteGPUTimestamp(cmd, frameIndex, static_cast<uint32_t>(GPUTimestampQuery::TransparencyResolveBegin));

        vk::RenderingAttachmentInfo colorAttachmentInfo{ .imageView = s_Data->ResolveImage.ImageView,
                                                         .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
                                                         .loadOp = vk::AttachmentLoadOp::eClear,
                                                         .storeOp = vk::AttachmentStoreOp::eStore,
                                                         .clearValue = vk::ClearColorValue{
                                                             std::array{ 0.0f, 0.0f, 0.0f, 1.0f } } };
        const vk::RenderingInfo renderingInfo{ .renderArea = renderArea,
                                               .layerCount = 1,
                                               .colorAttachmentCount = 1,
                                               .pColorAttachments = &colorAttachmentInfo,
                                               .pDepthAttachment = nullptr };
        BeginRenderPassDebugLabel(cmd, "Transparency Resolve Pass");
        cmd.beginRendering(renderingInfo);
        cmd.setViewport(0, viewport);
        cmd.setScissor(0, renderArea);

        s_Data->TransparencyResolvePipeline->Bind(cmd);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                               *s_Data->TransparencyResolvePipelineLayout,
                               0,
                               *s_Data->DescriptorSets[currentImage].resolve,
                               {});
        cmd.draw(3, 1, 0, 0);

        cmd.endRendering();
        EndRenderPassDebugLabel(cmd);

        WriteGPUTimestamp(cmd, frameIndex, static_cast<uint32_t>(GPUTimestampQuery::TransparencyResolveEnd));

        Log::CoreTrace("Transparency resolve pass done!");
    }

    ApplyBloom(cmd, currentImage);

    ApplyTonemapping(cmd, currentImage);

    ApplyAntiAliasing(cmd, currentImage);

    HandleMousePickingReadback(cmd);

    // Transition composite image layout for shader read in ImGui
    {
        vk::ImageMemoryBarrier2 barrier = { .srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                                            .srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
                                            .dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
                                            .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
                                            .oldLayout = vk::ImageLayout::eColorAttachmentOptimal,
                                            .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
                                            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                            .image = s_Data->CompositeImage.Image,
                                            .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eColor,
                                                                  .baseMipLevel = 0,
                                                                  .levelCount = 1,
                                                                  .baseArrayLayer = 0,
                                                                  .layerCount = 1 } };
        const vk::DependencyInfo dependencyInfo = { .dependencyFlags = {},
                                                    .imageMemoryBarrierCount = 1,
                                                    .pImageMemoryBarriers = &barrier };
        cmd.pipelineBarrier2(dependencyInfo);

        Log::CoreTrace("Resolve image transitioned for ImGui!");
    }

    WriteGPUTimestamp(cmd, frameIndex, static_cast<uint32_t>(GPUTimestampQuery::FrameEnd));

    cmd.endQuery(s_Data->PipelineStatisticsQueryPools[frameIndex], 1);

    s_Data->PendingRender.IsValid = false;
}

void Renderer::CreateDefaultMaterials()
{
    s_Data->MaterialRegistry.Add("Gold",
                                 CreateRef<Material>("Gold", glm::vec4(1.0f, 0.765557f, 0.336057f, 1.0f), 0.1f, 1.0f));
    s_Data->MaterialRegistry.Add(
        "Copper", CreateRef<Material>("Copper", glm::vec4(0.955008f, 0.637427f, 0.538163f, 1.0f), 0.1f, 1.0f));
    s_Data->MaterialRegistry.Add(
        "Chromium", CreateRef<Material>("Chromium", glm::vec4(0.549585f, 0.556114f, 0.554256f, 1.0f), 0.1f, 1.0f));
    s_Data->MaterialRegistry.Add(
        "Nickel", CreateRef<Material>("Nickel", glm::vec4(0.659777f, 0.608679f, 0.525649f, 1.0f), 0.1f, 1.0f));
    s_Data->MaterialRegistry.Add(
        "Titanium", CreateRef<Material>("Titanium", glm::vec4(0.541931f, 0.496791f, 0.449419f, 1.0f), 0.1f, 1.0f));
    s_Data->MaterialRegistry.Add(
        "Cobalt", CreateRef<Material>("Cobalt", glm::vec4(0.662124f, 0.654864f, 0.633732f, 1.0f), 0.1f, 1.0f));
    s_Data->MaterialRegistry.Add(
        "Platinum", CreateRef<Material>("Platinum", glm::vec4(0.672411f, 0.637331f, 0.585456f, 1.0f), 0.1f, 1.0f));
    s_Data->MaterialRegistry.Add("White", CreateRef<Material>("White", glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), 1.0f, 0.0f));
    s_Data->MaterialRegistry.Add("Red", CreateRef<Material>("Red", glm::vec4(1.0f, 0.0f, 0.0f, 1.0f), 0.1f, 1.0f));
    s_Data->MaterialRegistry.Add("Blue", CreateRef<Material>("Blue", glm::vec4(0.0f, 0.0f, 1.0f, 1.0f), 0.1f, 1.0f));
    s_Data->MaterialRegistry.Add("Black", CreateRef<Material>("Black", glm::vec4(0.0f, 0.0f, 0.0f, 1.0f), 0.1f, 1.0f));
    s_Data->MaterialRegistry.Add("DebugPink",
                                 CreateRef<Material>("DebugPink", glm::vec4(1.0f, 0.0f, 1.0f, 1.0f), 1.0f, 0.1f));
    s_Data->MaterialRegistry.Add("Water", CreateRef<Material>("Water", glm::vec4(0.0f, 0.0f, 1.0f, 0.5f), 1.0f, 0.1f));
}

void Renderer::CreateResources()
{
    // TODO: Set default skybox texture if none is set by the user
    /*s_Data->Skybox.SkyboxTexture = TextureCube::FromFile(
        "assets/textures/hdr/pisa_cube.ktx",
        vk::Format::eR16G16B16A16Sfloat,
        vk::ImageUsageFlagBits::eSampled
    );*/
    s_Data->Skybox.SkyboxTexture = TextureCube::FromFile("Assets/Textures/hdr/pisa_cube.ktx2");

    CreateSkyboxResources();

    PrepareUniformBuffers();
    PrepareStorageBuffers();

    auto& context = VulkanContext::Get();
    const auto& device = context.GetDevice();

    constexpr vk::DeviceSize colliderLineBufferSize = sizeof(LineVertex) * ColliderDebugHelpers::ColliderDebugMaxVertices;
    for (auto& [Buffer, Memory, MappedData] : s_Data->ColliderLineBuffers) {
        CreateBuffer(device,
                     colliderLineBufferSize,
                     vk::BufferUsageFlagBits::eVertexBuffer,
                     vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
                     Buffer,
                     Memory);
        MappedData = Memory.mapMemory(0, colliderLineBufferSize);
    }

    constexpr vk::SemaphoreTypeCreateInfo timelineSemaphoreTypeInfo{ .semaphoreType = vk::SemaphoreType::eTimeline,
                                                                     .initialValue = 0 };
    const vk::SemaphoreCreateInfo timelineSemaphoreCreateInfo{ .pNext = &timelineSemaphoreTypeInfo };
    s_Data->MousePickingReadback.TimelineSemaphore = vk::raii::Semaphore(device, timelineSemaphoreCreateInfo);
    context.SetObjectDebugName(s_Data->MousePickingReadback.TimelineSemaphore, "Mouse Picking Timeline Semaphore");

    const auto& queueFamilyInfo = context.GetQueueFamilyInfo();
    const auto queueFamilyProperties = context.GetPhysicalDevice().getQueueFamilyProperties();

    s_Data->SupportsGPUTimestamps = false;
    if (queueFamilyInfo.graphics < queueFamilyProperties.size()) {
        s_Data->SupportsGPUTimestamps = queueFamilyProperties[queueFamilyInfo.graphics].timestampValidBits > 0;
    }

    s_Data->GPUTimestampPeriodNanoseconds = context.GetProperties().properties.limits.timestampPeriod;

    if (s_Data->SupportsGPUTimestamps) {
        s_Data->GPUTimestampQueryPools.clear();
        s_Data->GPUTimestampQueryPools.reserve(context.GetMaxFramesInFlight());

        constexpr vk::QueryPoolCreateInfo queryPoolInfo{ .flags = {}, // vk::QueryPoolCreateFlagBits::eResetKHR,
                                                         .queryType = vk::QueryType::eTimestamp,
                                                         .queryCount =
                                                             static_cast<uint32_t>(GPUTimestampQuery::Count) };

        context.Submit(VulkanContext::OperationType::Graphics, [&](const vk::raii::CommandBuffer& cmd) {
            for (uint32_t i = 0; i < context.GetMaxFramesInFlight(); ++i) {
                s_Data->GPUTimestampQueryPools.emplace_back(device, queryPoolInfo);
                context.SetObjectDebugName(s_Data->GPUTimestampQueryPools.back(),
                                           "Renderer GPU Timestamp Query Pool[" + std::to_string(i) + "]");
                // Reset query pool at the beginning so that we can immediately start using it without waiting for the
                // first render to reset it
                cmd.resetQueryPool(
                    s_Data->GPUTimestampQueryPools.back(), 0, static_cast<uint32_t>(GPUTimestampQuery::Count));
            }
        });
    }

    // TODO: Query from VulkanContext
    s_Data->SupportsPipelineStatistics = true;

    if (s_Data->SupportsPipelineStatistics) {
        s_Data->PipelineStatisticsQueryPools.clear();
        s_Data->PipelineStatisticsQueryPools.reserve(context.GetMaxFramesInFlight());

        constexpr vk::QueryPoolCreateInfo queryPoolInfo{
            .flags = {}, // vk::QueryPoolCreateFlagBits::eResetKHR,
            .queryType = vk::QueryType::ePipelineStatistics,
            .queryCount = 2,
            .pipelineStatistics = vk::QueryPipelineStatisticFlagBits::eInputAssemblyVertices |
                                  vk::QueryPipelineStatisticFlagBits::eInputAssemblyPrimitives |
                                  vk::QueryPipelineStatisticFlagBits::eVertexShaderInvocations |
                                  vk::QueryPipelineStatisticFlagBits::eFragmentShaderInvocations
        };

        s_Data->MeshPipelineStatisticsQueryPools.clear();
        s_Data->MeshPipelineStatisticsQueryPools.reserve(context.GetMaxFramesInFlight());

        constexpr vk::QueryPoolCreateInfo meshQueryPoolInfo{
            .flags = {}, // vk::QueryPoolCreateFlagBits::eResetKHR,
            .queryType = vk::QueryType::ePipelineStatistics,
            .queryCount = 1,
            .pipelineStatistics = vk::QueryPipelineStatisticFlagBits::eTaskShaderInvocationsEXT |
                                  vk::QueryPipelineStatisticFlagBits::eMeshShaderInvocationsEXT |
                                  vk::QueryPipelineStatisticFlagBits::eFragmentShaderInvocations
        };

        context.Submit(VulkanContext::OperationType::Graphics, [&](const vk::raii::CommandBuffer& cmd) {
            for (uint32_t i = 0; i < context.GetMaxFramesInFlight(); ++i) {
                s_Data->PipelineStatisticsQueryPools.emplace_back(device, queryPoolInfo);
                context.SetObjectDebugName(s_Data->PipelineStatisticsQueryPools.back(),
                                           "Renderer Pipeline Statistics Query Pool[" + std::to_string(i) + "]");
                // Reset query pool at the beginning so that we can immediately start using it without waiting for the
                // first render to reset it
                cmd.resetQueryPool(s_Data->PipelineStatisticsQueryPools.back(), 0, 2);

                s_Data->MeshPipelineStatisticsQueryPools.emplace_back(device, meshQueryPoolInfo);
                context.SetObjectDebugName(s_Data->MeshPipelineStatisticsQueryPools.back(),
                                           "Renderer Mesh Pipeline Statistics Query Pool[" + std::to_string(i) + "]");
                // Reset query pool at the beginning so that we can immediately start using it without waiting for the
                // first render to reset it
                cmd.resetQueryPool(s_Data->MeshPipelineStatisticsQueryPools.back(), 0, 1);
            }
        });
    }

    // Create samplers
    {
        const vk::SamplerCreateInfo samplerInfo{ .magFilter = vk::Filter::eLinear,
                                                 .minFilter = vk::Filter::eLinear,
                                                 .mipmapMode = vk::SamplerMipmapMode::eLinear,
                                                 .addressModeU = vk::SamplerAddressMode::eRepeat,
                                                 .addressModeV = vk::SamplerAddressMode::eRepeat,
                                                 .addressModeW = vk::SamplerAddressMode::eRepeat,
                                                 .mipLodBias = 0.0f,
                                                 .anisotropyEnable = vk::True,
                                                 .maxAnisotropy = context.GetMaxAnisotropy(),
                                                 .compareEnable = vk::False,
                                                 .compareOp = vk::CompareOp::eAlways,
                                                 .minLod = 0.0f,
                                                 .maxLod = vk::LodClampNone,
                                                 .borderColor = vk::BorderColor::eIntOpaqueBlack,
                                                 .unnormalizedCoordinates = vk::False };
        s_Data->ColorSampler = vk::raii::Sampler{ device, samplerInfo };

        context.SetObjectDebugName(s_Data->ColorSampler, "Color Texture Sampler");

        const vk::SamplerCreateInfo shadowSamplerInfo{ .magFilter = vk::Filter::eLinear,
                                                       .minFilter = vk::Filter::eLinear,
                                                       .mipmapMode = vk::SamplerMipmapMode::eLinear,
                                                       .addressModeU = vk::SamplerAddressMode::eClampToBorder,
                                                       .addressModeV = vk::SamplerAddressMode::eClampToBorder,
                                                       .addressModeW = vk::SamplerAddressMode::eClampToBorder,
                                                       .mipLodBias = 0.0f,
                                                       .anisotropyEnable = vk::True,
                                                       .maxAnisotropy = context.GetMaxAnisotropy(),
                                                       .compareEnable = vk::True,
                                                       .compareOp = vk::CompareOp::eGreaterOrEqual, // eLessOrEqual,
                                                       .minLod = 0.0f,
                                                       .maxLod = 1.0f,
                                                       .borderColor = vk::BorderColor::eFloatOpaqueWhite,
                                                       .unnormalizedCoordinates = vk::False };
        s_Data->ShadowMapSampler = vk::raii::Sampler{ device, shadowSamplerInfo };

        context.SetObjectDebugName(s_Data->ShadowMapSampler, "Shadow Map Sampler");

        const vk::SamplerCreateInfo linearSamplerInfo{ .magFilter = vk::Filter::eLinear,
                                                       .minFilter = vk::Filter::eLinear,
                                                       .mipmapMode = vk::SamplerMipmapMode::eLinear,
                                                       .addressModeU = vk::SamplerAddressMode::eClampToEdge,
                                                       .addressModeV = vk::SamplerAddressMode::eClampToEdge,
                                                       .addressModeW = vk::SamplerAddressMode::eClampToEdge,
                                                       .mipLodBias = 0.0f,
                                                       .anisotropyEnable = vk::False,
                                                       .maxAnisotropy = context.GetMaxAnisotropy(),
                                                       .compareEnable = vk::False,
                                                       .compareOp = vk::CompareOp::eAlways,
                                                       .minLod = 0.0f,
                                                       .maxLod = 0.0f,
                                                       .borderColor = vk::BorderColor::eIntOpaqueBlack,
                                                       .unnormalizedCoordinates = vk::False };
        s_Data->LinearSampler = vk::raii::Sampler{ device, linearSamplerInfo };

        context.SetObjectDebugName(s_Data->LinearSampler, "Linear Sampler");

        const vk::SamplerCreateInfo pointSamplerInfo{ .magFilter = vk::Filter::eNearest,
                                                      .minFilter = vk::Filter::eNearest,
                                                      .mipmapMode = vk::SamplerMipmapMode::eNearest,
                                                      .addressModeU = vk::SamplerAddressMode::eClampToEdge,
                                                      .addressModeV = vk::SamplerAddressMode::eClampToEdge,
                                                      .addressModeW = vk::SamplerAddressMode::eClampToEdge,
                                                      .mipLodBias = 0.0f,
                                                      .anisotropyEnable = vk::False,
                                                      .maxAnisotropy = context.GetMaxAnisotropy(),
                                                      .compareEnable = vk::False,
                                                      .compareOp = vk::CompareOp::eAlways,
                                                      .minLod = 0.0f,
                                                      .maxLod = 0.0f,
                                                      .borderColor = vk::BorderColor::eIntOpaqueBlack,
                                                      .unnormalizedCoordinates = vk::False };
        s_Data->PointSampler = vk::raii::Sampler{ device, pointSamplerInfo };

        context.SetObjectDebugName(s_Data->PointSampler, "Point Sampler");
    }

    // Setup resources for GTAO
    {
        // Create storage image for gtao data
        s_Data->GTAOImage.Format = context.FindSupportedFormat({ vk::Format::eR8Unorm },
                                                               vk::ImageTiling::eOptimal,
                                                               vk::FormatFeatureFlagBits::eStorageImage |
                                                                   vk::FormatFeatureFlagBits::eSampledImage);
        s_Data->GTAOScratchImage.Format = s_Data->GTAOImage.Format;

        constexpr uint32_t gtaoImageWidth = 512;
        constexpr uint32_t gtaoImageHeight = 512;

        CreateGTAOImage(gtaoImageWidth, gtaoImageHeight);

        std::vector<vk::DescriptorSetLayoutBinding> bindings = {
            vk::DescriptorSetLayoutBinding{ // Constants uniform buffer
                                            .binding = 0,
                                            .descriptorType = vk::DescriptorType::eUniformBuffer,
                                            .descriptorCount = 1,
                                            .stageFlags = vk::ShaderStageFlagBits::eCompute,
                                            .pImmutableSamplers = nullptr },
            vk::DescriptorSetLayoutBinding{ // Depth texture
                                            .binding = 1,
                                            .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                            .descriptorCount = 1,
                                            .stageFlags = vk::ShaderStageFlagBits::eCompute,
                                            .pImmutableSamplers = nullptr },
            vk::DescriptorSetLayoutBinding{ // Normal texture
                                            .binding = 2,
                                            .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                            .descriptorCount = 1,
                                            .stageFlags = vk::ShaderStageFlagBits::eCompute,
                                            .pImmutableSamplers = nullptr },
            vk::DescriptorSetLayoutBinding{ // AO storage image
                                            .binding = 3,
                                            .descriptorType = vk::DescriptorType::eStorageImage,
                                            .descriptorCount = 1,
                                            .stageFlags = vk::ShaderStageFlagBits::eCompute,
                                            .pImmutableSamplers = nullptr },
        };

        const vk::DescriptorSetLayoutCreateInfo layoutInfo{ .bindingCount = static_cast<uint32_t>(bindings.size()),
                                                            .pBindings = bindings.data() };

        s_Data->DescriptorSetLayouts.gtao = vk::raii::DescriptorSetLayout{ device, layoutInfo };
        context.SetObjectDebugName(s_Data->DescriptorSetLayouts.gtao, "GTAO Descriptor Set Layout");

        const std::array setLayouts = { *s_Data->DescriptorSetLayouts.gtao };

        vk::PipelineLayoutCreateInfo gtaoPipelineLayoutInfo{ .setLayoutCount = static_cast<uint32_t>(setLayouts.size()),
                                                             .pSetLayouts = setLayouts.data(),
                                                             .pushConstantRangeCount = 0,
                                                             .pPushConstantRanges = nullptr };

        s_Data->GTAOPipelineLayout = vk::raii::PipelineLayout{ device, gtaoPipelineLayoutInfo };
        context.SetObjectDebugName(s_Data->GTAOPipelineLayout, "GTAO Pipeline Layout");

        ComputePipelineSpecification gtaoPipelineSpec{};
        gtaoPipelineSpec.Name = "GTAO Pipeline";
        gtaoPipelineSpec.Shader = CreateRef<Shader>("gtao", "GTAO");
        gtaoPipelineSpec.PipelineLayout = *s_Data->GTAOPipelineLayout;

        s_Data->GTAOPipeline = CreateRef<ComputePipeline>(gtaoPipelineSpec);

        // Setting up the cross-bilateral blur resources for GTAO

        std::vector<vk::DescriptorSetLayoutBinding> blurBindings = {
            vk::DescriptorSetLayoutBinding{ // Input AO texture
                                            .binding = 0,
                                            .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                            .descriptorCount = 1,
                                            .stageFlags = vk::ShaderStageFlagBits::eCompute,
                                            .pImmutableSamplers = nullptr },
            vk::DescriptorSetLayoutBinding{ // Depth texture
                                            .binding = 1,
                                            .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                            .descriptorCount = 1,
                                            .stageFlags = vk::ShaderStageFlagBits::eCompute,
                                            .pImmutableSamplers = nullptr },
            vk::DescriptorSetLayoutBinding{ // Normal texture
                                            .binding = 2,
                                            .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                            .descriptorCount = 1,
                                            .stageFlags = vk::ShaderStageFlagBits::eCompute,
                                            .pImmutableSamplers = nullptr },
            vk::DescriptorSetLayoutBinding{ // Output AO storage image
                                            .binding = 3,
                                            .descriptorType = vk::DescriptorType::eStorageImage,
                                            .descriptorCount = 1,
                                            .stageFlags = vk::ShaderStageFlagBits::eCompute,
                                            .pImmutableSamplers = nullptr },
        };

        const vk::DescriptorSetLayoutCreateInfo blurLayoutInfo{ .bindingCount =
                                                                    static_cast<uint32_t>(blurBindings.size()),
                                                                .pBindings = blurBindings.data() };

        s_Data->DescriptorSetLayouts.crossBilateralBlur = vk::raii::DescriptorSetLayout{ device, blurLayoutInfo };
        context.SetObjectDebugName(s_Data->DescriptorSetLayouts.crossBilateralBlur,
                                   "Cross-Bilateral Blur Descriptor Set Layout");

        const std::array blurSetLayouts = { *s_Data->DescriptorSetLayouts.crossBilateralBlur };

        constexpr vk::PushConstantRange blurPushConstantRange{ .stageFlags = vk::ShaderStageFlagBits::eCompute,
                                                               .offset = 0,
                                                               .size = sizeof(CrossBilateralBlurConstants) };

        vk::PipelineLayoutCreateInfo blurPipelineLayoutInfo{ .setLayoutCount =
                                                                 static_cast<uint32_t>(blurSetLayouts.size()),
                                                             .pSetLayouts = blurSetLayouts.data(),
                                                             .pushConstantRangeCount = 1,
                                                             .pPushConstantRanges = &blurPushConstantRange };

        s_Data->CrossBilateralBlurPipelineLayout = vk::raii::PipelineLayout{ device, blurPipelineLayoutInfo };
        context.SetObjectDebugName(s_Data->CrossBilateralBlurPipelineLayout, "Cross-Bilateral Blur Pipeline Layout");

        ComputePipelineSpecification blurPipelineSpec{};
        blurPipelineSpec.Name = "Cross-Bilateral Blur Pipeline";
        blurPipelineSpec.Shader = CreateRef<Shader>("spatial_cross_bilateral_blur", "Cross-Bilateral Blur");
        blurPipelineSpec.PipelineLayout = *s_Data->CrossBilateralBlurPipelineLayout;

        s_Data->CrossBilateralBlurPipeline = CreateRef<ComputePipeline>(blurPipelineSpec);
    }

    // Create the shadow map resources
    {
        // Create shadow map image
        s_Data->ShadowMap.Format = context.FindSupportedFormat({ vk::Format::eD32Sfloat },
                                                               vk::ImageTiling::eOptimal,
                                                               vk::FormatFeatureFlagBits::eDepthStencilAttachment |
                                                                   vk::FormatFeatureFlagBits::eSampledImage);

        constexpr uint32_t shadowMapMipLevels = 1;

        vk::ImageCreateInfo imageInfo{
            //.flags = vk::ImageCreateFlagBits::e2DArrayCompatible,
            .imageType = vk::ImageType::e2D,
            .format = s_Data->ShadowMap.Format,
            .extent = { .width = s_Data->ShadowMap.Size, .height = s_Data->ShadowMap.Size, .depth = 1 },
            .mipLevels = shadowMapMipLevels,
            .arrayLayers = ShadowMap::CascadeCount,
            .samples = vk::SampleCountFlagBits::e1,
            .tiling = vk::ImageTiling::eOptimal,
            .usage = vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled,
        };

        s_Data->ShadowMap.Image = vk::raii::Image(device, imageInfo);

        const vk::MemoryRequirements memRequirements = s_Data->ShadowMap.Image.getMemoryRequirements();
        const vk::MemoryAllocateInfo allocInfo{
            .allocationSize = memRequirements.size,
            .memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal)
        };

        s_Data->ShadowMap.ImageMemory = vk::raii::DeviceMemory(device, allocInfo);
        s_Data->ShadowMap.Image.bindMemory(*s_Data->ShadowMap.ImageMemory, 0);

        context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkImage>(*s_Data->ShadowMap.Image)),
                                   vk::ObjectType::eImage,
                                   "Shadow Map Image");
        context.SetObjectDebugName(
            reinterpret_cast<uint64_t>(static_cast<VkDeviceMemory>(*s_Data->ShadowMap.ImageMemory)),
            vk::ObjectType::eDeviceMemory,
            "Shadow Map Image Memory");

        const vk::ImageViewCreateInfo readViewInfo{ .image = *s_Data->ShadowMap.Image,
                                                    .viewType = vk::ImageViewType::e2DArray,
                                                    .format = s_Data->ShadowMap.Format,
                                                    .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eDepth,
                                                                          .baseMipLevel = 0,
                                                                          .levelCount = shadowMapMipLevels,
                                                                          .baseArrayLayer = 0,
                                                                          .layerCount = ShadowMap::CascadeCount } };

        s_Data->ShadowMap.ReadImageView = vk::raii::ImageView(device, readViewInfo);

        context.SetObjectDebugName(s_Data->ShadowMap.ReadImageView, "Shadow Map Image View");

        for (uint32_t i = 0; i < ShadowMap::CascadeCount; ++i) {
            const vk::ImageViewCreateInfo attachmentViewInfo{ .image = *s_Data->ShadowMap.Image,
                                                              .viewType = vk::ImageViewType::e2D,
                                                              .format = s_Data->ShadowMap.Format,
                                                              .subresourceRange = { .aspectMask =
                                                                                        vk::ImageAspectFlagBits::eDepth,
                                                                                    .baseMipLevel = 0,
                                                                                    .levelCount = shadowMapMipLevels,
                                                                                    .baseArrayLayer = i,
                                                                                    .layerCount = 1 } };
            s_Data->ShadowMap.WriteImageViews[i] = vk::raii::ImageView(device, attachmentViewInfo);
            context.SetObjectDebugName(s_Data->ShadowMap.WriteImageViews[i],
                                       "Shadow Map Write Image View [" + std::to_string(i) + "]");
        }

        // We do this here, because the descriptors will use the shadow map image view,
        // but has to happen before we create the pipeline
        SetupDescriptors();

        // Create shadow map image layout transition
        /*context.TransitionImageLayout(shadowMapImage,
                                        vk::ImageLayout::eUndefined,
                                        vk::ImageLayout::eDepthStencilAttachmentOptimal,
                                        shadowMapMipLevels);*/

        constexpr vk::PushConstantRange pushConstantRange{ .stageFlags = vk::ShaderStageFlagBits::eVertex,
                                                           .offset = 0,
                                                           .size = sizeof(uint32_t) };

        vk::PipelineLayoutCreateInfo pipelineLayoutInfo{ .setLayoutCount = 1,
                                                         .pSetLayouts = &*s_Data->DescriptorSetLayouts.scene,
                                                         .pushConstantRangeCount = 1,
                                                         .pPushConstantRanges = &pushConstantRange };

        s_Data->ShadowMap.PipelineLayout = vk::raii::PipelineLayout{ device, pipelineLayoutInfo };
        context.SetObjectDebugName(
            reinterpret_cast<uint64_t>(static_cast<VkPipelineLayout>(*s_Data->ShadowMap.PipelineLayout)),
            vk::ObjectType::ePipelineLayout,
            "Shadow Map Pipeline Layout");

        // Create shader for shadow mapping
        // Ref<Shader> shadowMapShader = CreateRef<Shader>("shadowmap", "ShadowMap");
        Ref<Shader> shadowMapShader = CreateRef<Shader>("shadowmap_csm", "ShadowMapCSM");

        /*constexpr vk::VertexInputBindingDescription bindingDescription = { 0, sizeof(glm::vec3),
        vk::VertexInputRate::eVertex }; constexpr std::array attributeDescriptions = {
            vk::VertexInputAttributeDescription{.location = 0, .binding = 0, .format = vk::Format::eR32G32B32Sfloat,
        .offset = 0 }
        };
        vk::PipelineVertexInputStateCreateInfo vertexInputInfo{
            .vertexBindingDescriptionCount = 1,
            .pVertexBindingDescriptions = &bindingDescription,
            .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size()),
            .pVertexAttributeDescriptions = attributeDescriptions.data(),
        };*/

        std::vector shadowMapDynamicState = {
            vk::DynamicState::eViewport,
            vk::DynamicState::eScissor,
            vk::DynamicState::eDepthBias,
        };

        const auto bindingDesc = Vertex::GetBindingDescription();
        const auto attributeDescs = Vertex::GetAttributeDescriptions();

        GraphicsPipelineSpecification shadowPipelineSpec{};
        shadowPipelineSpec.Name = "Shadow Map Pipeline";
        shadowPipelineSpec.Shader = shadowMapShader;
        shadowPipelineSpec.PipelineLayout = *s_Data->ShadowMap.PipelineLayout;
        shadowPipelineSpec.BindingDescription = bindingDesc;
        shadowPipelineSpec.InputAttributeDescriptions = { attributeDescs.begin(), attributeDescs.end() };
        shadowPipelineSpec.SampleCount = vk::SampleCountFlagBits::e1;
        shadowPipelineSpec.CullMode = CullMode::Front;
        shadowPipelineSpec.EnableDepthClamp = true;
        shadowPipelineSpec.EnableDepthBias = true;
        shadowPipelineSpec.EnableDepthTest = true;
        shadowPipelineSpec.EnableDepthWrite = true;
        shadowPipelineSpec.DepthTestFunc = DepthTestFunc::LessOrEqual;
        shadowPipelineSpec.DepthAttachmentFormat = s_Data->ShadowMap.Format;
        shadowPipelineSpec.DynamicStates = shadowMapDynamicState;

        s_Data->ShadowMap.Pipeline = CreateRef<GraphicsPipeline>(shadowPipelineSpec);
    }

    std::vector commonDynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };

    // Create the opaque pipeline resources
    {
        s_Data->ColorImage.Format = context.FindSupportedFormat(
            { vk::Format::eR16G16B16A16Sfloat, vk::Format::eR32G32B32A32Sfloat },
            vk::ImageTiling::eOptimal,
            vk::FormatFeatureFlagBits::eColorAttachment | vk::FormatFeatureFlagBits::eColorAttachmentBlend |
                vk::FormatFeatureFlagBits::eSampledImage);
        s_Data->PickingImage.Format = context.FindSupportedFormat({ vk::Format::eR32Uint },
                                                                  vk::ImageTiling::eOptimal,
                                                                  vk::FormatFeatureFlagBits::eColorAttachment |
                                                                      vk::FormatFeatureFlagBits::eTransferSrc);

        s_Data->ResolveImage.Format = context.FindSupportedFormat(
            { vk::Format::eR16G16B16A16Sfloat, vk::Format::eR32G32B32A32Sfloat },
            vk::ImageTiling::eOptimal,
            vk::FormatFeatureFlagBits::eColorAttachment | vk::FormatFeatureFlagBits::eSampledImage);

        constexpr uint32_t initialImageWidth = 1920;
        constexpr uint32_t initialImageHeight = 1080;
        constexpr uint32_t mipLevels = 1;

        CreateImage(device,
                    initialImageWidth,
                    initialImageHeight,
                    mipLevels,
                    vk::SampleCountFlagBits::e1,
                    s_Data->ResolveImage.Format,
                    vk::ImageTiling::eOptimal,
                    vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
                    vk::MemoryPropertyFlagBits::eDeviceLocal,
                    s_Data->ResolveImage.Image,
                    s_Data->ResolveImage.ImageMemory);

        context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkImage>(*s_Data->ResolveImage.Image)),
                                   vk::ObjectType::eImage,
                                   "Resolve Image");

        context.SetObjectDebugName(
            reinterpret_cast<uint64_t>(static_cast<VkDeviceMemory>(*s_Data->ResolveImage.ImageMemory)),
            vk::ObjectType::eDeviceMemory,
            "Resolve Image Memory");

        s_Data->ResolveImage.ImageView = CreateImageView(device,
                                                         s_Data->ResolveImage.Image,
                                                         s_Data->ResolveImage.Format,
                                                         vk::ImageAspectFlagBits::eColor,
                                                         mipLevels);
        context.SetObjectDebugName(
            reinterpret_cast<uint64_t>(static_cast<VkImageView>(*s_Data->ResolveImage.ImageView)),
            vk::ObjectType::eImageView,
            "Resolve Image View");

        CreateImage(device,
                    initialImageWidth,
                    initialImageHeight,
                    mipLevels,
                    vk::SampleCountFlagBits::e1,
                    s_Data->ColorImage.Format,
                    vk::ImageTiling::eOptimal,
                    vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
                    vk::MemoryPropertyFlagBits::eDeviceLocal,
                    s_Data->ColorImage.Image,
                    s_Data->ColorImage.ImageMemory);

        context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkImage>(*s_Data->ColorImage.Image)),
                                   vk::ObjectType::eImage,
                                   "Color Attachment Image");

        context.SetObjectDebugName(
            reinterpret_cast<uint64_t>(static_cast<VkDeviceMemory>(*s_Data->ColorImage.ImageMemory)),
            vk::ObjectType::eDeviceMemory,
            "Color Attachment Image Memory");

        s_Data->ColorImage.ImageView = CreateImageView(
            device, s_Data->ColorImage.Image, s_Data->ColorImage.Format, vk::ImageAspectFlagBits::eColor, mipLevels);

        context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkImageView>(*s_Data->ColorImage.ImageView)),
                                   vk::ObjectType::eImageView,
                                   "Color Attachment Image View");

        CreateImage(device,
                    initialImageWidth,
                    initialImageHeight,
                    mipLevels,
                    vk::SampleCountFlagBits::e1,
                    s_Data->PickingImage.Format,
                    vk::ImageTiling::eOptimal,
                    vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc,
                    vk::MemoryPropertyFlagBits::eDeviceLocal,
                    s_Data->PickingImage.Image,
                    s_Data->PickingImage.ImageMemory);

        context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkImage>(*s_Data->PickingImage.Image)),
                                   vk::ObjectType::eImage,
                                   "Picking Attachment Image");

        context.SetObjectDebugName(
            reinterpret_cast<uint64_t>(static_cast<VkDeviceMemory>(*s_Data->PickingImage.ImageMemory)),
            vk::ObjectType::eDeviceMemory,
            "Picking Attachment Image Memory");

        s_Data->PickingImage.ImageView = CreateImageView(device,
                                                         s_Data->PickingImage.Image,
                                                         s_Data->PickingImage.Format,
                                                         vk::ImageAspectFlagBits::eColor,
                                                         mipLevels);

        context.SetObjectDebugName(
            reinterpret_cast<uint64_t>(static_cast<VkImageView>(*s_Data->PickingImage.ImageView)),
            vk::ObjectType::eImageView,
            "Picking Attachment Image View");
        s_Data->PickingImageLayout = vk::ImageLayout::eUndefined;

        s_Data->DepthImage.Format = context.FindSupportedFormat({ vk::Format::eD32Sfloat },
                                                                vk::ImageTiling::eOptimal,
                                                                vk::FormatFeatureFlagBits::eDepthStencilAttachment |
                                                                    vk::FormatFeatureFlagBits::eSampledImage);

        CreateImage(device,
                    initialImageWidth,
                    initialImageHeight,
                    mipLevels,
                    vk::SampleCountFlagBits::e1,
                    s_Data->DepthImage.Format,
                    vk::ImageTiling::eOptimal,
                    vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled,
                    vk::MemoryPropertyFlagBits::eDeviceLocal,
                    s_Data->DepthImage.Image,
                    s_Data->DepthImage.ImageMemory);

        context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkImage>(*s_Data->DepthImage.Image)),
                                   vk::ObjectType::eImage,
                                   "Depth Attachment Image");

        context.SetObjectDebugName(
            reinterpret_cast<uint64_t>(static_cast<VkDeviceMemory>(*s_Data->DepthImage.ImageMemory)),
            vk::ObjectType::eDeviceMemory,
            "Depth Attachment Image Memory");

        s_Data->DepthImage.ImageView = CreateImageView(
            device, s_Data->DepthImage.Image, s_Data->DepthImage.Format, vk::ImageAspectFlagBits::eDepth, mipLevels);

        context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkImageView>(*s_Data->DepthImage.ImageView)),
                                   vk::ObjectType::eImageView,
                                   "Depth Attachment Image View");

        s_Data->NormalImage.Format = context.FindSupportedFormat({ vk::Format::eA2B10G10R10UnormPack32 },
                                                                 vk::ImageTiling::eOptimal,
                                                                 vk::FormatFeatureFlagBits::eColorAttachment |
                                                                     vk::FormatFeatureFlagBits::eSampledImage);

        CreateImage(device,
                    initialImageWidth,
                    initialImageHeight,
                    mipLevels,
                    vk::SampleCountFlagBits::e1,
                    s_Data->NormalImage.Format,
                    vk::ImageTiling::eOptimal,
                    vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
                    vk::MemoryPropertyFlagBits::eDeviceLocal,
                    s_Data->NormalImage.Image,
                    s_Data->NormalImage.ImageMemory);

        context.SetObjectDebugName(s_Data->NormalImage.Image, "Normal Attachment Image");
        context.SetObjectDebugName(s_Data->NormalImage.ImageMemory, "Normal Attachment Image Memory");

        s_Data->NormalImage.ImageView = CreateImageView(
            device, s_Data->NormalImage.Image, s_Data->NormalImage.Format, vk::ImageAspectFlagBits::eColor, mipLevels);

        context.SetObjectDebugName(s_Data->NormalImage.ImageView, "Normal Attachment Image View");

        // This is called here, since the normal and depth image has to be valid for the descriptor writes
        SetupGTAODescriptors();

        const std::array<vk::DescriptorSetLayout, 2> setLayouts = {
            s_Data->DescriptorSetLayouts.scene, s_Data->TextureManager.GetGlobalDescriptorSetLayout()
        };

        vk::PipelineLayoutCreateInfo pipelineLayoutInfo{ .setLayoutCount = static_cast<uint32_t>(setLayouts.size()),
                                                         .pSetLayouts = setLayouts.data(),
                                                         .pushConstantRangeCount = 0,
                                                         .pPushConstantRanges = nullptr };

        s_Data->PBRPipelineLayout = vk::raii::PipelineLayout{ device, pipelineLayoutInfo };

        context.SetObjectDebugName(
            reinterpret_cast<uint64_t>(static_cast<VkPipelineLayout>(*s_Data->PBRPipelineLayout)),
            vk::ObjectType::ePipelineLayout,
            "PBR Pipeline Layout");

        const auto bindingDesc = Vertex::GetBindingDescription();
        const auto attributeDescs = Vertex::GetAttributeDescriptions();

        Ref<Shader> depthPrepassShader = CreateRef<Shader>("depthprepass", "Depth Pre-Pass");

        GraphicsPipelineSpecification depthPrepassPipelineSpec{};
        depthPrepassPipelineSpec.Name = "Depth Pre-Pass Pipeline";
        depthPrepassPipelineSpec.Shader = depthPrepassShader;
        depthPrepassPipelineSpec.PipelineLayout = *s_Data->PBRPipelineLayout;
        depthPrepassPipelineSpec.BindingDescription = bindingDesc;
        depthPrepassPipelineSpec.InputAttributeDescriptions = { attributeDescs.begin(), attributeDescs.end() };
        depthPrepassPipelineSpec.SampleCount = vk::SampleCountFlagBits::e1;
        depthPrepassPipelineSpec.CullMode = CullMode::Back;
        depthPrepassPipelineSpec.EnableDepthClamp = false;
        depthPrepassPipelineSpec.EnableDepthBias = false;
        depthPrepassPipelineSpec.EnableDepthTest = true;
        depthPrepassPipelineSpec.EnableDepthWrite = true;
        depthPrepassPipelineSpec.DepthTestFunc = DepthTestFunc::LessOrEqual;
        depthPrepassPipelineSpec.BlendModes = { BlendMode::None };
        depthPrepassPipelineSpec.ColorAttachmentFormats = { s_Data->NormalImage.Format };
        depthPrepassPipelineSpec.DepthAttachmentFormat = s_Data->DepthImage.Format;
        depthPrepassPipelineSpec.DynamicStates = commonDynamicStates;

        s_Data->DepthPrePassPipeline = CreateRef<GraphicsPipeline>(depthPrepassPipelineSpec);

        Ref<Shader> pbrShader = CreateRef<Shader>("pbrtextured", "PBR");

        uint32_t enablePCF = 0;
        vk::SpecializationMapEntry specializationMapEntry{ .constantID = 0, .offset = 0, .size = sizeof(uint32_t) };
        vk::SpecializationInfo specializationInfo{ .mapEntryCount = 1,
                                                   .pMapEntries = &specializationMapEntry,
                                                   .dataSize = sizeof(uint32_t),
                                                   .pData = &enablePCF };

        GraphicsPipelineSpecification opaquePipelineSpec{};
        opaquePipelineSpec.Name = "PBR Opaque Pipeline";
        opaquePipelineSpec.Shader = pbrShader;
        opaquePipelineSpec.PipelineLayout = *s_Data->PBRPipelineLayout;
        opaquePipelineSpec.BindingDescription = bindingDesc;
        opaquePipelineSpec.InputAttributeDescriptions = { attributeDescs.begin(), attributeDescs.end() };
        opaquePipelineSpec.SampleCount = vk::SampleCountFlagBits::e1;
        opaquePipelineSpec.CullMode = CullMode::Back;
        opaquePipelineSpec.EnableDepthClamp = false;
        opaquePipelineSpec.EnableDepthBias = false;
        opaquePipelineSpec.EnableDepthTest = true;
        opaquePipelineSpec.EnableDepthWrite = true;
        opaquePipelineSpec.DepthTestFunc = DepthTestFunc::Equal;
        opaquePipelineSpec.BlendModes = { BlendMode::None, BlendMode::None };
        opaquePipelineSpec.ColorAttachmentFormats = { s_Data->ColorImage.Format, s_Data->PickingImage.Format };
        opaquePipelineSpec.DepthAttachmentFormat = s_Data->DepthImage.Format;
        opaquePipelineSpec.DynamicStates = commonDynamicStates;
        opaquePipelineSpec.SpecializationMapEntries = { { vk::ShaderStageFlagBits::eFragment, specializationInfo } };

        s_Data->PBROpaquePipeline = CreateRef<GraphicsPipeline>(opaquePipelineSpec);

        enablePCF = 1;
        opaquePipelineSpec.Name = "PBR Opaque Pipeline with PCF Shadows";
        s_Data->PBROpaquePipelinePCF = CreateRef<GraphicsPipeline>(opaquePipelineSpec);

        Ref<Shader> pbrRayQueryShadowsShader = CreateRef<Shader>("pbr_ray_query_shadows", "PBR Ray Query Shadows");

        uint32_t enableRayQuerySoftShadows = 0;
        vk::SpecializationMapEntry rayQuerySoftShadowsSpecializationMapEntry{ .constantID = 0,
                                                                              .offset = 0,
                                                                              .size = sizeof(uint32_t) };
        vk::SpecializationInfo rayQuerySoftShadowsSpecializationInfo{ .mapEntryCount = 1,
                                                                      .pMapEntries =
                                                                          &rayQuerySoftShadowsSpecializationMapEntry,
                                                                      .dataSize = sizeof(uint32_t),
                                                                      .pData = &enableRayQuerySoftShadows };

        opaquePipelineSpec.Name = "PBR Ray Query Shadows Pipeline";
        opaquePipelineSpec.Shader = pbrRayQueryShadowsShader;
        opaquePipelineSpec.SpecializationMapEntries = { { vk::ShaderStageFlagBits::eFragment,
                                                          rayQuerySoftShadowsSpecializationInfo } };
        s_Data->PBRRayQueryShadowsPipeline = CreateRef<GraphicsPipeline>(opaquePipelineSpec);

        enableRayQuerySoftShadows = 1;
        opaquePipelineSpec.Name = "PBR Ray Query Soft Shadows Pipeline";
        s_Data->PBRRayQuerySoftShadowsPipeline = CreateRef<GraphicsPipeline>(opaquePipelineSpec);

        Ref<Shader> normalDebugShader = CreateRef<Shader>("normaldebug", "NormalDebug");

        opaquePipelineSpec.Name = "Normal Debug Pipeline";
        opaquePipelineSpec.Shader = normalDebugShader;
        opaquePipelineSpec.SpecializationMapEntries = {};
        s_Data->NormalDebugPipeline = CreateRef<GraphicsPipeline>(opaquePipelineSpec);

        Ref<Shader> colliderLinesShader = CreateRef<Shader>("collider_lines", "Collider Lines");
        const auto lineBindingDesc = LineVertex::GetBindingDescription();
        const auto lineAttributeDescs = LineVertex::GetAttributeDescriptions();

        GraphicsPipelineSpecification colliderPipelineSpec{};
        colliderPipelineSpec.Name = "Collider Lines Pipeline";
        colliderPipelineSpec.Shader = colliderLinesShader;
        colliderPipelineSpec.PipelineLayout = *s_Data->PBRPipelineLayout;
        colliderPipelineSpec.BindingDescription = lineBindingDesc;
        colliderPipelineSpec.InputAttributeDescriptions = { lineAttributeDescs.begin(), lineAttributeDescs.end() };
        colliderPipelineSpec.SampleCount = vk::SampleCountFlagBits::e1;
        colliderPipelineSpec.CullMode = CullMode::None;
        colliderPipelineSpec.EnableDepthClamp = false;
        colliderPipelineSpec.EnableDepthBias = false;
        colliderPipelineSpec.EnableDepthTest = true;
        colliderPipelineSpec.EnableDepthWrite = false;
        colliderPipelineSpec.DepthTestFunc = DepthTestFunc::LessOrEqual;
        colliderPipelineSpec.Topology = PrimitiveTopology::LineList;
        colliderPipelineSpec.BlendModes = { BlendMode::None };
        colliderPipelineSpec.ColorAttachmentFormats = { s_Data->ColorImage.Format };
        colliderPipelineSpec.DepthAttachmentFormat = s_Data->DepthImage.Format;
        colliderPipelineSpec.DynamicStates = commonDynamicStates;
        s_Data->ColliderLinesPipeline = CreateRef<GraphicsPipeline>(colliderPipelineSpec);

        Ref<Shader> skyboxShader = CreateRef<Shader>("skybox", "Skybox");

        GraphicsPipelineSpecification skyboxPipelineSpec{};
        skyboxPipelineSpec.Name = "Skybox Pipeline";
        skyboxPipelineSpec.Shader = skyboxShader;
        skyboxPipelineSpec.PipelineLayout = *s_Data->PBRPipelineLayout;
        skyboxPipelineSpec.BindingDescription = bindingDesc;
        skyboxPipelineSpec.InputAttributeDescriptions = { attributeDescs.begin(), attributeDescs.end() };
        skyboxPipelineSpec.SampleCount = vk::SampleCountFlagBits::e1;
        skyboxPipelineSpec.CullMode = CullMode::Front;
        skyboxPipelineSpec.EnableDepthClamp = false;
        skyboxPipelineSpec.EnableDepthBias = false;
        skyboxPipelineSpec.EnableDepthTest = true;
        skyboxPipelineSpec.EnableDepthWrite = false;
        skyboxPipelineSpec.DepthTestFunc = DepthTestFunc::LessOrEqual;
        skyboxPipelineSpec.BlendModes = { BlendMode::None, BlendMode::None };
        skyboxPipelineSpec.ColorAttachmentFormats = { s_Data->ColorImage.Format, s_Data->PickingImage.Format };
        skyboxPipelineSpec.DepthAttachmentFormat = s_Data->DepthImage.Format;
        skyboxPipelineSpec.DynamicStates = commonDynamicStates;

        s_Data->SkyboxPipeline = CreateRef<GraphicsPipeline>(skyboxPipelineSpec);
    }

    s_Data->ParticleSystem.Initialize(s_Data->ColorImage.Format,
                                      s_Data->DepthImage.Format,
                                      s_Data->DepthImage.ImageView,
                                      s_Data->PersistentDescriptorAllocator);
    s_Data->GrassSystem.Init(s_Data->PersistentDescriptorAllocator);

    // Create transparent pipeline resources
    {
        s_Data->Transparency.AccumulationImage.Format = context.FindSupportedFormat(
            { vk::Format::eR16G16B16A16Sfloat },
            vk::ImageTiling::eOptimal,
            vk::FormatFeatureFlagBits::eColorAttachment | vk::FormatFeatureFlagBits::eColorAttachmentBlend |
                vk::FormatFeatureFlagBits::eSampledImage);

        s_Data->Transparency.RevealageImage.Format = context.FindSupportedFormat(
            { vk::Format::eR16Unorm, vk::Format::eR8Unorm },
            vk::ImageTiling::eOptimal,
            vk::FormatFeatureFlagBits::eColorAttachment | vk::FormatFeatureFlagBits::eColorAttachmentBlend |
                vk::FormatFeatureFlagBits::eSampledImage);

        s_Data->Transparency.DistortionImage.Format = context.FindSupportedFormat(
            { vk::Format::eR16G16Sfloat },
            vk::ImageTiling::eOptimal,
            vk::FormatFeatureFlagBits::eColorAttachment | vk::FormatFeatureFlagBits::eColorAttachmentBlend |
                vk::FormatFeatureFlagBits::eSampledImage);

        constexpr uint32_t initialImageWidth = 1920;
        constexpr uint32_t initialImageHeight = 1080;

        CreateTransparencyResources(initialImageWidth, initialImageHeight);

        const auto bindingDesc = Vertex::GetBindingDescription();
        const auto attributeDescs = Vertex::GetAttributeDescriptions();

        Ref<Shader> wboitShader = CreateRef<Shader>("transparent", "Weighted-Blended OIT");

        GraphicsPipelineSpecification transparentPipelineSpec{};
        transparentPipelineSpec.Name = "PBR Transparent Pipeline";
        transparentPipelineSpec.Shader = wboitShader;
        transparentPipelineSpec.PipelineLayout = *s_Data->PBRPipelineLayout;
        transparentPipelineSpec.BindingDescription = bindingDesc;
        transparentPipelineSpec.InputAttributeDescriptions = { attributeDescs.begin(), attributeDescs.end() };
        transparentPipelineSpec.SampleCount = vk::SampleCountFlagBits::e1;
        transparentPipelineSpec.CullMode = CullMode::None;
        transparentPipelineSpec.EnableDepthClamp = false;
        transparentPipelineSpec.EnableDepthBias = false;
        transparentPipelineSpec.EnableDepthTest = true;
        transparentPipelineSpec.EnableDepthWrite = false;
        transparentPipelineSpec.DepthTestFunc = DepthTestFunc::Less;
        transparentPipelineSpec.BlendModes = { BlendMode::Additive, BlendMode::Multiplicative, BlendMode::Additive };
        transparentPipelineSpec.ColorAttachmentFormats = { s_Data->Transparency.AccumulationImage.Format,
                                                           s_Data->Transparency.RevealageImage.Format,
                                                           s_Data->Transparency.DistortionImage.Format };
        transparentPipelineSpec.DepthAttachmentFormat = s_Data->DepthImage.Format;
        transparentPipelineSpec.DynamicStates = commonDynamicStates;

        s_Data->Transparency.MainPipeline = CreateRef<GraphicsPipeline>(transparentPipelineSpec);
    }

    // Has to be created before setting up transparency descriptors, since currently the resolve pass does the
    // tonemapping
    CreateBloomResources(static_cast<uint32_t>(s_Data->OutputSize.x), static_cast<uint32_t>(s_Data->OutputSize.y));

    SetupTransparencyDescriptors();

    SetupBloomDescriptors();

    // Create composite pipeline resources
    {
        const std::array setLayouts = { *s_Data->DescriptorSetLayouts.composite };

        vk::PipelineLayoutCreateInfo compositePipelineLayoutInfo{ .setLayoutCount =
                                                                      static_cast<uint32_t>(setLayouts.size()),
                                                                  .pSetLayouts = setLayouts.data(),
                                                                  .pushConstantRangeCount = 0,
                                                                  .pPushConstantRanges = nullptr };

        s_Data->TransparencyResolvePipelineLayout = vk::raii::PipelineLayout{ device, compositePipelineLayoutInfo };
        context.SetObjectDebugName(s_Data->TransparencyResolvePipelineLayout, "Transparency Resolve Pipeline Layout");

        Ref<Shader> compositeShader = CreateRef<Shader>("transparency_resolve", "Transparency Resolve");

        GraphicsPipelineSpecification compositePipelineSpec{};
        compositePipelineSpec.Name = "Transparency Resolve Pipeline";
        compositePipelineSpec.Shader = compositeShader;
        compositePipelineSpec.PipelineLayout = *s_Data->TransparencyResolvePipelineLayout;
        compositePipelineSpec.BindingDescription = {};
        compositePipelineSpec.InputAttributeDescriptions = {};
        compositePipelineSpec.SampleCount = vk::SampleCountFlagBits::e1;
        compositePipelineSpec.CullMode = CullMode::None;
        compositePipelineSpec.EnableDepthClamp = false;
        compositePipelineSpec.EnableDepthBias = false;
        compositePipelineSpec.EnableDepthTest = false;
        compositePipelineSpec.EnableDepthWrite = false;
        compositePipelineSpec.BlendModes = { BlendMode::None };
        compositePipelineSpec.ColorAttachmentFormats = { s_Data->ResolveImage.Format };
        compositePipelineSpec.DepthAttachmentFormat = std::nullopt;
        compositePipelineSpec.DynamicStates = commonDynamicStates;

        s_Data->TransparencyResolvePipeline = CreateRef<GraphicsPipeline>(compositePipelineSpec);
    }

    // Create tonemapping resolve pipeline resources
    {
        std::vector<vk::DescriptorSetLayoutBinding> bindings = {
            vk::DescriptorSetLayoutBinding{ // Scene color
                                            .binding = 0,
                                            .descriptorType = vk::DescriptorType::eSampledImage,
                                            .descriptorCount = 1,
                                            .stageFlags = vk::ShaderStageFlagBits::eFragment,
                                            .pImmutableSamplers = nullptr },
            vk::DescriptorSetLayoutBinding{ // Linear sampler
                                            .binding = 1,
                                            .descriptorType = vk::DescriptorType::eSampler,
                                            .descriptorCount = 1,
                                            .stageFlags = vk::ShaderStageFlagBits::eFragment,
                                            .pImmutableSamplers = nullptr },
            vk::DescriptorSetLayoutBinding{ // Bloom texture
                                            .binding = 2,
                                            .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                            .descriptorCount = 1,
                                            .stageFlags = vk::ShaderStageFlagBits::eFragment,
                                            .pImmutableSamplers = nullptr },
            vk::DescriptorSetLayoutBinding{ // Global lighting buffer
                                            .binding = 3,
                                            .descriptorType = vk::DescriptorType::eUniformBuffer,
                                            .descriptorCount = 1,
                                            .stageFlags = vk::ShaderStageFlagBits::eFragment,
                                            .pImmutableSamplers = nullptr },
        };

        const vk::DescriptorSetLayoutCreateInfo layoutInfo{ .bindingCount = static_cast<uint32_t>(bindings.size()),
                                                            .pBindings = bindings.data() };

        s_Data->DescriptorSetLayouts.tonemappingResolve = vk::raii::DescriptorSetLayout{ device, layoutInfo };
        context.SetObjectDebugName(s_Data->DescriptorSetLayouts.tonemappingResolve,
                                   "Tonemapping Resolve Descriptor Set Layout");

        const std::array setLayouts = { *s_Data->DescriptorSetLayouts.tonemappingResolve };

        constexpr vk::PushConstantRange tonemappingPushConstantRange{ .stageFlags = vk::ShaderStageFlagBits::eFragment,
                                                                      .offset = 0,
                                                                      .size = sizeof(TonemappingResolvePushConstants) };

        vk::PipelineLayoutCreateInfo tonemappingPipelineLayoutInfo{ .setLayoutCount =
                                                                        static_cast<uint32_t>(setLayouts.size()),
                                                                    .pSetLayouts = setLayouts.data(),
                                                                    .pushConstantRangeCount = 1,
                                                                    .pPushConstantRanges =
                                                                        &tonemappingPushConstantRange };

        s_Data->TonemappingResolvePipelineLayout = vk::raii::PipelineLayout{ device, tonemappingPipelineLayoutInfo };
        context.SetObjectDebugName(s_Data->TonemappingResolvePipelineLayout, "Tonemapping Resolve Pipeline Layout");

        s_Data->TonemappedImage.Format = s_Data->ResolveImage.Format;

        constexpr uint32_t initialImageWidth = 1920;
        constexpr uint32_t initialImageHeight = 1080;

        CreateTonemappedImage(initialImageWidth, initialImageHeight);
        SetupTonemappingResolveDescriptors();

        Ref<Shader> tonemappingShader = CreateRef<Shader>("tonemapping_resolve", "Tonemapping Resolve");

        GraphicsPipelineSpecification tonemappingPipelineSpec{};
        tonemappingPipelineSpec.Name = "Tonemapping Resolve Pipeline";
        tonemappingPipelineSpec.Shader = tonemappingShader;
        tonemappingPipelineSpec.PipelineLayout = *s_Data->TonemappingResolvePipelineLayout;
        tonemappingPipelineSpec.BindingDescription = {};
        tonemappingPipelineSpec.InputAttributeDescriptions = {};
        tonemappingPipelineSpec.SampleCount = vk::SampleCountFlagBits::e1;
        tonemappingPipelineSpec.CullMode = CullMode::None;
        tonemappingPipelineSpec.EnableDepthClamp = false;
        tonemappingPipelineSpec.EnableDepthBias = false;
        tonemappingPipelineSpec.EnableDepthTest = false;
        tonemappingPipelineSpec.EnableDepthWrite = false;
        tonemappingPipelineSpec.BlendModes = { BlendMode::None };
        tonemappingPipelineSpec.ColorAttachmentFormats = { s_Data->TonemappedImage.Format };
        tonemappingPipelineSpec.DepthAttachmentFormat = std::nullopt;
        tonemappingPipelineSpec.DynamicStates = commonDynamicStates;

        s_Data->TonemappingResolvePipeline = CreateRef<GraphicsPipeline>(tonemappingPipelineSpec);
    }

    // Create fxaa pipeline resources
    {
        constexpr uint32_t initialImageWidth = 1920;
        constexpr uint32_t initialImageHeight = 1080;

        std::vector<vk::DescriptorSetLayoutBinding> bindings = {
            vk::DescriptorSetLayoutBinding{ // Scene color with Luma
                                            .binding = 0,
                                            .descriptorType = vk::DescriptorType::eSampledImage,
                                            .descriptorCount = 1,
                                            .stageFlags = vk::ShaderStageFlagBits::eFragment,
                                            .pImmutableSamplers = nullptr },
            vk::DescriptorSetLayoutBinding{ // Linear sampler
                                            .binding = 1,
                                            .descriptorType = vk::DescriptorType::eSampler,
                                            .descriptorCount = 1,
                                            .stageFlags = vk::ShaderStageFlagBits::eFragment,
                                            .pImmutableSamplers = nullptr },
            vk::DescriptorSetLayoutBinding{ // Bloom texture
                                            .binding = 2,
                                            .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                            .descriptorCount = 1,
                                            .stageFlags = vk::ShaderStageFlagBits::eFragment,
                                            .pImmutableSamplers = nullptr },
            vk::DescriptorSetLayoutBinding{ // Global lighting buffer
                                            .binding = 3,
                                            .descriptorType = vk::DescriptorType::eUniformBuffer,
                                            .descriptorCount = 1,
                                            .stageFlags = vk::ShaderStageFlagBits::eFragment,
                                            .pImmutableSamplers = nullptr },
        };

        const vk::DescriptorSetLayoutCreateInfo layoutInfo{ .bindingCount = static_cast<uint32_t>(bindings.size()),
                                                            .pBindings = bindings.data() };

        s_Data->DescriptorSetLayouts.fxaa = vk::raii::DescriptorSetLayout{ device, layoutInfo };
        context.SetObjectDebugName(s_Data->DescriptorSetLayouts.fxaa, "FXAA Descriptor Set Layout");

        const std::array setLayouts = { *s_Data->DescriptorSetLayouts.fxaa };

        constexpr vk::PushConstantRange fxaaPushConstantRange{ .stageFlags = vk::ShaderStageFlagBits::eFragment,
                                                               .offset = 0,
                                                               .size = sizeof(FXAAPushConstants) };

        vk::PipelineLayoutCreateInfo fxaaPipelineLayoutInfo{ .setLayoutCount = static_cast<uint32_t>(setLayouts.size()),
                                                             .pSetLayouts = setLayouts.data(),
                                                             .pushConstantRangeCount = 1,
                                                             .pPushConstantRanges = &fxaaPushConstantRange };

        s_Data->FXAAPipelineLayout = vk::raii::PipelineLayout{ device, fxaaPipelineLayoutInfo };
        context.SetObjectDebugName(s_Data->FXAAPipelineLayout, "FXAA Pipeline Layout");

        s_Data->CompositeImage.Format = context.FindSupportedFormat(
            { vk::Format::eR16G16B16A16Sfloat, vk::Format::eR32G32B32A32Sfloat },
            vk::ImageTiling::eOptimal,
            vk::FormatFeatureFlagBits::eColorAttachment | vk::FormatFeatureFlagBits::eSampledImage);

        // Sanity check
        KBRAssert(s_Data->CompositeImage.Format == s_Data->ResolveImage.Format,
                        "FXAA composite image format does not match resolve image format!");

        CreateFXAAImage(initialImageWidth, initialImageHeight);
        SetupFXAADescriptors();

        Ref<Shader> fxaaShader = CreateRef<Shader>("fxaa", "FXAA");

        GraphicsPipelineSpecification fxaaPipelineSpec{};
        fxaaPipelineSpec.Name = "FXAA Pipeline";
        fxaaPipelineSpec.Shader = fxaaShader;
        fxaaPipelineSpec.PipelineLayout = *s_Data->FXAAPipelineLayout;
        fxaaPipelineSpec.BindingDescription = {};
        fxaaPipelineSpec.InputAttributeDescriptions = {};
        fxaaPipelineSpec.SampleCount = vk::SampleCountFlagBits::e1;
        fxaaPipelineSpec.CullMode = CullMode::None;
        fxaaPipelineSpec.EnableDepthClamp = false;
        fxaaPipelineSpec.EnableDepthBias = false;
        fxaaPipelineSpec.EnableDepthTest = false;
        fxaaPipelineSpec.EnableDepthWrite = false;
        fxaaPipelineSpec.BlendModes = { BlendMode::None };
        fxaaPipelineSpec.ColorAttachmentFormats = { s_Data->CompositeImage.Format };
        fxaaPipelineSpec.DepthAttachmentFormat = std::nullopt;
        fxaaPipelineSpec.DynamicStates = commonDynamicStates;

        s_Data->FXAAPipeline = CreateRef<GraphicsPipeline>(fxaaPipelineSpec);

        Ref<Shader> noopPostProcessShader = CreateRef<Shader>("noop_post_process", "No-op Post Process");

        GraphicsPipelineSpecification noopPostProcessPipelineSpec{};
        noopPostProcessPipelineSpec.Name = "No-op Post Process Pipeline";
        noopPostProcessPipelineSpec.Shader = noopPostProcessShader;
        noopPostProcessPipelineSpec.PipelineLayout = *s_Data->FXAAPipelineLayout;
        noopPostProcessPipelineSpec.BindingDescription = {};
        noopPostProcessPipelineSpec.InputAttributeDescriptions = {};
        noopPostProcessPipelineSpec.SampleCount = vk::SampleCountFlagBits::e1;
        noopPostProcessPipelineSpec.CullMode = CullMode::None;
        noopPostProcessPipelineSpec.EnableDepthClamp = false;
        noopPostProcessPipelineSpec.EnableDepthBias = false;
        noopPostProcessPipelineSpec.EnableDepthTest = false;
        noopPostProcessPipelineSpec.EnableDepthWrite = false;
        noopPostProcessPipelineSpec.BlendModes = { BlendMode::None };
        noopPostProcessPipelineSpec.ColorAttachmentFormats = { s_Data->CompositeImage.Format };
        noopPostProcessPipelineSpec.DepthAttachmentFormat = std::nullopt;
        noopPostProcessPipelineSpec.DynamicStates = commonDynamicStates;

        s_Data->NoopPostProcessPipeline = CreateRef<GraphicsPipeline>(noopPostProcessPipelineSpec);
    }

    // Create SMAA resources
    {
        CreateSMAATextures();

        s_Data->SMAAResources.EdgesImage.Format = context.FindSupportedFormat(
            { vk::Format::eR8G8Unorm },
            vk::ImageTiling::eOptimal,
            vk::FormatFeatureFlagBits::eColorAttachment | vk::FormatFeatureFlagBits::eSampledImage);
        s_Data->SMAAResources.BlendImage.Format = context.FindSupportedFormat(
            { vk::Format::eR8G8B8A8Unorm },
            vk::ImageTiling::eOptimal,
            vk::FormatFeatureFlagBits::eColorAttachment | vk::FormatFeatureFlagBits::eSampledImage);

        constexpr uint32_t initialImageWidth = 1920;
        constexpr uint32_t initialImageHeight = 1080;

        CreateSMAADescriptorSetAndPipelineLayouts();
        CreateSMAAImages(initialImageWidth, initialImageHeight);
        SetupSMAADescriptors();

        GraphicsPipelineSpecification smaaEdgeDetectionPipelineSpec{};
        smaaEdgeDetectionPipelineSpec.Name = "SMAA Edge Detection Pipeline";
        smaaEdgeDetectionPipelineSpec.Shader = CreateRef<Shader>("smaa_edge", "SMAA Edge Detection");
        smaaEdgeDetectionPipelineSpec.PipelineLayout = *s_Data->SMAAResources.EdgeDetectionPipelineLayout;
        smaaEdgeDetectionPipelineSpec.BindingDescription = {};
        smaaEdgeDetectionPipelineSpec.InputAttributeDescriptions = {};
        smaaEdgeDetectionPipelineSpec.SampleCount = vk::SampleCountFlagBits::e1;
        smaaEdgeDetectionPipelineSpec.CullMode = CullMode::None;
        smaaEdgeDetectionPipelineSpec.EnableDepthClamp = false;
        smaaEdgeDetectionPipelineSpec.EnableDepthBias = false;
        smaaEdgeDetectionPipelineSpec.EnableDepthTest = false;
        smaaEdgeDetectionPipelineSpec.EnableDepthWrite = false;
        smaaEdgeDetectionPipelineSpec.BlendModes = { BlendMode::None };
        smaaEdgeDetectionPipelineSpec.ColorAttachmentFormats = { s_Data->SMAAResources.EdgesImage.Format };
        smaaEdgeDetectionPipelineSpec.DepthAttachmentFormat = std::nullopt;
        smaaEdgeDetectionPipelineSpec.DynamicStates = commonDynamicStates;

        s_Data->SMAAResources.EdgeDetectionPipeline = CreateRef<GraphicsPipeline>(smaaEdgeDetectionPipelineSpec);

        GraphicsPipelineSpecification smaaBlendWeightPipelineSpec{};
        smaaBlendWeightPipelineSpec.Name = "SMAA Blend Weight Pipeline";
        smaaBlendWeightPipelineSpec.Shader = CreateRef<Shader>("smaa_weights", "SMAA Blend Weights");
        smaaBlendWeightPipelineSpec.PipelineLayout = *s_Data->SMAAResources.BlendWeightPipelineLayout;
        smaaBlendWeightPipelineSpec.BindingDescription = {};
        smaaBlendWeightPipelineSpec.InputAttributeDescriptions = {};
        smaaBlendWeightPipelineSpec.SampleCount = vk::SampleCountFlagBits::e1;
        smaaBlendWeightPipelineSpec.CullMode = CullMode::None;
        smaaBlendWeightPipelineSpec.EnableDepthClamp = false;
        smaaBlendWeightPipelineSpec.EnableDepthBias = false;
        smaaBlendWeightPipelineSpec.EnableDepthTest = false;
        smaaBlendWeightPipelineSpec.EnableDepthWrite = false;
        smaaBlendWeightPipelineSpec.BlendModes = { BlendMode::None };
        smaaBlendWeightPipelineSpec.ColorAttachmentFormats = { s_Data->SMAAResources.BlendImage.Format };
        smaaBlendWeightPipelineSpec.DepthAttachmentFormat = std::nullopt;
        smaaBlendWeightPipelineSpec.DynamicStates = commonDynamicStates;

        s_Data->SMAAResources.BlendWeightPipeline = CreateRef<GraphicsPipeline>(smaaBlendWeightPipelineSpec);

        GraphicsPipelineSpecification smaaNeighbourhoodBlendPipelineSpec{};
        smaaNeighbourhoodBlendPipelineSpec.Name = "SMAA Neighbourhood Blend Pipeline";
        smaaNeighbourhoodBlendPipelineSpec.Shader =
            CreateRef<Shader>("smaa_neighbourhood_blend", "SMAA Neighbourhood Blend");
        smaaNeighbourhoodBlendPipelineSpec.PipelineLayout = *s_Data->SMAAResources.NeighborhoodBlendingPipelineLayout;
        smaaNeighbourhoodBlendPipelineSpec.BindingDescription = {};
        smaaNeighbourhoodBlendPipelineSpec.InputAttributeDescriptions = {};
        smaaNeighbourhoodBlendPipelineSpec.SampleCount = vk::SampleCountFlagBits::e1;
        smaaNeighbourhoodBlendPipelineSpec.CullMode = CullMode::None;
        smaaNeighbourhoodBlendPipelineSpec.EnableDepthClamp = false;
        smaaNeighbourhoodBlendPipelineSpec.EnableDepthBias = false;
        smaaNeighbourhoodBlendPipelineSpec.EnableDepthTest = false;
        smaaNeighbourhoodBlendPipelineSpec.EnableDepthWrite = false;
        smaaNeighbourhoodBlendPipelineSpec.BlendModes = { BlendMode::None };
        smaaNeighbourhoodBlendPipelineSpec.ColorAttachmentFormats = { s_Data->CompositeImage.Format };
        smaaNeighbourhoodBlendPipelineSpec.DepthAttachmentFormat = std::nullopt;
        smaaNeighbourhoodBlendPipelineSpec.DynamicStates = commonDynamicStates;

        s_Data->SMAAResources.NeighborhoodBlendingPipeline =
            CreateRef<GraphicsPipeline>(smaaNeighbourhoodBlendPipelineSpec);
    }

    // Transition sampled post-process inputs to shader read layout
    {
        const vk::ImageMemoryBarrier2 resolveImageBarrier = {
            .srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe,
            .srcAccessMask = {},
            .dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
            .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = s_Data->ResolveImage.Image,
            .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eColor,
                                  .baseMipLevel = 0,
                                  .levelCount = 1,
                                  .baseArrayLayer = 0,
                                  .layerCount = 1 }
        };
        const vk::ImageMemoryBarrier2 compositeImageBarrier = {
            .srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe,
            .srcAccessMask = {},
            .dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
            .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = s_Data->CompositeImage.Image,
            .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eColor,
                                  .baseMipLevel = 0,
                                  .levelCount = 1,
                                  .baseArrayLayer = 0,
                                  .layerCount = 1 }
        };
        const vk::ImageMemoryBarrier2 colorOutputImageBarrier = {
            .srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe,
            .srcAccessMask = {},
            .dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
            .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = s_Data->ColorImage.Image,
            .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eColor,
                                  .baseMipLevel = 0,
                                  .levelCount = 1,
                                  .baseArrayLayer = 0,
                                  .layerCount = 1 }
        };
        const vk::ImageMemoryBarrier2 bloomImageBarrier = {
            .srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe,
            .srcAccessMask = {},
            .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eFragmentShader,
            .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = s_Data->Bloom.Image,
            .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eColor,
                                  .baseMipLevel = 0,
                                  .levelCount = s_Data->Bloom.MipLevels,
                                  .baseArrayLayer = 0,
                                  .layerCount = 1 }
        };
        const std::array barriers = {
            resolveImageBarrier, compositeImageBarrier, colorOutputImageBarrier, bloomImageBarrier
        };
        const vk::DependencyInfo dependencyInfo = { .dependencyFlags = {},
                                                    .imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size()),
                                                    .pImageMemoryBarriers = barriers.data() };

        const auto cmd = context.BeginSingleTimeCommands();
        context.SetObjectDebugName(
            reinterpret_cast<uint64_t>(static_cast<VkCommandBuffer>(*cmd)),
            vk::ObjectType::eCommandBuffer,
            "EditorLayer Single Time Command Buffer for Composite/Color Image Layout Transition");
        cmd.pipelineBarrier2(dependencyInfo);
        context.EndSingleTimeCommands(cmd);
    }

    // Create descriptor set for the output image for ImGui rendering
    {
        s_Data->ColorOutputDescriptorSet =
            VulkanContext::GenerateImGuiDescriptorSet(s_Data->LinearSampler, s_Data->CompositeImage.ImageView);

        context.SetObjectDebugName(
            reinterpret_cast<uint64_t>(static_cast<VkDescriptorSet>(s_Data->ColorOutputDescriptorSet)),
            vk::ObjectType::eDescriptorSet,
            "Color Output Descriptor Set for ImGui");

        for (uint32_t i = 0; i < ShadowMap::CascadeCount; ++i) {
            s_Data->ShadowMapDescriptorSet[i] =
                VulkanContext::GenerateImGuiDescriptorSet(s_Data->ColorSampler, s_Data->ShadowMap.WriteImageViews[i]);
            context.SetObjectDebugName(
                reinterpret_cast<uint64_t>(static_cast<VkDescriptorSet>(s_Data->ShadowMapDescriptorSet[i])),
                vk::ObjectType::eDescriptorSet,
                "Shadow Map Descriptor Set for ImGui");
        }
    }

    for (uint32_t i = 0; i < Renderer::MousePickingReadbackFrameLag; ++i) {
        auto& slot = s_Data->MousePickingReadback.Slots[i];
        CreateBuffer(device,
                     sizeof(uint32_t),
                     vk::BufferUsageFlagBits::eTransferDst,
                     vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
                     slot.Buffer,
                     slot.Memory);

        slot.MappedData = slot.Memory.mapMemory(0, sizeof(uint32_t));
    }

    // m_OutputSize = m_ViewportSize;
}

void Renderer::ResizeResources(const uint32_t width, const uint32_t height)
{
    KBRAssert(s_Data != nullptr, "Renderer not initialized!");

    // Resize the color and depth image, the shadowmap image can keep its size
    auto& context = VulkanContext::Get();
    const auto& device = context.GetDevice();

    constexpr uint32_t mipLevels = 1;

    device.waitIdle();

    // Destroy old resources
    VulkanContext::DestroyImGuiDescriptorSet(s_Data->ColorOutputDescriptorSet);

    s_Data->ColorImage.ImageView.clear();
    s_Data->ColorImage.Image.clear();
    s_Data->ColorImage.ImageMemory.clear();
    s_Data->PickingImage.ImageView.clear();
    s_Data->PickingImage.Image.clear();
    s_Data->PickingImage.ImageMemory.clear();
    s_Data->DepthImage.ImageView.clear();
    s_Data->DepthImage.Image.clear();
    s_Data->DepthImage.ImageMemory.clear();
    s_Data->Transparency.AccumulationImage.ImageView.clear();
    s_Data->Transparency.AccumulationImage.Image.clear();
    s_Data->Transparency.AccumulationImage.ImageMemory.clear();
    s_Data->Transparency.RevealageImage.ImageView.clear();
    s_Data->Transparency.RevealageImage.Image.clear();
    s_Data->Transparency.RevealageImage.ImageMemory.clear();
    s_Data->ResolveImage.ImageView.clear();
    s_Data->ResolveImage.Image.clear();
    s_Data->ResolveImage.ImageMemory.clear();
    s_Data->TonemappedImage.ImageView.clear();
    s_Data->TonemappedImage.Image.clear();
    s_Data->TonemappedImage.ImageMemory.clear();
    s_Data->CompositeImage.ImageView.clear();
    s_Data->CompositeImage.Image.clear();
    s_Data->CompositeImage.ImageMemory.clear();
    s_Data->NormalImage.ImageView.clear();
    s_Data->NormalImage.Image.clear();
    s_Data->NormalImage.ImageMemory.clear();
    s_Data->GTAOImage.ImageView.clear();
    s_Data->GTAOImage.Image.clear();
    s_Data->GTAOImage.ImageMemory.clear();
    s_Data->GTAOScratchImage.ImageView.clear();
    s_Data->GTAOScratchImage.Image.clear();
    s_Data->GTAOScratchImage.ImageMemory.clear();
    s_Data->SMAAResources.EdgesImage.ImageView.clear();
    s_Data->SMAAResources.EdgesImage.Image.clear();
    s_Data->SMAAResources.EdgesImage.ImageMemory.clear();
    s_Data->SMAAResources.BlendImage.ImageView.clear();
    s_Data->SMAAResources.BlendImage.Image.clear();
    s_Data->SMAAResources.BlendImage.ImageMemory.clear();

    // Recreate resources with new size

    CreateTonemappedImage(width, height);

    CreateFXAAImage(width, height);

    CreateBloomImage(width, height);

    CreateImage(device,
                width,
                height,
                mipLevels,
                vk::SampleCountFlagBits::e1,
                s_Data->ResolveImage.Format,
                vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
                vk::MemoryPropertyFlagBits::eDeviceLocal,
                s_Data->ResolveImage.Image,
                s_Data->ResolveImage.ImageMemory);

    context.SetObjectDebugName(s_Data->ResolveImage.Image, "Resolve Image");

    context.SetObjectDebugName(s_Data->ResolveImage.ImageMemory, "Resolve Image Memory");

    s_Data->ResolveImage.ImageView = CreateImageView(
        device, s_Data->ResolveImage.Image, s_Data->ResolveImage.Format, vk::ImageAspectFlagBits::eColor, mipLevels);
    context.SetObjectDebugName(s_Data->ResolveImage.ImageView, "Resolve Image View");

    CreateImage(device,
                width,
                height,
                mipLevels,
                vk::SampleCountFlagBits::e1,
                s_Data->ColorImage.Format,
                vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
                vk::MemoryPropertyFlagBits::eDeviceLocal,
                s_Data->ColorImage.Image,
                s_Data->ColorImage.ImageMemory);

    context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkImage>(*s_Data->ColorImage.Image)),
                               vk::ObjectType::eImage,
                               "Color Attachment Image");

    context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkDeviceMemory>(*s_Data->ColorImage.ImageMemory)),
                               vk::ObjectType::eDeviceMemory,
                               "Color Attachment Image Memory");

    s_Data->ColorImage.ImageView = CreateImageView(
        device, s_Data->ColorImage.Image, s_Data->ColorImage.Format, vk::ImageAspectFlagBits::eColor, mipLevels);
    context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkImageView>(*s_Data->ColorImage.ImageView)),
                               vk::ObjectType::eImageView,
                               "Color Attachment Image View");

    CreateImage(device,
                width,
                height,
                mipLevels,
                vk::SampleCountFlagBits::e1,
                s_Data->PickingImage.Format,
                vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc,
                vk::MemoryPropertyFlagBits::eDeviceLocal,
                s_Data->PickingImage.Image,
                s_Data->PickingImage.ImageMemory);

    context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkImage>(*s_Data->PickingImage.Image)),
                               vk::ObjectType::eImage,
                               "Picking Attachment Image");

    context.SetObjectDebugName(
        reinterpret_cast<uint64_t>(static_cast<VkDeviceMemory>(*s_Data->PickingImage.ImageMemory)),
        vk::ObjectType::eDeviceMemory,
        "Picking Attachment Image Memory");

    s_Data->PickingImage.ImageView = CreateImageView(
        device, s_Data->PickingImage.Image, s_Data->PickingImage.Format, vk::ImageAspectFlagBits::eColor, mipLevels);
    context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkImageView>(*s_Data->PickingImage.ImageView)),
                               vk::ObjectType::eImageView,
                               "Picking Attachment Image View");

    CreateImage(device,
                width,
                height,
                mipLevels,
                vk::SampleCountFlagBits::e1,
                s_Data->DepthImage.Format,
                vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled,
                vk::MemoryPropertyFlagBits::eDeviceLocal,
                s_Data->DepthImage.Image,
                s_Data->DepthImage.ImageMemory);

    context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkImage>(*s_Data->DepthImage.Image)),
                               vk::ObjectType::eImage,
                               "Depth Attachment Image");

    context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkDeviceMemory>(*s_Data->DepthImage.ImageMemory)),
                               vk::ObjectType::eDeviceMemory,
                               "Depth Attachment Image Memory");

    s_Data->DepthImage.ImageView = CreateImageView(
        device, s_Data->DepthImage.Image, s_Data->DepthImage.Format, vk::ImageAspectFlagBits::eDepth, mipLevels);

    context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkImageView>(*s_Data->DepthImage.ImageView)),
                               vk::ObjectType::eImageView,
                               "Depth Attachment Image View");

    CreateImage(device,
                width,
                height,
                mipLevels,
                vk::SampleCountFlagBits::e1,
                s_Data->NormalImage.Format,
                vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
                vk::MemoryPropertyFlagBits::eDeviceLocal,
                s_Data->NormalImage.Image,
                s_Data->NormalImage.ImageMemory);

    context.SetObjectDebugName(s_Data->NormalImage.Image, "Normal Attachment Image");
    context.SetObjectDebugName(s_Data->NormalImage.ImageMemory, "Normal Attachment Image Memory");

    s_Data->NormalImage.ImageView = CreateImageView(
        device, s_Data->NormalImage.Image, s_Data->NormalImage.Format, vk::ImageAspectFlagBits::eColor, mipLevels);

    context.SetObjectDebugName(s_Data->NormalImage.ImageView, "Normal Attachment Image View");

    CreateTransparencyResources(width, height);

    CreateGTAOImage(width, height);

    CreateSMAAImages(width, height);

    SetupTonemappingResolveDescriptors();
    SetupFXAADescriptors();
    SetupBloomDescriptors();
    SetupSMAADescriptors();

    // Update the transparency resolve descriptor sets to point to the new images
    {
        std::array<vk::DescriptorImageInfo, 5> imageInfos;

        imageInfos[0] = { .sampler = s_Data->LinearSampler,
                          .imageView = s_Data->ColorImage.ImageView,
                          .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };
        imageInfos[1] = { .sampler = s_Data->PointSampler,
                          .imageView = s_Data->DepthImage.ImageView,
                          .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };
        imageInfos[2] = { .sampler = s_Data->PointSampler,
                          .imageView = s_Data->Transparency.AccumulationImage.ImageView,
                          .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };
        imageInfos[3] = { .sampler = s_Data->PointSampler,
                          .imageView = s_Data->Transparency.RevealageImage.ImageView,
                          .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };
        imageInfos[4] = { .sampler = s_Data->LinearSampler,
                          .imageView = s_Data->Transparency.DistortionImage.ImageView,
                          .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };

        for (size_t frameIndex = 0; frameIndex < VulkanContext::MaxFramesInFlight; frameIndex++) {
            std::array<vk::WriteDescriptorSet, 5> descriptorWrites;
            for (size_t i = 0; i < 5; i++) {
                descriptorWrites[i] = { .dstSet = s_Data->DescriptorSets[frameIndex].resolve,
                                        .dstBinding = static_cast<uint32_t>(i),
                                        .dstArrayElement = 0,
                                        .descriptorCount = 1,
                                        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                        .pImageInfo = &imageInfos[i] };
            }

            // Overwrite the old bindings with the new ones
            device.updateDescriptorSets(descriptorWrites, nullptr);
        }
    }

    // Update the gtao descriptor sets to point to the new images
    {
        const vk::DescriptorImageInfo depthImageInfo = { .sampler = s_Data->PointSampler,
                                                         .imageView = s_Data->DepthImage.ImageView,
                                                         .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };
        const vk::DescriptorImageInfo normalImageInfo = { .sampler = s_Data->LinearSampler,
                                                          .imageView = s_Data->NormalImage.ImageView,
                                                          .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };
        const vk::DescriptorImageInfo aoImageInfo = { .sampler = s_Data->PointSampler,
                                                      .imageView = s_Data->GTAOImage.ImageView,
                                                      .imageLayout = vk::ImageLayout::eGeneral };

        for (size_t frameIndex = 0; frameIndex < VulkanContext::MaxFramesInFlight; frameIndex++) {
            std::array<vk::WriteDescriptorSet, 3> descriptorWrites;

            descriptorWrites[0] = { .dstSet = s_Data->DescriptorSets[frameIndex].gtao,
                                    .dstBinding = 1,
                                    .dstArrayElement = 0,
                                    .descriptorCount = 1,
                                    .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                    .pImageInfo = &depthImageInfo };
            descriptorWrites[1] = { .dstSet = s_Data->DescriptorSets[frameIndex].gtao,
                                    .dstBinding = 2,
                                    .dstArrayElement = 0,
                                    .descriptorCount = 1,
                                    .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                    .pImageInfo = &normalImageInfo };
            descriptorWrites[2] = { .dstSet = s_Data->DescriptorSets[frameIndex].gtao,
                                    .dstBinding = 3,
                                    .dstArrayElement = 0,
                                    .descriptorCount = 1,
                                    .descriptorType = vk::DescriptorType::eStorageImage,
                                    .pImageInfo = &aoImageInfo };

            // Overwrite the old bindings with the new ones
            device.updateDescriptorSets(descriptorWrites, nullptr);
        }
    }

    // Update the GTAO blur descriptor sets to point to the new images
    {
        const vk::DescriptorImageInfo depthImageInfo = { .sampler = s_Data->PointSampler,
                                                         .imageView = s_Data->DepthImage.ImageView,
                                                         .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };
        const vk::DescriptorImageInfo normalImageInfo = { .sampler = s_Data->LinearSampler,
                                                          .imageView = s_Data->NormalImage.ImageView,
                                                          .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };

        const vk::DescriptorImageInfo mainImageWriteInfo = { .sampler = s_Data->PointSampler,
                                                             .imageView = s_Data->GTAOImage.ImageView,
                                                             .imageLayout = vk::ImageLayout::eGeneral };
        const vk::DescriptorImageInfo mainImageReadInfo = { .sampler = s_Data->PointSampler,
                                                            .imageView = s_Data->GTAOImage.ImageView,
                                                            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };
        const vk::DescriptorImageInfo scratchImageWriteInfo = { .sampler = s_Data->PointSampler,
                                                                .imageView = s_Data->GTAOScratchImage.ImageView,
                                                                .imageLayout = vk::ImageLayout::eGeneral };
        const vk::DescriptorImageInfo scratchImageReadInfo = { .sampler = s_Data->PointSampler,
                                                               .imageView = s_Data->GTAOScratchImage.ImageView,
                                                               .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };

        for (size_t frameIndex = 0; frameIndex < VulkanContext::MaxFramesInFlight; frameIndex++) {
            std::array<vk::WriteDescriptorSet, 8> descriptorWrites;

            descriptorWrites[0] = { .dstSet = s_Data->DescriptorSets[frameIndex].crossBilateralBlurHorizontal,
                                    .dstBinding = 0,
                                    .dstArrayElement = 0,
                                    .descriptorCount = 1,
                                    .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                    .pImageInfo = &mainImageReadInfo };
            descriptorWrites[1] = { .dstSet = s_Data->DescriptorSets[frameIndex].crossBilateralBlurHorizontal,
                                    .dstBinding = 1,
                                    .dstArrayElement = 0,
                                    .descriptorCount = 1,
                                    .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                    .pImageInfo = &depthImageInfo };
            descriptorWrites[2] = { .dstSet = s_Data->DescriptorSets[frameIndex].crossBilateralBlurHorizontal,
                                    .dstBinding = 2,
                                    .dstArrayElement = 0,
                                    .descriptorCount = 1,
                                    .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                    .pImageInfo = &normalImageInfo };
            descriptorWrites[3] = { .dstSet = s_Data->DescriptorSets[frameIndex].crossBilateralBlurHorizontal,
                                    .dstBinding = 3,
                                    .dstArrayElement = 0,
                                    .descriptorCount = 1,
                                    .descriptorType = vk::DescriptorType::eStorageImage,
                                    .pImageInfo = &scratchImageWriteInfo };

            descriptorWrites[4] = { .dstSet = s_Data->DescriptorSets[frameIndex].crossBilateralBlurVertical,
                                    .dstBinding = 0,
                                    .dstArrayElement = 0,
                                    .descriptorCount = 1,
                                    .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                    .pImageInfo = &scratchImageReadInfo };
            descriptorWrites[5] = { .dstSet = s_Data->DescriptorSets[frameIndex].crossBilateralBlurVertical,
                                    .dstBinding = 1,
                                    .dstArrayElement = 0,
                                    .descriptorCount = 1,
                                    .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                    .pImageInfo = &depthImageInfo };
            descriptorWrites[6] = { .dstSet = s_Data->DescriptorSets[frameIndex].crossBilateralBlurVertical,
                                    .dstBinding = 2,
                                    .dstArrayElement = 0,
                                    .descriptorCount = 1,
                                    .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                    .pImageInfo = &normalImageInfo };
            descriptorWrites[7] = { .dstSet = s_Data->DescriptorSets[frameIndex].crossBilateralBlurVertical,
                                    .dstBinding = 3,
                                    .dstArrayElement = 0,
                                    .descriptorCount = 1,
                                    .descriptorType = vk::DescriptorType::eStorageImage,
                                    .pImageInfo = &mainImageWriteInfo };

            device.updateDescriptorSets(descriptorWrites, nullptr);
        }
    }

    // Update the AO texture in the PBR descriptor sets
    {
        const vk::DescriptorImageInfo aoImageInfo = { .sampler = s_Data->LinearSampler,
                                                      .imageView = s_Data->GTAOImage.ImageView,
                                                      .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };

        for (size_t frameIndex = 0; frameIndex < VulkanContext::MaxFramesInFlight; frameIndex++) {
            const std::array descriptorWrites = {
                vk::WriteDescriptorSet{ .dstSet = *s_Data->DescriptorSets[frameIndex].scene,
                                        .dstBinding = 9,
                                        .dstArrayElement = 0,
                                        .descriptorCount = 1,
                                        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                        .pImageInfo = &aoImageInfo },
            };

            device.updateDescriptorSets(descriptorWrites, nullptr);
        }
    }

    // Update the FXAA descriptor set to use the resized tonemapped image
    {
        const vk::DescriptorImageInfo sceneColorWithLumaImageInfo = { .sampler = nullptr,
                                                                      .imageView = s_Data->TonemappedImage.ImageView,
                                                                      .imageLayout =
                                                                          vk::ImageLayout::eShaderReadOnlyOptimal };

        const vk::DescriptorImageInfo bloomImageInfo = { .sampler = s_Data->LinearSampler,
                                                         .imageView = s_Data->Bloom.ImageViews[0],
                                                         .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };

        for (size_t frameIndex = 0; frameIndex < VulkanContext::MaxFramesInFlight; frameIndex++) {
            const std::array descriptorWrites = {
                vk::WriteDescriptorSet{ .dstSet = *s_Data->DescriptorSets[frameIndex].fxaa,
                                        .dstBinding = 0,
                                        .dstArrayElement = 0,
                                        .descriptorCount = 1,
                                        .descriptorType = vk::DescriptorType::eSampledImage,
                                        .pImageInfo = &sceneColorWithLumaImageInfo },
                vk::WriteDescriptorSet{ .dstSet = *s_Data->DescriptorSets[frameIndex].fxaa,
                                        .dstBinding = 2,
                                        .dstArrayElement = 0,
                                        .descriptorCount = 1,
                                        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                        .pImageInfo = &bloomImageInfo }
            };

            device.updateDescriptorSets(descriptorWrites, nullptr);
        }
    }

    // Transition sampled post-process inputs to shader read layout
    {
        const vk::ImageMemoryBarrier2 resolveImageBarrier = {
            .srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe,
            .srcAccessMask = {},
            .dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
            .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = s_Data->ResolveImage.Image,
            .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eColor,
                                  .baseMipLevel = 0,
                                  .levelCount = 1,
                                  .baseArrayLayer = 0,
                                  .layerCount = 1 }
        };
        const vk::ImageMemoryBarrier2 tonemappedImageBarrier = {
            .srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe,
            .srcAccessMask = {},
            .dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
            .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = s_Data->TonemappedImage.Image,
            .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eColor,
                                  .baseMipLevel = 0,
                                  .levelCount = 1,
                                  .baseArrayLayer = 0,
                                  .layerCount = 1 }
        };
        const vk::ImageMemoryBarrier2 compositeImageBarrier = {
            .srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe,
            .srcAccessMask = {},
            .dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
            .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = s_Data->CompositeImage.Image,
            .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eColor,
                                  .baseMipLevel = 0,
                                  .levelCount = 1,
                                  .baseArrayLayer = 0,
                                  .layerCount = 1 }
        };
        const vk::ImageMemoryBarrier2 colorOutputImageBarrier = {
            .srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe,
            .srcAccessMask = {},
            .dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
            .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = s_Data->ColorImage.Image,
            .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eColor,
                                  .baseMipLevel = 0,
                                  .levelCount = 1,
                                  .baseArrayLayer = 0,
                                  .layerCount = 1 }
        };
        const vk::ImageMemoryBarrier2 bloomImageBarrier = {
            .srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe,
            .srcAccessMask = {},
            .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eFragmentShader,
            .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = s_Data->Bloom.Image,
            .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eColor,
                                  .baseMipLevel = 0,
                                  .levelCount = s_Data->Bloom.MipLevels,
                                  .baseArrayLayer = 0,
                                  .layerCount = 1 }
        };
        const std::array barriers = { resolveImageBarrier,
                                      tonemappedImageBarrier,
                                      compositeImageBarrier,
                                      colorOutputImageBarrier,
                                      bloomImageBarrier };
        const vk::DependencyInfo dependencyInfo = { .dependencyFlags = {},
                                                    .imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size()),
                                                    .pImageMemoryBarriers = barriers.data() };
        const auto cmd = context.BeginSingleTimeCommands();
        context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkCommandBuffer>(*cmd)),
                                   vk::ObjectType::eCommandBuffer,
                                   "EditorLayer Single Time Command Buffer for Composite Image Layout Transition");
        cmd.pipelineBarrier2(dependencyInfo);
        context.EndSingleTimeCommands(cmd);
    }

    // Recreate descriptor set for the output image for ImGui rendering
    {
        KBRAssert(s_Data->LinearSampler != nullptr && s_Data->CompositeImage.ImageView != nullptr,
                        "Sampler and image view has to be initialized to create an ImGui descriptor set");

        s_Data->ColorOutputDescriptorSet =
            VulkanContext::GenerateImGuiDescriptorSet(s_Data->LinearSampler, s_Data->CompositeImage.ImageView);
        context.SetObjectDebugName(
            reinterpret_cast<uint64_t>(static_cast<VkDescriptorSet>(s_Data->ColorOutputDescriptorSet)),
            vk::ObjectType::eDescriptorSet,
            "Color Output Descriptor Set for ImGui");
    }

    s_Data->OutputSize = { static_cast<float>(width), static_cast<float>(height) };

    s_Data->ParticleSystem.OnResize(width, height, s_Data->DepthImage.ImageView);
}

void Renderer::RecompileShaders()
{
    VulkanContext::Get().WaitIdle();

    if (const auto& shadowMapPipeline = s_Data->ShadowMap.Pipeline)
        shadowMapPipeline->Recompile();
    if (const auto& depthPrePassPipeline = s_Data->DepthPrePassPipeline)
        depthPrePassPipeline->Recompile();
    if (const auto& pbrOpaquePipeline = s_Data->PBROpaquePipeline)
        pbrOpaquePipeline->Recompile();
    if (const auto& pbrOpaquePipelinePCF = s_Data->PBROpaquePipelinePCF)
        pbrOpaquePipelinePCF->Recompile();
    if (const auto& pbrRayQueryShadowsPipeline = s_Data->PBRRayQueryShadowsPipeline)
        pbrRayQueryShadowsPipeline->Recompile();
    if (const auto& pbrRayQuerySoftShadowsPipeline = s_Data->PBRRayQuerySoftShadowsPipeline)
        pbrRayQuerySoftShadowsPipeline->Recompile();
    if (const auto& normalDebugPipeline = s_Data->NormalDebugPipeline)
        normalDebugPipeline->Recompile();
    if (const auto& colliderLinesPipeline = s_Data->ColliderLinesPipeline)
        colliderLinesPipeline->Recompile();
    if (const auto& transparentPipeline = s_Data->Transparency.MainPipeline)
        transparentPipeline->Recompile();
    if (const auto& skyboxPipeline = s_Data->SkyboxPipeline)
        skyboxPipeline->Recompile();
    if (const auto& compositePipeline = s_Data->TransparencyResolvePipeline)
        compositePipeline->Recompile();
    if (const auto& gtaoPipeline = s_Data->GTAOPipeline)
        gtaoPipeline->Recompile();
    if (const auto& tonemappingPipeline = s_Data->TonemappingResolvePipeline)
        tonemappingPipeline->Recompile();
    if (const auto& fxaaPipeline = s_Data->FXAAPipeline)
        fxaaPipeline->Recompile();
    if (const auto& noopPostProcessPipeline = s_Data->NoopPostProcessPipeline)
        noopPostProcessPipeline->Recompile();
    if (const auto& bloomDownsamplePipeline = s_Data->Bloom.DownsamplePipeline)
        bloomDownsamplePipeline->Recompile();
    if (const auto& bloomUpsamplePipeline = s_Data->Bloom.UpsamplePipeline)
        bloomUpsamplePipeline->Recompile();
    if (const auto& crossBilateralBlurPipeline = s_Data->CrossBilateralBlurPipeline)
        crossBilateralBlurPipeline->Recompile();
    if (const auto& smaaEdgePipeline = s_Data->SMAAResources.EdgeDetectionPipeline)
        smaaEdgePipeline->Recompile();
    if (const auto& smaaBlendPipeline = s_Data->SMAAResources.BlendWeightPipeline)
        smaaBlendPipeline->Recompile();
    if (const auto& smaaNeighborhoodPipeline = s_Data->SMAAResources.NeighborhoodBlendingPipeline)
        smaaNeighborhoodPipeline->Recompile();
}

glm::vec3 Renderer::GetLightPositionForShadowMapCalculation()
{
    KBRAssert(s_Data != nullptr, "Renderer not initialized!");

    return s_Data->ShadowMap.LightPosForCalculation;
}

DepthBias& Renderer::GetShadowMapDepthBiasSettings()
{
    KBRAssert(s_Data != nullptr, "Renderer not initialized!");

    return s_Data->DepthBias;
}

bool& Renderer::GetIsPCFEnabledForShadowMap()
{
    KBRAssert(s_Data != nullptr, "Renderer not initialized!");

    return s_Data->ShadowMap.EnablePCF;
}

bool& Renderer::GetDisplayDebugNormals()
{
    KBRAssert(s_Data != nullptr, "Renderer not initialized!");

    return s_Data->DisplayDebugNormals;
}

bool& Renderer::GetDisplayPhysicsColliders()
{
    KBRAssert(s_Data != nullptr, "Renderer not initialized!");

    return s_Data->DisplayPhysicsColliders;
}

bool& Renderer::GetDisplaySkybox()
{
    KBRAssert(s_Data != nullptr, "Renderer not initialized!");

    return s_Data->Skybox.ShowSkybox;
}

bool& Renderer::GetUseRayQueryBasedShadows()
{
    KBRAssert(s_Data != nullptr, "Renderer not initialized!");

    return s_Data->UseRayQueryBasedShadows;
}

bool& Renderer::GetUseRayQueryBasedSoftShadows()
{
    KBRAssert(s_Data != nullptr, "Renderer not initialized!");

    return s_Data->UseRayQueryBasedSoftShadows;
}

bool& Renderer::GetUseGTAO()
{
    KBRAssert(s_Data != nullptr, "Renderer not initialized!");

    return s_Data->UseGTAO;
}

bool& Renderer::GetUseBlurForGTAO()
{
    return s_Data->UseBlurredGTAO;
}

GTAOConstants& Renderer::GetGTAOConstants()
{
    return s_Data->GTAOData;
}

float& Renderer::GetGamma()
{
    KBRAssert(s_Data != nullptr, "Renderer not initialized!");

    return s_Data->GlobalLightingData.gamma;
}

float& Renderer::GetExposure()
{
    KBRAssert(s_Data != nullptr, "Renderer not initialized!");

    return s_Data->GlobalLightingData.exposure;
}

uint32_t Renderer::GetShadowMapCascadeCount()
{
    return ShadowMap::CascadeCount;
}

uint32_t Renderer::GetShadowMapResolution()
{
    return s_Data->ShadowMap.Size;
}

AntiAliasingMode& Renderer::GetAntiAliasingMode()
{
    return s_Data->AntiAliasingMode;
}

uint32_t Renderer::GetBloomMipLevels()
{
    return s_Data->Bloom.MipLevels;
}

void Renderer::SetBloomMipLevels(uint32_t levels)
{
    if (levels > BloomData::MaxMipLevels) {
        Log::CoreWarn("Attempted to set bloom mip levels to {}, which exceeds the maximum of {}. Clamping to maximum.",
                      levels,
                      BloomData::MaxMipLevels);
        levels = BloomData::MaxMipLevels;
    }
    s_Data->Bloom.MipLevels = levels;
}

float& Renderer::GetBloomIntensity()
{
    return s_Data->Bloom.Intensity;
}

BloomMode& Renderer::GetBloomMode()
{
    return s_Data->Bloom.Mode;
}

float& Renderer::GetBloomThreshold()
{
    return s_Data->Bloom.Threshold;
}

float& Renderer::GetBloomKnee()
{
    return s_Data->Bloom.Knee;
}

float& Renderer::GetBloomMaxBrightness()
{
    return s_Data->Bloom.MaxBrightness;
}

TonemappingOperator& Renderer::GetTonemappingOperator()
{
    return s_Data->TonemappingOperator;
}

bool& Renderer::GetUseFrustumCulling()
{
    return s_Data->UseFrustumCulling;
}

bool& Renderer::GetFreezeFrustum()
{
    return s_Data->FreezeFrustum;
}

uint32_t Renderer::GetAllObjectCount()
{
    return s_Data->AllObjectCount;
}

uint32_t Renderer::GetVisibleObjectCount()
{
    return s_Data->VisibleObjectCount;
}

uint32_t Renderer::GetCulledObjectCount()
{
    return s_Data->CulledObjectCount;
}

glm::vec2 Renderer::GetOutputImageSize()
{
    return s_Data->OutputSize;
}

uint64_t Renderer::GetCompositedOutputImageID()
{
    KBRAssert(s_Data->ColorOutputDescriptorSet,
                    "ImGui descriptor set is not created for composited color output image!");

    return reinterpret_cast<uint64_t>(static_cast<VkDescriptorSet>(s_Data->ColorOutputDescriptorSet));
}

uint64_t Renderer::GetShadowMapDepthImageID(uint32_t index)
{
    KBRAssert(index < s_Data->ShadowMapDescriptorSet.size(), "Out of bounds index for shadow map depth image!");
    KBRAssert(s_Data->ShadowMapDescriptorSet[index],
                    std::format("ImGui descriptor set is not created for shadow map depth image {}!", index));

    return reinterpret_cast<uint64_t>(static_cast<VkDescriptorSet>(s_Data->ShadowMapDescriptorSet[index]));
}

void Renderer::RequestMousePickingPixel(const uint32_t x, const uint32_t y)
{
    KBRAssert(s_Data != nullptr, "Renderer not initialized!");

    if (x >= static_cast<uint32_t>(s_Data->OutputSize.x) || y >= static_cast<uint32_t>(s_Data->OutputSize.y))
        return;

    s_Data->MousePickingReadback.RequestedPixel = { x, y };
    s_Data->MousePickingReadback.RequestPending = true;
}

std::optional<uint32_t> Renderer::GetMousePickingEntityID()
{
    KBRAssert(s_Data != nullptr, "Renderer not initialized!");

    return s_Data->MousePickingReadback.LatestEntityID;
}

bool Renderer::ConsumePendingMousePickingTimelineSignal(vk::Semaphore& semaphore, uint64_t& value)
{
    KBRAssert(s_Data != nullptr, "Renderer not initialized!");

    if (s_Data->MousePickingReadback.PendingTimelineSignalValue == 0 ||
        s_Data->MousePickingReadback.TimelineSemaphore == nullptr)
        return false;

    semaphore = s_Data->MousePickingReadback.TimelineSemaphore;
    value = s_Data->MousePickingReadback.PendingTimelineSignalValue;
    s_Data->MousePickingReadback.PendingTimelineSignalValue = 0;
    return true;
}

GPUTimings Renderer::GetLatestGPUTimings()
{
    return s_Data->LatestGPUTimings;
}

RenderStatistics Renderer::GetLatestRenderStatistics()
{
    return s_Data->LatestRenderStatistics;
}

PipelineStatistics Renderer::GetLatestPipelineStatistics()
{
    return s_Data->LatestPipelineStatistics;
}

void Renderer::WriteGPUTimestamp(const vk::raii::CommandBuffer& cmd, const uint32_t frameIndex, const uint32_t index)
{
    if (!s_Data->SupportsGPUTimestamps || frameIndex >= s_Data->GPUTimestampQueryPools.size() ||
        s_Data->GPUTimestampQueryPools[frameIndex] == nullptr)
        return;

    cmd.writeTimestamp2(vk::PipelineStageFlagBits2::eBottomOfPipe, s_Data->GPUTimestampQueryPools[frameIndex], index);
}

void Renderer::ResolveGPUTimings(const uint32_t frameIndex)
{
    if (!s_Data->SupportsGPUTimestamps || frameIndex >= s_Data->GPUTimestampQueryPools.size() ||
        s_Data->GPUTimestampQueryPools[frameIndex] == nullptr) {
        s_Data->LatestGPUTimings.IsValid = false;
        return;
    }

    std::array<uint64_t, static_cast<size_t>(GPUTimestampQuery::Count)> timestamps{};
    const auto& device = VulkanContext::Get().GetDevice();
    const vk::Result result =
        static_cast<vk::Device>(device).getQueryPoolResults(s_Data->GPUTimestampQueryPools[frameIndex],
                                                            0,
                                                            static_cast<uint32_t>(timestamps.size()),
                                                            timestamps.size() * sizeof(uint64_t),
                                                            timestamps.data(),
                                                            sizeof(uint64_t),
                                                            vk::QueryResultFlagBits::e64);

    if (result != vk::Result::eSuccess) {
        s_Data->LatestGPUTimings.IsValid = false;
        return;
    }

    auto toMilliseconds = [&](const GPUTimestampQuery begin, const GPUTimestampQuery end) -> float {
        const auto beginTicks = timestamps[static_cast<size_t>(begin)];
        const auto endTicks = timestamps[static_cast<size_t>(end)];
        if (endTicks <= beginTicks)
            return 0.0f;

        const double deltaTicks = static_cast<double>(endTicks - beginTicks);
        const double nanoseconds = deltaTicks * static_cast<double>(s_Data->GPUTimestampPeriodNanoseconds);
        return static_cast<float>(nanoseconds * 1e-6);
    };

    s_Data->LatestGPUTimings.FrameMilliseconds =
        toMilliseconds(GPUTimestampQuery::FrameBegin, GPUTimestampQuery::FrameEnd);
    s_Data->LatestGPUTimings.DepthPrePassMilliseconds =
        toMilliseconds(GPUTimestampQuery::DepthPrePassBegin, GPUTimestampQuery::DepthPrePassEnd);
    s_Data->LatestGPUTimings.ShadowPassMilliseconds =
        toMilliseconds(GPUTimestampQuery::ShadowBegin, GPUTimestampQuery::ShadowEnd);
    s_Data->LatestGPUTimings.OpaquePassMilliseconds =
        toMilliseconds(GPUTimestampQuery::OpaqueBegin, GPUTimestampQuery::OpaqueEnd);
    s_Data->LatestGPUTimings.GrassPassMilliseconds =
        toMilliseconds(GPUTimestampQuery::GrassBegin, GPUTimestampQuery::GrassEnd);
    s_Data->LatestGPUTimings.ParticlesSimulateMilliseconds =
        toMilliseconds(GPUTimestampQuery::ParticlesSimulateBegin, GPUTimestampQuery::ParticlesSimulateEnd);
    s_Data->LatestGPUTimings.ParticlesDrawMilliseconds =
        toMilliseconds(GPUTimestampQuery::ParticlesDrawBegin, GPUTimestampQuery::ParticlesDrawEnd);
    s_Data->LatestGPUTimings.TransparentPassMilliseconds =
        toMilliseconds(GPUTimestampQuery::TransparentBegin, GPUTimestampQuery::TransparentEnd);
    s_Data->LatestGPUTimings.TransparencyResolvePassMilliseconds =
        toMilliseconds(GPUTimestampQuery::TransparencyResolveBegin, GPUTimestampQuery::TransparencyResolveEnd);
    s_Data->LatestGPUTimings.BloomPassMilliseconds =
        toMilliseconds(GPUTimestampQuery::BloomPassBegin, GPUTimestampQuery::BloomPassEnd);
    s_Data->LatestGPUTimings.AmbientOcclusionPassMilliseconds =
        toMilliseconds(GPUTimestampQuery::AmbientOcclusionPassBegin, GPUTimestampQuery::AmbientOcclusionPassEnd);
    s_Data->LatestGPUTimings.AntialiasingPassMilliseconds =
        toMilliseconds(GPUTimestampQuery::AntialiasingPassBegin, GPUTimestampQuery::AntialiasingPassEnd);
    s_Data->LatestGPUTimings.TonemappingPassMilliseconds =
        toMilliseconds(GPUTimestampQuery::TonemappingPassBegin, GPUTimestampQuery::TonemappingPassEnd);
    s_Data->LatestGPUTimings.IsValid = true;
}

void Renderer::ResetQueryPools(const vk::raii::CommandBuffer& cmd, const uint32_t frameIndex)
{
    if (s_Data->SupportsGPUTimestamps) {
        KBRAssert(frameIndex < s_Data->GPUTimestampQueryPools.size(),
                        "Current frame index exceeds GPU Timestamp Query Pools size!");
        KBRAssert(s_Data->GPUTimestampQueryPools[frameIndex] != nullptr,
                        "GPU Timestamp Query Pool for current frame is null!");

        cmd.resetQueryPool(
            s_Data->GPUTimestampQueryPools[frameIndex], 0, static_cast<uint32_t>(GPUTimestampQuery::Count));
    }
    if (s_Data->SupportsPipelineStatistics) {
        KBRAssert(frameIndex < s_Data->PipelineStatisticsQueryPools.size(),
                        "Current frame index exceeds Pipeline Statistics Query Pools size!");
        KBRAssert(s_Data->PipelineStatisticsQueryPools[frameIndex] != nullptr,
                        "Pipeline Statistics Query Pool for current frame is null!");

        cmd.resetQueryPool(s_Data->PipelineStatisticsQueryPools[frameIndex], 0, 2);

        KBRAssert(frameIndex < s_Data->MeshPipelineStatisticsQueryPools.size(),
                        "Current frame index exceeds Mesh Pipeline Statistics Query Pools size!");
        KBRAssert(s_Data->MeshPipelineStatisticsQueryPools[frameIndex] != nullptr,
                        "Mesh Pipeline Statistics Query Pool for current frame is null!");

        cmd.resetQueryPool(s_Data->MeshPipelineStatisticsQueryPools[frameIndex], 0, 1);
    }
}

void Renderer::ResolvePipelineStatistics(const uint32_t frameIndex)
{
    if (!s_Data->SupportsPipelineStatistics)
        return;

    KBRAssert(frameIndex < s_Data->PipelineStatisticsQueryPools.size(),
                    "Current frame index exceeds Pipeline Statistics Query Pools size!");
    KBRAssert(s_Data->PipelineStatisticsQueryPools[frameIndex] != nullptr,
                    "Pipeline Statistics Query Pool for current frame is null!");

    KBRAssert(frameIndex < s_Data->MeshPipelineStatisticsQueryPools.size(),
                    "Current frame index exceeds Mesh Pipeline Statistics Query Pools size!");
    KBRAssert(s_Data->MeshPipelineStatisticsQueryPools[frameIndex] != nullptr,
                    "Mesh Pipeline Statistics Query Pool for current frame is null!");

    // Traditional pipeline statistics

    constexpr uint32_t statisticsPerQuery = 4;
    constexpr uint32_t totalStatisticsCount = statisticsPerQuery * 2;
    std::array<uint64_t, totalStatisticsCount> results{};
    const auto& device = VulkanContext::Get().GetDevice();

    vk::Result result =
        static_cast<vk::Device>(device).getQueryPoolResults(s_Data->PipelineStatisticsQueryPools[frameIndex],
                                                            0,
                                                            2,
                                                            results.size() * sizeof(uint64_t),
                                                            results.data(),
                                                            statisticsPerQuery * sizeof(uint64_t),
                                                            vk::QueryResultFlagBits::e64);

    // On the first VulkanContext::GetMaxFramesInFlight calls, this will return NotReady
    if (result != vk::Result::eSuccess && result != vk::Result::eNotReady) {
        Log::CoreError("Failed to retrieve pipeline statistics query results: {}", vk::to_string(result));
        return;
    }

    s_Data->LatestPipelineStatistics.InputAssemblyVertices = results[0] + results[4];
    s_Data->LatestPipelineStatistics.InputAssemblyPrimitives = results[1] + results[5];
    s_Data->LatestPipelineStatistics.VertexShaderInvocations = results[2] + results[6];
    s_Data->LatestPipelineStatistics.FragmentShaderInvocations = results[3] + results[7];

    // Mesh pipeline statistics
    constexpr uint32_t statisticsPerMeshQuery = 3;
    constexpr uint32_t totalMeshStatisticsCount = statisticsPerMeshQuery * 1;
    std::array<uint64_t, totalMeshStatisticsCount> meshResults{};

    result = static_cast<vk::Device>(device).getQueryPoolResults(s_Data->MeshPipelineStatisticsQueryPools[frameIndex],
                                                                 0,
                                                                 1,
                                                                 meshResults.size() * sizeof(uint64_t),
                                                                 meshResults.data(),
                                                                 statisticsPerMeshQuery * sizeof(uint64_t),
                                                                 vk::QueryResultFlagBits::e64);

    // On the first VulkanContext::GetMaxFramesInFlight calls, this will return NotReady
    if (result != vk::Result::eSuccess && result != vk::Result::eNotReady) {
        Log::CoreError("Failed to retrieve mesh pipeline statistics query results: {}", vk::to_string(result));
        return;
    }

    s_Data->LatestPipelineStatistics.TaskShaderInvocations = meshResults[0];
    s_Data->LatestPipelineStatistics.MeshShaderInvocations = meshResults[1];
    s_Data->LatestPipelineStatistics.FragmentShaderInvocations += meshResults[2];
}

void Renderer::UpdateLights(const uint32_t currentImage, const std::vector<GPULight>& sceneLights)
{
    std::memcpy(s_Data->UniformBuffers[currentImage].globalLighting->GetMappedData(),
                &s_Data->GlobalLightingData,
                sizeof(GlobalLighting));

    std::memcpy(s_Data->StorageBuffers[currentImage].Lights->GetMappedData(),
                sceneLights.data(),
                sceneLights.size() * sizeof(GPULight));
}

void Renderer::UpdateSceneUniformBuffers(const uint32_t currentImage,
                                         const Camera* mainCamera,
                                         const uint32_t temporalIndex,
                                         const std::vector<glm::mat4>& lightSpaceMatrices,
                                         const glm::vec4& cascadeSplits,
                                         const uint32_t lightCount,
                                         const float deltaTime)
{
    const glm::mat4& projection = mainCamera->GetProjectionMatrix();
    const glm::mat4& view = mainCamera->GetViewMatrix();
    const glm::vec3 camPos = mainCamera->GetPosition();

    UpdateSceneUniformBuffers(currentImage,
                              view,
                              projection,
                              camPos,
                              temporalIndex,
                              lightSpaceMatrices,
                              cascadeSplits,
                              lightCount,
                              deltaTime);
}

void Renderer::UpdateSceneUniformBuffers(const uint32_t currentImage,
                                         const glm::mat4& view,
                                         const glm::mat4& projection,
                                         const glm::vec3& camPos,
                                         uint32_t /*temporalIndex*/,
                                         const std::vector<glm::mat4>& lightSpaceMatrices,
                                         const glm::vec4& cascadeSplits,
                                         const uint32_t lightCount,
                                         const float deltaTime)
{
    s_Data->SceneUniformData.projection = projection;
    s_Data->SceneUniformData.view = view;
    s_Data->SceneUniformData.camPos = camPos;
    s_Data->SceneUniformData.cascadeSplits = cascadeSplits;
    s_Data->SceneUniformData.lightCount = lightCount;
    s_Data->SceneUniformData.viewportSize = s_Data->OutputSize;

    s_Data->SceneUniformData.cameraRight = glm::vec4(view[0][0], view[1][0], view[2][0], 0.0f);
    s_Data->SceneUniformData.cameraUp = glm::vec4(view[0][1], view[1][1], view[2][1], 0.0f);
    s_Data->SceneUniformData.deltaTime = deltaTime;

    static float time = 0.0f;
    time += deltaTime;
    s_Data->SceneUniformData.time = time;

    for (size_t i = 0; i < s_Data->SceneUniformData.lightSpaceMatrices.size(); ++i) {
        s_Data->SceneUniformData.lightSpaceMatrices[i] = lightSpaceMatrices[i];
    }

    if (s_Data->SceneUniformData.lightSpaceMatrices.size() != lightSpaceMatrices.size()) {
        Log::CoreError("Number of light space matrices exceeds the maximum supported count of {}",
                       s_Data->SceneUniformData.lightSpaceMatrices.size());
    }

    std::memcpy(s_Data->UniformBuffers[currentImage].scene->GetMappedData(),
                &s_Data->SceneUniformData,
                sizeof(SceneUniformData));

    const glm::mat4 skyboxModel = glm::mat4(glm::mat3(view));
    s_Data->SkyboxData.model = skyboxModel;
    s_Data->SkyboxData.projection = projection;
    std::memcpy(s_Data->UniformBuffers[currentImage].skybox->GetMappedData(), &s_Data->SkyboxData, sizeof(SkyboxData));

    s_Data->GTAOData.projectionMatrix = projection;
    s_Data->GTAOData.invProjectionMatrix = glm::inverse(projection);
    s_Data->GTAOData.viewportSize = s_Data->OutputSize;
    s_Data->GTAOData.temporalIndex = 1.0f;
    // s_Data->GTAOData.temporalIndex = static_cast<float>(temporalIndex); // TODO: Add temporal-spatial denoiser
    std::memcpy(s_Data->UniformBuffers[currentImage].gtao->GetMappedData(), &s_Data->GTAOData, sizeof(GTAOConstants));
}

void Renderer::UpdatePerObjectUniformBuffer(const uint32_t currentImage,
                                            const uint32_t objectIndex,
                                            const glm::mat4& model,
                                            const Material& material,
                                            const uint32_t entityID)
{
    s_Data->PerObjectData = {
        .model = model, .worldNormal = glm::inverseTranspose(model), .material = material.Params, .entityID = entityID
    };

    char* data = static_cast<char*>(s_Data->UniformBuffers[currentImage].perObject->GetMappedData());
    data += static_cast<size_t>(objectIndex) * s_Data->DynamicAlignment;

    std::memcpy(data, &s_Data->PerObjectData, sizeof(PerObjectData));
}

std::vector<GPULight> Renderer::GetLightsFromScene(const Scene& scene)
{
    std::vector<GPULight> sceneLights;

    const auto pointLightView = scene.m_Registry.view<PointLightComponent, TransformComponent>();
    for (const auto entity : pointLightView) {
        auto& pl = pointLightView.get<PointLightComponent>(entity);
        if (!pl.IsEnabled)
            continue;

        const auto& tsc = pointLightView.get<TransformComponent>(entity);
        const glm::vec3 position = tsc.WorldTransform[3];

        GPULight gpuLight{};
        gpuLight.Type = static_cast<uint32_t>(LightType::Point);
        gpuLight.Position = position;
        gpuLight.Color = pl.Light.Color;
        gpuLight.Intensity = pl.Light.Intensity;
        gpuLight.Range = pl.Light.Radius;
        sceneLights.push_back(gpuLight);
    }

    const auto spotLightView = scene.m_Registry.view<SpotLightComponent, TransformComponent>();
    for (const auto entity : spotLightView) {
        const auto& sl = spotLightView.get<SpotLightComponent>(entity);
        if (!sl.IsEnabled)
            continue;

        const auto& tsc = spotLightView.get<TransformComponent>(entity);
        const glm::vec3 position = tsc.WorldTransform[3];

        GPULight gpuLight{};
        gpuLight.Type = static_cast<uint32_t>(LightType::Spot);
        gpuLight.Position = position;
        gpuLight.Direction = sl.Light.Direction;
        gpuLight.Color = sl.Light.Color;
        gpuLight.Intensity = sl.Light.Intensity;
        gpuLight.Range = sl.Light.Radius;
        gpuLight.InnerConeCos = glm::cos(sl.Light.CutOffAngleRadians);
        gpuLight.OuterConeCos = glm::cos(sl.Light.OuterCutOffAngleRadians);
        sceneLights.push_back(gpuLight);
    }

    // TODO: Add area light when they are implemented

    return sceneLights;
}

static AABB CalculateWorldAABB(const AABB& localAABB, const glm::mat4& worldTransform)
{
    AABB worldAABB;

    const glm::mat3 rotScaleMatrix = glm::mat3(worldTransform);

    glm::mat3 absRotScaleMat;
    for (int col = 0; col < 3; ++col) {
        for (int row = 0; row < 3; ++row) {
            absRotScaleMat[col][row] = std::abs(rotScaleMatrix[col][row]);
        }
    }

    const glm::vec3 localMin = localAABB.Min;
    const glm::vec3 localMax = localAABB.Max;

    const glm::vec3 worldCenter = glm::vec3(worldTransform * glm::vec4((localMin + localMax) * 0.5f, 1.0f));

    const glm::vec3 localExtents = (localMax - localMin) * 0.5f;

    const glm::vec3 worldExtents = absRotScaleMat * localExtents;

    worldAABB.Min = worldCenter - worldExtents;
    worldAABB.Max = worldCenter + worldExtents;

    return worldAABB;
}

std::pair<std::pmr::vector<RenderObject>, std::pmr::set<Ref<Material>>> Renderer::GetRenderObjectsAndUniqueMaterialsFromScene(const Scene& scene, std::pmr::memory_resource* arena)
{
    static uint32_t renderObjectCountFromLastFrame = 0;

    std::pmr::vector<RenderObject> renderObjects(arena);
    std::pmr::set<Ref<Material>> uniqueMaterials(arena);
    renderObjects.reserve(renderObjectCountFromLastFrame);

    uint32_t entityCount = 0;

    const auto meshView = scene.m_Registry.view<TransformComponent, StaticMeshComponent, TagComponent>();
    for (const auto entity : meshView) {
        auto& transform = meshView.get<TransformComponent>(entity);
        auto& staticMesh = meshView.get<StaticMeshComponent>(entity);
        if (!staticMesh.Visible || !staticMesh.StaticMesh /* || !staticMesh.MeshMaterial*/)
            continue;

        RenderObject renderObject{};
        // renderObject.Transform = transform.GetTransform();
        renderObject.Transform = transform.WorldTransform;
        renderObject.Mesh = staticMesh.StaticMesh;
        renderObject.Material = staticMesh.MeshMaterial;
        renderObject.EntityID = static_cast<uint32_t>(entity);
        renderObject.DebugName = meshView.get<TagComponent>(entity).Tag;

        renderObject.WorldAABB = CalculateWorldAABB(staticMesh.StaticMesh->GetBoundingBox(), renderObject.Transform);

        renderObject.UBOIndex = entityCount++;

        renderObjects.push_back(renderObject);
        if (renderObject.Material)
            uniqueMaterials.insert(renderObject.Material);
    }

    renderObjectCountFromLastFrame = static_cast<uint32_t>(renderObjects.size());

    return { renderObjects, uniqueMaterials };
}

std::vector<LineVertex> Renderer::GetColliderLineVerticesFromScene(const Scene& scene)
{
    std::vector<LineVertex> vertices;
    vertices.reserve(4096);

    const auto boxView = scene.m_Registry.view<TransformComponent, BoxCollider3DComponent>();
    for (const auto entity : boxView) {
        const auto& transform = boxView.get<TransformComponent>(entity);
        const auto& collider = boxView.get<BoxCollider3DComponent>(entity);
        const glm::mat4 base =
            GetWorldTransformWithoutScale(transform) * glm::translate(glm::mat4(1.0f), collider.Offset);
        ColliderDebugHelpers::AddBoxLines(vertices, base, collider.Size);
    }

    const auto sphereView = scene.m_Registry.view<TransformComponent, SphereCollider3DComponent>();
    for (const auto entity : sphereView) {
        const auto& transform = sphereView.get<TransformComponent>(entity);
        const auto& collider = sphereView.get<SphereCollider3DComponent>(entity);
        const glm::mat4 base =
            GetWorldTransformWithoutScale(transform) * glm::translate(glm::mat4(1.0f), collider.Offset);
        ColliderDebugHelpers::AddCircleLines(vertices, base, 0, 1, collider.Radius);
        ColliderDebugHelpers::AddCircleLines(vertices, base, 1, 2, collider.Radius);
        ColliderDebugHelpers::AddCircleLines(vertices, base, 2, 0, collider.Radius);
    }

    const auto capsuleView = scene.m_Registry.view<TransformComponent, CapsuleCollider3DComponent>();
    for (const auto entity : capsuleView) {
        const auto& transform = capsuleView.get<TransformComponent>(entity);
        const auto& collider = capsuleView.get<CapsuleCollider3DComponent>(entity);
        const glm::mat4 base =
            GetWorldTransformWithoutScale(transform) * glm::translate(glm::mat4(1.0f), collider.Offset);
        ColliderDebugHelpers::AddCapsuleLines(vertices, base, collider.Radius, collider.Height * 0.5f);
    }

    const auto meshColliderView = scene.m_Registry.view<TransformComponent, MeshCollider3DComponent>();
    for (const auto entity : meshColliderView) {
        const auto& transform = meshColliderView.get<TransformComponent>(entity);
        const auto& collider = meshColliderView.get<MeshCollider3DComponent>(entity);

        Ref<Mesh> mesh = collider.Mesh;
        if (!mesh && scene.m_Registry.all_of<StaticMeshComponent>(entity)) {
            mesh = scene.m_Registry.get<StaticMeshComponent>(entity).StaticMesh;
        }
        if (!mesh || mesh->GetVertices().empty()) {
            continue;
        }

        glm::vec3 minPoint(std::numeric_limits<float>::max());
        glm::vec3 maxPoint(-std::numeric_limits<float>::max());
        for (const auto& vertex : mesh->GetVertices()) {
            minPoint = glm::min(minPoint, vertex.Position);
            maxPoint = glm::max(maxPoint, vertex.Position);
        }
        const glm::vec3 halfExtents = (maxPoint - minPoint) * 0.5f;
        const glm::vec3 center = (maxPoint + minPoint) * 0.5f;

        const glm::mat4 base =
            GetWorldTransformWithoutScale(transform) * glm::translate(glm::mat4(1.0f), collider.Offset + center);
        ColliderDebugHelpers::AddBoxLines(vertices, base, halfExtents);
    }

    return vertices;
}

Renderer::RenderObjectContainer Renderer::FrustumCullRenderObjects(const RenderObjectContainer& renderObjects,
                                                                   const Frustum& frustum,
                                                                   std::pmr::memory_resource* arena)
{
    RenderObjectContainer culledObjects(arena);
    culledObjects.reserve(renderObjects.size());

    for (const auto& renderObject : renderObjects) {
        if (renderObject.WorldAABB.IsInsideFrustum(frustum)) {
            culledObjects.push_back(renderObject);
        }
    }

    return culledObjects;
}

void Renderer::RenderShadowPass(const vk::raii::CommandBuffer& cmd,
                                uint32_t frameIndex,
                                const RenderObjectContainer& renderObjects,
                                std::pmr::memory_resource* arena)
{
    vk::ImageMemoryBarrier2 barrier = { .srcStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                                                        vk::PipelineStageFlagBits2::eLateFragmentTests,
                                        .srcAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
                                        .dstStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                                                        vk::PipelineStageFlagBits2::eLateFragmentTests,
                                        .dstAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentRead |
                                                         vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
                                        .oldLayout = vk::ImageLayout::eUndefined,
                                        .newLayout = vk::ImageLayout::eDepthAttachmentOptimal,
                                        .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
                                        .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
                                        .image = s_Data->ShadowMap.Image,
                                        .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eDepth,
                                                              .baseMipLevel = 0,
                                                              .levelCount = 1,
                                                              .baseArrayLayer = 0,
                                                              .layerCount = ShadowMap::CascadeCount } };

    const vk::DependencyInfo dependencyInfo = { .dependencyFlags = {},
                                                .imageMemoryBarrierCount = 1,
                                                .pImageMemoryBarriers = &barrier };

    cmd.pipelineBarrier2(dependencyInfo);

    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *s_Data->ShadowMap.Pipeline->GetVulkanPipeline());
    cmd.setDepthBias(s_Data->DepthBias.ConstantFactor, s_Data->DepthBias.Clamp, s_Data->DepthBias.SlopeFactor);

    const vk::Rect2D renderArea{ .offset = vk::Offset2D{ .x = 0, .y = 0 },
                                 .extent = vk::Extent2D{ .width = s_Data->ShadowMap.Size,
                                                         .height = s_Data->ShadowMap.Size } };

    static constexpr std::array<std::string_view, ShadowMap::CascadeCount> ShadowCascadePassLabels = {
        "Shadow Pass (Cascade 0)", "Shadow Pass (Cascade 1)", "Shadow Pass (Cascade 2)", "Shadow Pass (Cascade 3)"
    };
    for (uint32_t cascadeIndex = 0; cascadeIndex < ShadowMap::CascadeCount; ++cascadeIndex) {
        vk::RenderingAttachmentInfo shadowMapDepthAttachmentInfo{
            .imageView = s_Data->ShadowMap.WriteImageViews[cascadeIndex],
            .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
            .loadOp = vk::AttachmentLoadOp::eClear,
            .storeOp = vk::AttachmentStoreOp::eStore,
            .clearValue = vk::ClearDepthStencilValue{ .depth = 1.0f, .stencil = 0 }
        };

        const vk::RenderingInfo shadowMapRenderingInfo{ .renderArea = renderArea,
                                                        .layerCount = 1,
                                                        .colorAttachmentCount = 0,
                                                        .pColorAttachments = nullptr,
                                                        .pDepthAttachment = &shadowMapDepthAttachmentInfo };

        BeginRenderPassDebugLabel(cmd, ShadowCascadePassLabels[cascadeIndex]);
        cmd.beginRendering(shadowMapRenderingInfo);
        cmd.setViewport(0,
                        vk::Viewport{ .x = 0.0f,
                                      .y = 0.0f,
                                      .width = static_cast<float>(s_Data->ShadowMap.Size),
                                      .height = static_cast<float>(s_Data->ShadowMap.Size),
                                      .minDepth = 0.0f,
                                      .maxDepth = 1.0f });
        cmd.setScissor(0, renderArea);

        cmd.pushConstants<uint32_t>(
            *s_Data->ShadowMap.PipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, { cascadeIndex });

        // Cull objects
        Frustum frustum = Frustum::CreateFromViewProjection(s_Data->SceneUniformData.lightSpaceMatrices[cascadeIndex]);
        const RenderObjectContainer culledObjects = FrustumCullRenderObjects(renderObjects, frustum, arena);

        for (const auto& renderObject : culledObjects) {
            if (renderObject.Material != nullptr && renderObject.Material->IsTransparent())
                continue;

            const uint32_t dynamicOffset = static_cast<uint32_t>(renderObject.UBOIndex * s_Data->DynamicAlignment);

            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                   *s_Data->ShadowMap.PipelineLayout,
                                   0,
                                   { s_Data->DescriptorSets[frameIndex].scene },
                                   { dynamicOffset });

            renderObject.Mesh->Draw(cmd);
        }

        cmd.endRendering();
        EndRenderPassDebugLabel(cmd);
    }
}

void Renderer::RenderParticles(const vk::raii::CommandBuffer& cmd, const uint32_t frameIndex)
{
    WriteGPUTimestamp(cmd, frameIndex, static_cast<uint32_t>(GPUTimestampQuery::ParticlesDrawBegin));

    // Transition depth attachment to read-only optimal
    const vk::ImageMemoryBarrier2 depthBarrierIn = {
        .srcStageMask =
            vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
        .srcAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                        vk::PipelineStageFlagBits2::eLateFragmentTests | vk::PipelineStageFlagBits2::eFragmentShader,
        .dstAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eShaderSampledRead,
        .oldLayout = vk::ImageLayout::eDepthAttachmentOptimal,
        .newLayout = vk::ImageLayout::eDepthReadOnlyOptimal,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = *s_Data->DepthImage.Image,
        .subresourceRange = { vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1 }
    };
    const vk::DependencyInfo dependencyInfoIn = { .dependencyFlags = {},
                                                  .imageMemoryBarrierCount = 1,
                                                  .pImageMemoryBarriers = &depthBarrierIn };

    cmd.pipelineBarrier2(dependencyInfoIn);

    vk::RenderingAttachmentInfo colorAttachmentInfo{
        .imageView = s_Data->ColorImage.ImageView,
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eLoad,
        .storeOp = vk::AttachmentStoreOp::eStore,
    };

    vk::RenderingAttachmentInfo depthAttachmentInfo{
        .imageView = s_Data->DepthImage.ImageView,
        .imageLayout = vk::ImageLayout::eDepthReadOnlyOptimal,
        .loadOp = vk::AttachmentLoadOp::eLoad,
        .storeOp = vk::AttachmentStoreOp::eStore,
    };

    const vk::Rect2D renderArea{ .offset = vk::Offset2D{ .x = 0, .y = 0 },
                                 .extent = vk::Extent2D{ .width = static_cast<uint32_t>(s_Data->OutputSize.x),
                                                         .height = static_cast<uint32_t>(s_Data->OutputSize.y) } };

    const vk::Viewport viewport{ .x = 0.0f,
                                 .y = 0.0f,
                                 .width = s_Data->OutputSize.x,
                                 .height = s_Data->OutputSize.y,
                                 .minDepth = 0.0f,
                                 .maxDepth = 1.0f };

    const vk::RenderingInfo renderingInfo{ .renderArea = renderArea,
                                           .layerCount = 1,
                                           .colorAttachmentCount = 1,
                                           .pColorAttachments = &colorAttachmentInfo,
                                           .pDepthAttachment = &depthAttachmentInfo };

    BeginRenderPassDebugLabel(cmd, "Particles Draw Pass");
    cmd.beginRendering(renderingInfo);
    cmd.setViewport(0, viewport);
    cmd.setScissor(0, renderArea);

    s_Data->ParticleSystem.RecordDraw(cmd, frameIndex);

    cmd.endRendering();
    EndRenderPassDebugLabel(cmd);

    const vk::ImageMemoryBarrier2 depthBarrierOut = {
        .srcStageMask = vk::PipelineStageFlagBits2::eFragmentShader | vk::PipelineStageFlagBits2::eLateFragmentTests,
        .srcAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eShaderSampledRead |
                         vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
        .dstStageMask =
            vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
        .dstAccessMask =
            vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
        .oldLayout = vk::ImageLayout::eDepthReadOnlyOptimal,
        .newLayout = vk::ImageLayout::eDepthAttachmentOptimal,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = *s_Data->DepthImage.Image,
        .subresourceRange = { vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1 }
    };

    const vk::DependencyInfo dependencyInfoOut = { .dependencyFlags = {},
                                                   .imageMemoryBarrierCount = 1,
                                                   .pImageMemoryBarriers = &depthBarrierOut };

    cmd.pipelineBarrier2(dependencyInfoOut);

    WriteGPUTimestamp(cmd, frameIndex, static_cast<uint32_t>(GPUTimestampQuery::ParticlesDrawEnd));
}

void Renderer::RenderGrass(const vk::raii::CommandBuffer& cmd, const uint32_t frameIndex)
{
    WriteGPUTimestamp(cmd, frameIndex, static_cast<uint32_t>(GPUTimestampQuery::GrassBegin));

    vk::RenderingAttachmentInfo colorAttachmentInfo{
        .imageView = s_Data->ColorImage.ImageView,
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eLoad,
        .storeOp = vk::AttachmentStoreOp::eStore,
    };

    vk::RenderingAttachmentInfo depthAttachmentInfo{
        .imageView = s_Data->DepthImage.ImageView,
        .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eLoad,
        .storeOp = vk::AttachmentStoreOp::eDontCare,
    };

    const vk::Rect2D renderArea{ .offset = vk::Offset2D{ .x = 0, .y = 0 },
                                 .extent = vk::Extent2D{ .width = static_cast<uint32_t>(s_Data->OutputSize.x),
                                                         .height = static_cast<uint32_t>(s_Data->OutputSize.y) } };

    const vk::Viewport viewport{ .x = 0.0f,
                                 .y = 0.0f,
                                 .width = s_Data->OutputSize.x,
                                 .height = s_Data->OutputSize.y,
                                 .minDepth = 0.0f,
                                 .maxDepth = 1.0f };

    const vk::RenderingInfo renderingInfo{ .renderArea = renderArea,
                                           .layerCount = 1,
                                           .colorAttachmentCount = 1,
                                           .pColorAttachments = &colorAttachmentInfo,
                                           .pDepthAttachment = &depthAttachmentInfo };

    BeginRenderPassDebugLabel(cmd, "GPU Grass Pass");
    cmd.beginRendering(renderingInfo);
    cmd.setViewportWithCount({ viewport });
    cmd.setScissorWithCount({ renderArea });

    const GrassConstants grassConstants{
        .viewProjMatrix = s_Data->SceneUniformData.projection * s_Data->SceneUniformData.view,
        .time = s_Data->SceneUniformData.time,
    };
    s_Data->GrassSystem.RecordDraw(cmd, frameIndex, grassConstants);

    cmd.endRendering();
    EndRenderPassDebugLabel(cmd);

    WriteGPUTimestamp(cmd, frameIndex, static_cast<uint32_t>(GPUTimestampQuery::GrassEnd));
}

void Renderer::ApplyTonemapping(const vk::raii::CommandBuffer& cmd, uint32_t frameIndex)
{
    WriteGPUTimestamp(cmd, frameIndex, static_cast<uint32_t>(GPUTimestampQuery::TonemappingPassBegin));

    const uint32_t outputWidth = static_cast<uint32_t>(s_Data->OutputSize.x);
    const uint32_t outputHeight = static_cast<uint32_t>(s_Data->OutputSize.y);
    if (outputWidth == 0 || outputHeight == 0) {
        WriteGPUTimestamp(cmd, frameIndex, static_cast<uint32_t>(GPUTimestampQuery::TonemappingPassEnd));
        return;
    }

    const vk::Rect2D renderArea{ .offset = vk::Offset2D{ .x = 0, .y = 0 },
                                 .extent = vk::Extent2D{ .width = outputWidth, .height = outputHeight } };

    const vk::Viewport viewport{ .x = 0.0f,
                                 .y = 0.0f,
                                 .width = static_cast<float>(outputWidth),
                                 .height = static_cast<float>(outputHeight),
                                 .minDepth = 0.0f,
                                 .maxDepth = 1.0f };

    // Resolve -> Tonemapped
    {
        const vk::ImageMemoryBarrier2 resolveBarrier = { .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
                                                         .srcAccessMask = vk::AccessFlagBits2::eShaderRead,
                                                         .dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
                                                         .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
                                                         .oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
                                                         .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
                                                         .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                                         .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                                         .image = s_Data->ResolveImage.Image,
                                                         .subresourceRange = { .aspectMask =
                                                                                   vk::ImageAspectFlagBits::eColor,
                                                                               .baseMipLevel = 0,
                                                                               .levelCount = 1,
                                                                               .baseArrayLayer = 0,
                                                                               .layerCount = 1 } };

        const vk::ImageMemoryBarrier2 tonemappedBarrier = {
            .srcStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
            .srcAccessMask = vk::AccessFlagBits2::eShaderRead,
            .dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            .dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = s_Data->TonemappedImage.Image,
            .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eColor,
                                  .baseMipLevel = 0,
                                  .levelCount = 1,
                                  .baseArrayLayer = 0,
                                  .layerCount = 1 }
        };

        const std::array barriers = { resolveBarrier, tonemappedBarrier };

        const vk::DependencyInfo dependencyInfo = { .dependencyFlags = {},
                                                    .imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size()),
                                                    .pImageMemoryBarriers = barriers.data() };

        cmd.pipelineBarrier2(dependencyInfo);
    }

    vk::RenderingAttachmentInfo tonemappedAttachmentInfo{ .imageView = s_Data->TonemappedImage.ImageView,
                                                          .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
                                                          .loadOp = vk::AttachmentLoadOp::eClear,
                                                          .storeOp = vk::AttachmentStoreOp::eStore,
                                                          .clearValue = vk::ClearColorValue{
                                                              std::array{ 0.0f, 0.0f, 0.0f, 1.0f } } };
    const vk::RenderingInfo tonemappedRenderingInfo{ .renderArea = renderArea,
                                                     .layerCount = 1,
                                                     .colorAttachmentCount = 1,
                                                     .pColorAttachments = &tonemappedAttachmentInfo,
                                                     .pDepthAttachment = nullptr };

    BeginRenderPassDebugLabel(cmd, "Tonemapping Resolve Pass");
    cmd.beginRendering(tonemappedRenderingInfo);
    cmd.setViewport(0, viewport);
    cmd.setScissor(0, renderArea);

    s_Data->TonemappingResolvePipeline->Bind(cmd);

    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                           *s_Data->TonemappingResolvePipelineLayout,
                           0,
                           *s_Data->DescriptorSets[frameIndex].tonemappingResolve,
                           {});

    TonemappingResolvePushConstants tonemappingConstants;
    tonemappingConstants.inverseScreenSize = glm::vec2(1.0f) / s_Data->OutputSize;
    tonemappingConstants.bloomIntensity = s_Data->Bloom.Intensity;
    tonemappingConstants.tonemapOperator = static_cast<float>(s_Data->TonemappingOperator);
    tonemappingConstants.needsLuma = (s_Data->AntiAliasingMode == AntiAliasingMode::FXAA) ? 1u : 0u;

    cmd.pushConstants<TonemappingResolvePushConstants>(
        *s_Data->TonemappingResolvePipelineLayout, vk::ShaderStageFlagBits::eFragment, 0, { tonemappingConstants });

    cmd.draw(3, 1, 0, 0);

    cmd.endRendering();
    EndRenderPassDebugLabel(cmd);

    WriteGPUTimestamp(cmd, frameIndex, static_cast<uint32_t>(GPUTimestampQuery::TonemappingPassEnd));
}

void Renderer::ApplyAntiAliasing(const vk::raii::CommandBuffer& cmd, const uint32_t frameIndex)
{
    WriteGPUTimestamp(cmd, frameIndex, static_cast<uint32_t>(GPUTimestampQuery::AntialiasingPassBegin));

    {
        const vk::ImageMemoryBarrier2 tonemappedBarrier = {
            .srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            .srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
            .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
            .oldLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = s_Data->TonemappedImage.Image,
            .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eColor,
                                  .baseMipLevel = 0,
                                  .levelCount = 1,
                                  .baseArrayLayer = 0,
                                  .layerCount = 1 }
        };

        const vk::ImageMemoryBarrier2 compositeImageBarrier = {
            .srcStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
            .srcAccessMask = vk::AccessFlagBits2::eShaderRead,
            .dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            .dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
            .oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
            .newLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = s_Data->CompositeImage.Image,
            .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eColor,
                                  .baseMipLevel = 0,
                                  .levelCount = 1,
                                  .baseArrayLayer = 0,
                                  .layerCount = 1 }
        };

        const std::array barriers = { tonemappedBarrier, compositeImageBarrier };

        const vk::DependencyInfo dependencyInfo = { .dependencyFlags = {},
                                                    .imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size()),
                                                    .pImageMemoryBarriers = barriers.data() };

        cmd.pipelineBarrier2(dependencyInfo);
    }

    const uint32_t outputWidth = static_cast<uint32_t>(s_Data->OutputSize.x);
    const uint32_t outputHeight = static_cast<uint32_t>(s_Data->OutputSize.y);

    const vk::Rect2D renderArea{ .offset = vk::Offset2D{ .x = 0, .y = 0 },
                                 .extent = vk::Extent2D{ .width = outputWidth, .height = outputHeight } };

    const vk::Viewport viewport{ .x = 0.0f,
                                 .y = 0.0f,
                                 .width = static_cast<float>(outputWidth),
                                 .height = static_cast<float>(outputHeight),
                                 .minDepth = 0.0f,
                                 .maxDepth = 1.0f };

    BeginRenderPassDebugLabel(cmd, "Antialiasing Pass");

    if (s_Data->AntiAliasingMode == AntiAliasingMode::None) {
        ApplyNoOpPostProcessing(cmd, frameIndex, renderArea, viewport);
    }
    else if (s_Data->AntiAliasingMode == AntiAliasingMode::FXAA) {
        ApplyFXAA(cmd, frameIndex, renderArea, viewport);
    }
    else if (s_Data->AntiAliasingMode == AntiAliasingMode::SMAA) {
        ApplySMAA(cmd, frameIndex, renderArea, viewport);
    }
    else if (s_Data->AntiAliasingMode == AntiAliasingMode::TAA) {
    }

    EndRenderPassDebugLabel(cmd);

    WriteGPUTimestamp(cmd, frameIndex, static_cast<uint32_t>(GPUTimestampQuery::AntialiasingPassEnd));
}

void Renderer::ApplyFXAA(const vk::raii::CommandBuffer& cmd,
                         const uint32_t frameIndex,
                         const vk::Rect2D& renderArea,
                         const vk::Viewport& viewport)
{
    vk::RenderingAttachmentInfo compositeAttachmentInfo{ .imageView = s_Data->CompositeImage.ImageView,
                                                         .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
                                                         .loadOp = vk::AttachmentLoadOp::eClear,
                                                         .storeOp = vk::AttachmentStoreOp::eStore,
                                                         .clearValue = vk::ClearColorValue{
                                                             std::array{ 0.0f, 0.0f, 0.0f, 1.0f } } };
    const vk::RenderingInfo compositeRenderingInfo{ .renderArea = renderArea,
                                                    .layerCount = 1,
                                                    .colorAttachmentCount = 1,
                                                    .pColorAttachments = &compositeAttachmentInfo,
                                                    .pDepthAttachment = nullptr };

    cmd.beginRendering(compositeRenderingInfo);
    cmd.setViewport(0, viewport);
    cmd.setScissor(0, renderArea);

    FXAAPushConstants fxaaConstants;
    fxaaConstants.inverseViewportSize = glm::vec2(1.0f) / s_Data->OutputSize;

    s_Data->FXAAPipeline->Bind(cmd);

    cmd.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics, *s_Data->FXAAPipelineLayout, 0, *s_Data->DescriptorSets[frameIndex].fxaa, {});

    cmd.pushConstants<FXAAPushConstants>(
        *s_Data->FXAAPipelineLayout, vk::ShaderStageFlagBits::eFragment, 0, { fxaaConstants });

    cmd.draw(3, 1, 0, 0);

    cmd.endRendering();
}

void Renderer::ApplyNoOpPostProcessing(const vk::raii::CommandBuffer& cmd,
                                       const uint32_t frameIndex,
                                       const vk::Rect2D& renderArea,
                                       const vk::Viewport& viewport)
{
    vk::RenderingAttachmentInfo compositeAttachmentInfo{ .imageView = s_Data->CompositeImage.ImageView,
                                                         .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
                                                         .loadOp = vk::AttachmentLoadOp::eClear,
                                                         .storeOp = vk::AttachmentStoreOp::eStore,
                                                         .clearValue = vk::ClearColorValue{
                                                             std::array{ 0.0f, 0.0f, 0.0f, 1.0f } } };
    const vk::RenderingInfo compositeRenderingInfo{ .renderArea = renderArea,
                                                    .layerCount = 1,
                                                    .colorAttachmentCount = 1,
                                                    .pColorAttachments = &compositeAttachmentInfo,
                                                    .pDepthAttachment = nullptr };

    cmd.beginRendering(compositeRenderingInfo);
    cmd.setViewport(0, viewport);
    cmd.setScissor(0, renderArea);

    FXAAPushConstants fxaaConstants;
    fxaaConstants.inverseViewportSize = glm::vec2(1.0f) / s_Data->OutputSize;

    s_Data->NoopPostProcessPipeline->Bind(cmd);

    cmd.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics, *s_Data->FXAAPipelineLayout, 0, *s_Data->DescriptorSets[frameIndex].fxaa, {});

    cmd.pushConstants<FXAAPushConstants>(
        *s_Data->FXAAPipelineLayout, vk::ShaderStageFlagBits::eFragment, 0, { fxaaConstants });

    cmd.draw(3, 1, 0, 0);

    cmd.endRendering();
}

void Renderer::ApplySMAA(const vk::raii::CommandBuffer& cmd,
                         const uint32_t frameIndex,
                         const vk::Rect2D& renderArea,
                         const vk::Viewport& viewport)
{
    // Tonemapped image is already shader read-only optimal and composite image is already color attachment optimal

    SMAAData::PushConstants smaaPushConstants;
    smaaPushConstants.InverseViewportSize = glm::vec2(1.0f) / s_Data->OutputSize;
    smaaPushConstants.ViewportSize = s_Data->OutputSize;

    // Transfer edges image to color attachment optimal for the edge detection pass
    {
        const vk::ImageMemoryBarrier2 edgeImageBarrier = {
            .srcStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
            .srcAccessMask = vk::AccessFlagBits2::eShaderRead,
            .dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            .dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = s_Data->SMAAResources.EdgesImage.Image,
            .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eColor,
                                  .baseMipLevel = 0,
                                  .levelCount = 1,
                                  .baseArrayLayer = 0,
                                  .layerCount = 1 }
        };
        const std::array barriers = { edgeImageBarrier };
        cmd.pipelineBarrier2({ .imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size()),
                               .pImageMemoryBarriers = barriers.data() });
    }

    // Edge Detection Pass
    {
        vk::RenderingAttachmentInfo edgeDetectionAttachmentInfo{
            .imageView = s_Data->SMAAResources.EdgesImage.ImageView,
            .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .loadOp = vk::AttachmentLoadOp::eClear,
            .storeOp = vk::AttachmentStoreOp::eStore,
            .clearValue = vk::ClearColorValue{ std::array{ 0.0f, 0.0f, 0.0f, 0.0f } }
        };
        const vk::RenderingInfo edgeDetectionRenderingInfo{ .renderArea = renderArea,
                                                            .layerCount = 1,
                                                            .colorAttachmentCount = 1,
                                                            .pColorAttachments = &edgeDetectionAttachmentInfo,
                                                            .pDepthAttachment = nullptr };

        BeginRenderPassDebugLabel(cmd, "SMAA Edge Detection Pass");

        cmd.beginRendering(edgeDetectionRenderingInfo);
        cmd.setViewport(0, viewport);
        cmd.setScissor(0, renderArea);

        s_Data->SMAAResources.EdgeDetectionPipeline->Bind(cmd);

        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                               *s_Data->SMAAResources.EdgeDetectionPipelineLayout,
                               0,
                               *s_Data->SMAAResources.DescriptorSets[frameIndex].EdgeDetection,
                               {});

        cmd.pushConstants<SMAAData::PushConstants>(*s_Data->SMAAResources.EdgeDetectionPipelineLayout,
                                                   vk::ShaderStageFlagBits::eFragment |
                                                       vk::ShaderStageFlagBits::eVertex,
                                                   0,
                                                   { smaaPushConstants });

        cmd.draw(3, 1, 0, 0);

        cmd.endRendering();

        EndRenderPassDebugLabel(cmd);
    }

    // Transition EdgesImage to shader read-only optimal and BlendImage to color attachment optimal for the next pass
    {
        const vk::ImageMemoryBarrier2 edgeImageBarrier = {
            .srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            .srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
            .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = s_Data->SMAAResources.EdgesImage.Image,
            .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eColor,
                                  .baseMipLevel = 0,
                                  .levelCount = 1,
                                  .baseArrayLayer = 0,
                                  .layerCount = 1 }
        };
        const vk::ImageMemoryBarrier2 blendImageBarrier = {
            .srcStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
            .srcAccessMask = vk::AccessFlagBits2::eShaderRead,
            .dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            .dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = s_Data->SMAAResources.BlendImage.Image,
            .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eColor,
                                  .baseMipLevel = 0,
                                  .levelCount = 1,
                                  .baseArrayLayer = 0,
                                  .layerCount = 1 }
        };
        const std::array barriers = { edgeImageBarrier, blendImageBarrier };

        cmd.pipelineBarrier2({ .imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size()),
                               .pImageMemoryBarriers = barriers.data() });
    }

    // Blend Weights Calculation Pass
    {
        vk::RenderingAttachmentInfo blendWeightAttachmentInfo{ .imageView = s_Data->SMAAResources.BlendImage.ImageView,
                                                               .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
                                                               .loadOp = vk::AttachmentLoadOp::eClear,
                                                               .storeOp = vk::AttachmentStoreOp::eStore,
                                                               .clearValue = vk::ClearColorValue{
                                                                   std::array{ 0.0f, 0.0f, 0.0f, 0.0f } } };
        const vk::RenderingInfo blendWeightRenderingInfo{ .renderArea = renderArea,
                                                          .layerCount = 1,
                                                          .colorAttachmentCount = 1,
                                                          .pColorAttachments = &blendWeightAttachmentInfo,
                                                          .pDepthAttachment = nullptr };

        BeginRenderPassDebugLabel(cmd, "SMAA Blend Weights Pass");

        cmd.beginRendering(blendWeightRenderingInfo);

        cmd.setViewport(0, viewport);
        cmd.setScissor(0, renderArea);

        s_Data->SMAAResources.BlendWeightPipeline->Bind(cmd);

        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                               *s_Data->SMAAResources.BlendWeightPipelineLayout,
                               0,
                               *s_Data->SMAAResources.DescriptorSets[frameIndex].BlendWeight,
                               {});

        cmd.pushConstants<SMAAData::PushConstants>(*s_Data->SMAAResources.BlendWeightPipelineLayout,
                                                   vk::ShaderStageFlagBits::eFragment |
                                                       vk::ShaderStageFlagBits::eVertex,
                                                   0,
                                                   { smaaPushConstants });

        cmd.draw(3, 1, 0, 0);

        cmd.endRendering();

        EndRenderPassDebugLabel(cmd);
    }

    // Transition BlendImage to shader read-only optimal for the next pass
    {
        const vk::ImageMemoryBarrier2 blendImageBarrier = {
            .srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            .srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
            .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
            .oldLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = s_Data->SMAAResources.BlendImage.Image,
            .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eColor,
                                  .baseMipLevel = 0,
                                  .levelCount = 1,
                                  .baseArrayLayer = 0,
                                  .layerCount = 1 }
        };
        cmd.pipelineBarrier2({ .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &blendImageBarrier });
    }

    // Neighborhood Blending Pass
    {
        vk::RenderingAttachmentInfo neighbourhoodBlendRenderingInfo{
            .imageView = s_Data->CompositeImage.ImageView,
            .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .loadOp = vk::AttachmentLoadOp::eClear,
            .storeOp = vk::AttachmentStoreOp::eStore,
            .clearValue = vk::ClearColorValue{ std::array{ 0.0f, 0.0f, 0.0f, 0.0f } }
        };
        const vk::RenderingInfo blendWeightRenderingInfo{ .renderArea = renderArea,
                                                          .layerCount = 1,
                                                          .colorAttachmentCount = 1,
                                                          .pColorAttachments = &neighbourhoodBlendRenderingInfo,
                                                          .pDepthAttachment = nullptr };

        BeginRenderPassDebugLabel(cmd, "SMAA Neighbourhood Blend Pass");

        cmd.beginRendering(blendWeightRenderingInfo);

        cmd.setViewport(0, viewport);
        cmd.setScissor(0, renderArea);

        s_Data->SMAAResources.NeighborhoodBlendingPipeline->Bind(cmd);

        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                               *s_Data->SMAAResources.NeighborhoodBlendingPipelineLayout,
                               0,
                               *s_Data->SMAAResources.DescriptorSets[frameIndex].NeighbourhoodBlend,
                               {});

        cmd.pushConstants<SMAAData::PushConstants>(*s_Data->SMAAResources.NeighborhoodBlendingPipelineLayout,
                                                   vk::ShaderStageFlagBits::eFragment |
                                                       vk::ShaderStageFlagBits::eVertex,
                                                   0,
                                                   { smaaPushConstants });

        cmd.draw(3, 1, 0, 0);

        cmd.endRendering();

        EndRenderPassDebugLabel(cmd);
    }
}

void Renderer::ApplyBloom(const vk::raii::CommandBuffer& cmd, const uint32_t frameIndex)
{
    WriteGPUTimestamp(cmd, frameIndex, static_cast<uint32_t>(GPUTimestampQuery::BloomPassBegin));

    {
        const vk::ImageMemoryBarrier2 resolveBarrier = {
            .srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            .srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
            .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
            .oldLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = *s_Data->ResolveImage.Image,
            .subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 }
        };

        const vk::ImageMemoryBarrier2 bloomBarrier = {
            .srcStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
            .srcAccessMask = vk::AccessFlagBits2::eShaderRead,
            .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
            .dstAccessMask = vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderStorageWrite,
            .oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
            .newLayout = vk::ImageLayout::eGeneral,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = *s_Data->Bloom.Image,
            .subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, s_Data->Bloom.MipLevels, 0, 1 }
        };

        const std::array initialBarriers = { resolveBarrier, bloomBarrier };
        const vk::DependencyInfo initialDependencyInfo = { .dependencyFlags = {},
                                                           .imageMemoryBarrierCount =
                                                               static_cast<uint32_t>(initialBarriers.size()),
                                                           .pImageMemoryBarriers = initialBarriers.data() };
        cmd.pipelineBarrier2(initialDependencyInfo);
    }

    auto computeBarrier = [&]() {
        constexpr vk::MemoryBarrier2 memoryBarrier = { .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
                                                       .srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
                                                       .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
                                                       .dstAccessMask = vk::AccessFlagBits2::eShaderRead |
                                                                        vk::AccessFlagBits2::eShaderStorageWrite };
        vk::DependencyInfo depInfo = { .dependencyFlags = {},
                                       .memoryBarrierCount = 1,
                                       .pMemoryBarriers = &memoryBarrier };
        cmd.pipelineBarrier2(depInfo);
    };

    constexpr uint32_t groupSize = 8;
    const uint32_t mipCount = s_Data->Bloom.MipLevels;

    BeginRenderPassDebugLabel(cmd, "Bloom Downsample Pass");

    s_Data->Bloom.DownsamplePipeline->Bind(cmd);
    {
        // Main Image and Bloom Mip 0
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                               *s_Data->Bloom.DownsamplePipelineLayout,
                               0,
                               { s_Data->Bloom.ExtractSet },
                               {});

        BloomData::DownsamplePushConstants pushConstants;
        pushConstants.srcTexelSize = 1.0f / s_Data->OutputSize;
        pushConstants.enablePrefilter = (s_Data->Bloom.Mode == BloomMode::BrightPassPrefilter) ? 1u : 0u;
        pushConstants.threshold = std::max(s_Data->Bloom.Threshold, 0.0f);
        pushConstants.knee = std::clamp(s_Data->Bloom.Knee, 0.0f, pushConstants.threshold + 1.0f);
        pushConstants.isExtract = 1;
        pushConstants.maxBrightness = s_Data->Bloom.MaxBrightness;
        cmd.pushConstants<BloomData::DownsamplePushConstants>(
            *s_Data->Bloom.DownsamplePipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, { pushConstants });

        const uint32_t groupX = (static_cast<uint32_t>(s_Data->Bloom.MipSizes[0].x) + groupSize - 1) / groupSize;
        const uint32_t groupY = (static_cast<uint32_t>(s_Data->Bloom.MipSizes[0].y) + groupSize - 1) / groupSize;

        cmd.dispatch(groupX, groupY, 1);
    }

    for (uint32_t mip = 0; mip < mipCount - 1; ++mip) {
        computeBarrier();

        // BloomMipView[i]_SRV, BloomMipView[i+1]_UAV
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                               *s_Data->Bloom.DownsamplePipelineLayout,
                               0,
                               { s_Data->Bloom.DownsampleSets[mip] },
                               {});

        BloomData::DownsamplePushConstants pushConstants;
        pushConstants.srcTexelSize = 1.0f / s_Data->Bloom.MipSizes[mip];
        pushConstants.enablePrefilter = 0u;
        pushConstants.threshold = 0.0f;
        pushConstants.knee = 0.0f;
        pushConstants.isExtract = 0;
        cmd.pushConstants<BloomData::DownsamplePushConstants>(
            *s_Data->Bloom.DownsamplePipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, { pushConstants });

        const uint32_t groupX = (static_cast<uint32_t>(s_Data->Bloom.MipSizes[mip + 1].x) + groupSize - 1) / groupSize;
        const uint32_t groupY = (static_cast<uint32_t>(s_Data->Bloom.MipSizes[mip + 1].y) + groupSize - 1) / groupSize;
        cmd.dispatch(groupX, groupY, 1);
    }

    EndRenderPassDebugLabel(cmd);

    BeginRenderPassDebugLabel(cmd, "Bloom Upsample Pass");

    s_Data->Bloom.UpsamplePipeline->Bind(cmd);

    BloomData::UpsamplePushConstants pushConstants;
    pushConstants.filterRadius = s_Data->Bloom.FilterRadius;

    for (uint32_t mip = mipCount - 1; mip > 0; --mip) {
        computeBarrier();

        // BloomMipView[i]_SRV, BloomMipView[i-1]_UAV
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                               *s_Data->Bloom.UpsamplePipelineLayout,
                               0,
                               { s_Data->Bloom.UpsampleSets[mip - 1] },
                               {});

        cmd.pushConstants<BloomData::UpsamplePushConstants>(
            *s_Data->Bloom.UpsamplePipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, { pushConstants });

        const uint32_t groupX = (static_cast<uint32_t>(s_Data->Bloom.MipSizes[mip - 1].x) + groupSize - 1) / groupSize;
        const uint32_t groupY = (static_cast<uint32_t>(s_Data->Bloom.MipSizes[mip - 1].y) + groupSize - 1) / groupSize;
        cmd.dispatch(groupX, groupY, 1);
    }

    EndRenderPassDebugLabel(cmd);

    const vk::ImageMemoryBarrier2 bloomFinalBarrier = {
        .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
        .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
        .oldLayout = vk::ImageLayout::eGeneral,
        .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = *s_Data->Bloom.Image,
        .subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, s_Data->Bloom.MipLevels, 0, 1 }
    };
    const vk::DependencyInfo finalDependencyInfo = { .dependencyFlags = {},
                                                     .imageMemoryBarrierCount = 1,
                                                     .pImageMemoryBarriers = &bloomFinalBarrier };
    cmd.pipelineBarrier2(finalDependencyInfo);

    WriteGPUTimestamp(cmd, frameIndex, static_cast<uint32_t>(GPUTimestampQuery::BloomPassEnd));
}

glm::mat4 Renderer::CalculateLightSpaceMatrix()
{
    constexpr float nearPlane = 0.1f;
    constexpr float farPlane = 100.0f;
    constexpr float orthoSize = 20.0f;
    glm::mat4 lightProjection = glm::ortho(-orthoSize, orthoSize, -orthoSize, orthoSize, nearPlane, farPlane);
    lightProjection[1][1] *= -1.0f;

    constexpr glm::vec3 sceneCenter = glm::vec3(0.0f, 0.0f, 0.0f);
    constexpr float lightDistance = 80.0f;

    /*const glm::vec3 lightDirRaw = glm::vec3(m_UniformDataParams.lights[0]);
    const glm::vec3 lightDir = glm::length2(lightDirRaw) > std::numeric_limits<float>::epsilon()
        ? glm::normalize(lightDirRaw)
        : glm::vec3(0.0f, 1.0f, 0.0f);*/

    const glm::vec3 lightDir = glm::normalize(glm::vec3(s_Data->GlobalLightingData.sunLight));

    const glm::vec3 lightPos = sceneCenter + lightDir * lightDistance;
    s_Data->ShadowMap.LightPosForCalculation = lightPos;
    constexpr glm::vec3 lightTarget = sceneCenter; /// Look at origin

    glm::vec3 lightUp = glm::vec3(0.0f, 1.0f, 0.0f);
    if (glm::abs(glm::dot(lightDir, lightUp)) > 0.99f) {
        lightUp = glm::vec3(1.0f, 0.0f, 0.0f);
    }

    const glm::mat4 lightView = glm::lookAt(lightPos, lightTarget, lightUp);

    // Correction matrix for Vulkan Clip Space
    // Y: -1 (flip logic), Z: 0.5 scale + 0.5 offset ([-1,1] -> [0,1])
    // constexpr glm::mat4 correction = glm::mat4(
    //	1.0f, 0.0f, 0.0f, 0.0f,
    //	0.0f, -1.0f, 0.0f, 0.0f,
    //	0.0f, 0.0f, 0.5f, 0.0f,
    //	0.0f, 0.0f, 0.5f, 1.0f);

    return lightProjection * lightView;
}

void Renderer::HandleMousePickingReadback(const vk::raii::CommandBuffer& cmd)
{
    const auto& device = VulkanContext::Get().GetDevice();
    const uint64_t completedTimelineValue =
        s_Data->MousePickingReadback.TimelineSemaphore != nullptr
            ? static_cast<vk::Device>(device).getSemaphoreCounterValue(s_Data->MousePickingReadback.TimelineSemaphore)
            : 0;

    for (auto& readSlot : s_Data->MousePickingReadback.Slots) {
        if (readSlot.Pending && completedTimelineValue >= readSlot.TimelineValue) {
            const auto pickedEntity = *static_cast<const uint32_t*>(readSlot.MappedData);
            s_Data->MousePickingReadback.LatestEntityID = pickedEntity;
            readSlot.Pending = false;
        }
    }

    if (s_Data->MousePickingReadback.RequestPending) {
        auto& writeSlot = s_Data->MousePickingReadback.Slots[s_Data->MousePickingReadback.WriteIndex];

        vk::ImageMemoryBarrier2 toTransferSrcBarrier = {
            .srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            .srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
            .dstAccessMask = vk::AccessFlagBits2::eTransferRead,
            .oldLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .newLayout = vk::ImageLayout::eTransferSrcOptimal,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = s_Data->PickingImage.Image,
            .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eColor,
                                  .baseMipLevel = 0,
                                  .levelCount = 1,
                                  .baseArrayLayer = 0,
                                  .layerCount = 1 }
        };

        const vk::DependencyInfo toTransferDependencyInfo = { .dependencyFlags = {},
                                                              .imageMemoryBarrierCount = 1,
                                                              .pImageMemoryBarriers = &toTransferSrcBarrier };
        cmd.pipelineBarrier2(toTransferDependencyInfo);
        s_Data->PickingImageLayout = vk::ImageLayout::eTransferSrcOptimal;

        const uint32_t outputWidth = static_cast<uint32_t>(s_Data->OutputSize.x);
        const uint32_t outputHeight = static_cast<uint32_t>(s_Data->OutputSize.y);

        if (outputWidth == 0 || outputHeight == 0 || s_Data->MousePickingReadback.RequestedPixel.x >= outputWidth ||
            s_Data->MousePickingReadback.RequestedPixel.y >= outputHeight) {
            s_Data->MousePickingReadback.RequestPending = false;
            s_Data->MousePickingReadback.LatestEntityID = std::numeric_limits<uint32_t>::max();
            return;
        }

        const vk::BufferImageCopy copyRegion{
            .bufferOffset = 0,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource = { .aspectMask = vk::ImageAspectFlagBits::eColor,
                                  .mipLevel = 0,
                                  .baseArrayLayer = 0,
                                  .layerCount = 1 },
            .imageOffset = { .x = static_cast<int32_t>(s_Data->MousePickingReadback.RequestedPixel.x),
                             .y = static_cast<int32_t>(s_Data->MousePickingReadback.RequestedPixel.y),
                             .z = 0 },
            .imageExtent = { .width = 1, .height = 1, .depth = 1 }
        };

        cmd.copyImageToBuffer(
            s_Data->PickingImage.Image, vk::ImageLayout::eTransferSrcOptimal, writeSlot.Buffer, copyRegion);

        writeSlot.Pending = true;
        writeSlot.TimelineValue = ++s_Data->MousePickingReadback.TimelineValue;
        s_Data->MousePickingReadback.PendingTimelineSignalValue = writeSlot.TimelineValue;
        s_Data->MousePickingReadback.WriteIndex =
            (s_Data->MousePickingReadback.WriteIndex + 1) % Renderer::MousePickingReadbackFrameLag;
        s_Data->MousePickingReadback.RequestPending = false;
    }
}

bool Renderer::IsUsingAccelerationStructures()
{
    return s_Data->UseRayQueryBasedShadows || s_Data->UseRayQueryBasedSoftShadows;
}

void Renderer::PrepareUniformBuffers()
{
    const auto& context = VulkanContext::Get();
    const auto properties = context.GetProperties().properties;

    s_Data->MinUniformBufferOffsetAlignment = properties.limits.minUniformBufferOffsetAlignment;

    s_Data->DynamicAlignment = sizeof(PerObjectData);
    if (s_Data->MinUniformBufferOffsetAlignment > 0) {
        s_Data->DynamicAlignment = (s_Data->DynamicAlignment + s_Data->MinUniformBufferOffsetAlignment - 1) &
                                   ~(s_Data->MinUniformBufferOffsetAlignment - 1);
    }

    for (auto& [scene, globalLighting, perObject, skybox, gtao] : s_Data->UniformBuffers) {
        scene = CreateRef<UniformBuffer>(sizeof(SceneUniformData));

        globalLighting = CreateRef<UniformBuffer>(sizeof(GlobalLighting));

        // TODO: Allocate a large enough buffer for a maximum number of objects, and allocate a bigger one if needed.
        constexpr size_t maxObjects = 1000;
        perObject = CreateRef<UniformBuffer>(s_Data->DynamicAlignment * maxObjects);

        skybox = CreateRef<UniformBuffer>(sizeof(SkyboxData));

        gtao = CreateRef<UniformBuffer>(sizeof(GTAOConstants));
    }
}

void Renderer::PrepareStorageBuffers()
{
    for (auto& [lights] : s_Data->StorageBuffers) {
        constexpr uint32_t lightBufferSize = sizeof(GPULight) * MaxLights;
        lights = CreateRef<StorageBuffer>(lightBufferSize);
    }
}

void Renderer::SetupDescriptors()
{
    auto& context = VulkanContext::Get();
    const auto& device = context.GetDevice();

    std::vector<vk::DescriptorPoolSize> poolSizes = {
        vk::DescriptorPoolSize{ .type = vk::DescriptorType::eUniformBuffer, .descriptorCount = 10 },
        vk::DescriptorPoolSize{ .type = vk::DescriptorType::eUniformBufferDynamic, .descriptorCount = 10 },
        vk::DescriptorPoolSize{ .type = vk::DescriptorType::eCombinedImageSampler, .descriptorCount = 100 },
        vk::DescriptorPoolSize{ .type = vk::DescriptorType::eAccelerationStructureKHR, .descriptorCount = 2 },
        vk::DescriptorPoolSize{ .type = vk::DescriptorType::eStorageBuffer, .descriptorCount = 10 },
        vk::DescriptorPoolSize{ .type = vk::DescriptorType::eStorageImage, .descriptorCount = 20 },
        vk::DescriptorPoolSize{ .type = vk::DescriptorType::eSampledImage, .descriptorCount = 20 },
        vk::DescriptorPoolSize{ .type = vk::DescriptorType::eSampler, .descriptorCount = 10 },
    };

    vk::DescriptorPoolCreateInfo poolInfo{ .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
                                           .maxSets = 200,
                                           .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
                                           .pPoolSizes = poolSizes.data() };

    s_Data->DescriptorPool = vk::raii::DescriptorPool{ device, poolInfo };
    context.SetObjectDebugName(reinterpret_cast<uint64_t>(static_cast<VkDescriptorPool>(*s_Data->DescriptorPool)),
                               vk::ObjectType::eDescriptorPool,
                               "PBR Descriptor Pool");

    std::vector<vk::DescriptorSetLayoutBinding> bindings = {
        vk::DescriptorSetLayoutBinding{ // Scene data
                                        .binding = 0,
                                        .descriptorType = vk::DescriptorType::eUniformBuffer,
                                        .descriptorCount = 1,
                                        .stageFlags =
                                            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment |
                                            vk::ShaderStageFlagBits::eGeometry | vk::ShaderStageFlagBits::eCompute,
                                        .pImmutableSamplers = nullptr },
        vk::DescriptorSetLayoutBinding{ // Light data and other params
                                        .binding = 1,
                                        .descriptorType = vk::DescriptorType::eUniformBuffer,
                                        .descriptorCount = 1,
                                        .stageFlags = vk::ShaderStageFlagBits::eFragment,
                                        .pImmutableSamplers = nullptr },
        vk::DescriptorSetLayoutBinding{ // Per-object data
                                        .binding = 2,
                                        .descriptorType = vk::DescriptorType::eUniformBufferDynamic,
                                        .descriptorCount = 1,
                                        .stageFlags = vk::ShaderStageFlagBits::eVertex |
                                                      vk::ShaderStageFlagBits::eFragment |
                                                      vk::ShaderStageFlagBits::eGeometry,
                                        .pImmutableSamplers = nullptr },
        vk::DescriptorSetLayoutBinding{ // Shadow map
                                        .binding = 3,
                                        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                        .descriptorCount = 1,
                                        .stageFlags = vk::ShaderStageFlagBits::eFragment,
                                        .pImmutableSamplers = nullptr },
        vk::DescriptorSetLayoutBinding{ // Irradiance map
                                        .binding = 4,
                                        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                        .descriptorCount = 1,
                                        .stageFlags = vk::ShaderStageFlagBits::eFragment,
                                        .pImmutableSamplers = nullptr },
        vk::DescriptorSetLayoutBinding{ // BRDF LUT
                                        .binding = 5,
                                        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                        .descriptorCount = 1,
                                        .stageFlags = vk::ShaderStageFlagBits::eFragment,
                                        .pImmutableSamplers = nullptr },
        vk::DescriptorSetLayoutBinding{ // Prefiltered environment map
                                        .binding = 6,
                                        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                        .descriptorCount = 1,
                                        .stageFlags = vk::ShaderStageFlagBits::eFragment,
                                        .pImmutableSamplers = nullptr },
        vk::DescriptorSetLayoutBinding{ // TLAS
                                        .binding = 7,
                                        .descriptorType = vk::DescriptorType::eAccelerationStructureKHR,
                                        .descriptorCount = 1,
                                        .stageFlags = vk::ShaderStageFlagBits::eFragment,
                                        .pImmutableSamplers = nullptr },
        vk::DescriptorSetLayoutBinding{ // Light storage buffer
                                        .binding = 8,
                                        .descriptorType = vk::DescriptorType::eStorageBuffer,
                                        .descriptorCount = 1,
                                        .stageFlags = vk::ShaderStageFlagBits::eFragment,
                                        .pImmutableSamplers = nullptr },
        vk::DescriptorSetLayoutBinding{ // Global AO texture
                                        .binding = 9,
                                        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                        .descriptorCount = 1,
                                        .stageFlags = vk::ShaderStageFlagBits::eFragment,
                                        .pImmutableSamplers = nullptr },
    };

    const vk::DescriptorSetLayoutCreateInfo layoutInfo{ .bindingCount = static_cast<uint32_t>(bindings.size()),
                                                        .pBindings = bindings.data() };

    s_Data->DescriptorSetLayouts.scene = vk::raii::DescriptorSetLayout{ device, layoutInfo };
    context.SetObjectDebugName(s_Data->DescriptorSetLayouts.scene, "PBR Descriptor Set Layout");

    const std::vector<vk::DescriptorSetLayout> sceneSetLayouts(s_Data->DescriptorSets.size(),
                                                               *s_Data->DescriptorSetLayouts.scene);

    vk::DescriptorSetAllocateInfo allocInfo{ .descriptorPool = *s_Data->DescriptorPool,
                                             .descriptorSetCount = static_cast<uint32_t>(s_Data->DescriptorSets.size()),
                                             .pSetLayouts = sceneSetLayouts.data() };

    std::vector<vk::raii::DescriptorSet> sceneDescriptorSets = device.allocateDescriptorSets(allocInfo);
    std::vector<vk::raii::DescriptorSet> skyboxDescriptorSets = device.allocateDescriptorSets(allocInfo);
    for (uint32_t i = 0; i < s_Data->UniformBuffers.size(); i++) {
        s_Data->DescriptorSets[i].scene = std::move(sceneDescriptorSets[i]);
        context.SetObjectDebugName(s_Data->DescriptorSets[i].scene, "PBR Descriptor Set[" + std::to_string(i) + "]");

        const vk::DescriptorBufferInfo sceneBufferInfo{ .buffer = *s_Data->UniformBuffers[i].scene->GetBuffer(),
                                                        .offset = 0,
                                                        .range = sizeof(SceneUniformData) };

        const vk::DescriptorBufferInfo paramsBufferInfo{ .buffer =
                                                             *s_Data->UniformBuffers[i].globalLighting->GetBuffer(),
                                                         .offset = 0,
                                                         .range = sizeof(GlobalLighting) };

        const vk::DescriptorBufferInfo perObjectBufferInfo{ .buffer = *s_Data->UniformBuffers[i].perObject->GetBuffer(),
                                                            .offset = 0,
                                                            .range = sizeof(PerObjectData) };

        const vk::DescriptorImageInfo shadowMapImageInfo{ .sampler = *s_Data->ShadowMapSampler,
                                                          .imageView = *s_Data->ShadowMap.ReadImageView,
                                                          .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };

        const vk::DescriptorImageInfo skyboxImageInfo{ .sampler = *s_Data->Skybox.SkyboxTexture->GetSampler(),
                                                       .imageView = *s_Data->Skybox.SkyboxTexture->GetImageView(),
                                                       .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };

        const vk::DescriptorBufferInfo skyboxBufferInfo{ .buffer = *s_Data->UniformBuffers[i].skybox->GetBuffer(),
                                                         .offset = 0,
                                                         .range = sizeof(SkyboxData) };

        const vk::DescriptorBufferInfo lightStorageBufferInfo{ .buffer = *s_Data->StorageBuffers[i].Lights->GetBuffer(),
                                                               .offset = 0,
                                                               .range = sizeof(GPULight) * MaxLights };

        const vk::DescriptorImageInfo globalAOImageInfo{ .sampler = *s_Data->LinearSampler,
                                                         .imageView = *s_Data->GTAOImage.ImageView,
                                                         .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };

        const std::vector descriptorWrites = {
            vk::WriteDescriptorSet{ .dstSet = *s_Data->DescriptorSets[i].scene,
                                    .dstBinding = 0,
                                    .dstArrayElement = 0,
                                    .descriptorCount = 1,
                                    .descriptorType = vk::DescriptorType::eUniformBuffer,
                                    .pBufferInfo = &sceneBufferInfo },
            vk::WriteDescriptorSet{ .dstSet = *s_Data->DescriptorSets[i].scene,
                                    .dstBinding = 1,
                                    .dstArrayElement = 0,
                                    .descriptorCount = 1,
                                    .descriptorType = vk::DescriptorType::eUniformBuffer,
                                    .pBufferInfo = &paramsBufferInfo },
            vk::WriteDescriptorSet{ .dstSet = *s_Data->DescriptorSets[i].scene,
                                    .dstBinding = 2,
                                    .dstArrayElement = 0,
                                    .descriptorCount = 1,
                                    .descriptorType = vk::DescriptorType::eUniformBufferDynamic,
                                    .pBufferInfo = &perObjectBufferInfo },
            vk::WriteDescriptorSet{ .dstSet = *s_Data->DescriptorSets[i].scene,
                                    .dstBinding = 3,
                                    .dstArrayElement = 0,
                                    .descriptorCount = 1,
                                    .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                    .pImageInfo = &shadowMapImageInfo },
            vk::WriteDescriptorSet{ .dstSet = *s_Data->DescriptorSets[i].scene,
                                    .dstBinding = 4,
                                    .dstArrayElement = 0,
                                    .descriptorCount = 1,
                                    .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                    .pImageInfo = &s_Data->Skybox.IrradianceCubeTexture->GetDescriptorInfo() },
            vk::WriteDescriptorSet{ .dstSet = *s_Data->DescriptorSets[i].scene,
                                    .dstBinding = 5,
                                    .dstArrayElement = 0,
                                    .descriptorCount = 1,
                                    .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                    .pImageInfo = &s_Data->Skybox.LutBrdfTexture->GetDescriptorInfo() },
            vk::WriteDescriptorSet{ .dstSet = *s_Data->DescriptorSets[i].scene,
                                    .dstBinding = 6,
                                    .dstArrayElement = 0,
                                    .descriptorCount = 1,
                                    .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                    .pImageInfo = &s_Data->Skybox.PrefilteredCubeTexture->GetDescriptorInfo() },
            vk::WriteDescriptorSet{ .dstSet = *s_Data->DescriptorSets[i].scene,
                                    .dstBinding = 8,
                                    .dstArrayElement = 0,
                                    .descriptorCount = 1,
                                    .descriptorType = vk::DescriptorType::eStorageBuffer,
                                    .pBufferInfo = &lightStorageBufferInfo },
            vk::WriteDescriptorSet{ .dstSet = *s_Data->DescriptorSets[i].scene,
                                    .dstBinding = 9,
                                    .dstArrayElement = 0,
                                    .descriptorCount = 1,
                                    .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                    .pImageInfo = &globalAOImageInfo },
            // The TLAS descriptor will be updated later when the TLAS is built for the first time, since it requires a
            // valid acceleration structure handle.
        };

        device.updateDescriptorSets(descriptorWrites, {});

        s_Data->DescriptorSets[i].skybox = std::move(skyboxDescriptorSets[i]);
        context.SetObjectDebugName(s_Data->DescriptorSets[i].skybox,
                                   "Skybox Descriptor Set[" + std::to_string(i) + "]");

        const std::vector skyboxDescriptorWrites = {
            vk::WriteDescriptorSet{ .dstSet = *s_Data->DescriptorSets[i].skybox,
                                    .dstBinding = 0,
                                    .dstArrayElement = 0,
                                    .descriptorCount = 1,
                                    .descriptorType = vk::DescriptorType::eUniformBuffer,
                                    .pBufferInfo = &skyboxBufferInfo },
            vk::WriteDescriptorSet{ .dstSet = *s_Data->DescriptorSets[i].skybox,
                                    .dstBinding = 1,
                                    .dstArrayElement = 0,
                                    .descriptorCount = 1,
                                    .descriptorType = vk::DescriptorType::eUniformBuffer,
                                    .pBufferInfo = &paramsBufferInfo },
            vk::WriteDescriptorSet{ .dstSet = *s_Data->DescriptorSets[i].skybox,
                                    .dstBinding = 4,
                                    .dstArrayElement = 0,
                                    .descriptorCount = 1,
                                    .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                    .pImageInfo = &skyboxImageInfo }
        };

        device.updateDescriptorSets(skyboxDescriptorWrites, {});
    }
}

void Renderer::CreateSkyboxResources()
{
    KBRAssert(s_Data->Skybox.SkyboxTexture != nullptr,
                    "Skybox texture has to be set before creating skybox resources");

    // TODO: Set default skybox texture if none is set by the user

    if (s_Data->Skybox.SkyboxMesh == nullptr) {
        // The project has not been initialized this far
        // s_Data->Skybox.SkyboxMesh = AssetManager::GetDefaultCubeMesh();
        s_Data->Skybox.SkyboxMesh = CreateRef<Mesh>(ModelLoader::LoadModel("Assets/Models/cube.gltf", None));
    }
    if (s_Data->Skybox.LutBrdfTexture == nullptr) {
        s_Data->Skybox.LutBrdfTexture = CreateRef<Texture2D>();
        SkyboxUtils::GenerateBRDFLUT(*s_Data->Skybox.LutBrdfTexture);
    }

    s_Data->Skybox.IrradianceCubeTexture = CreateRef<TextureCube>();
    s_Data->Skybox.PrefilteredCubeTexture = CreateRef<TextureCube>();

    SkyboxUtils::GenerateIrradianceCube(
        *s_Data->Skybox.IrradianceCubeTexture, s_Data->Skybox.SkyboxTexture->descriptor, *s_Data->Skybox.SkyboxMesh);
    SkyboxUtils::GeneratePrefilteredEnvMap(
        *s_Data->Skybox.PrefilteredCubeTexture, s_Data->Skybox.SkyboxTexture->descriptor, *s_Data->Skybox.SkyboxMesh);
}

void Renderer::CreateTransparencyResources(const uint32_t width, const uint32_t height)
{
    KBRAssert(s_Data->Transparency.AccumulationImage.Format != vk::Format::eUndefined,
                    "Accumulation format has to be set before creating transparency resources");
    KBRAssert(s_Data->Transparency.RevealageImage.Format != vk::Format::eUndefined,
                    "Revealage format has to be set before creating transparency resources");
    KBRAssert(s_Data->Transparency.DistortionImage.Format != vk::Format::eUndefined,
                    "Distortion format has to be set before creating transparency resources");

    auto& context = VulkanContext::Get();
    const auto& device = context.GetDevice();

    constexpr uint32_t mipLevels = 1;

    // Accumulation

    CreateImage(device,
                width,
                height,
                mipLevels,
                vk::SampleCountFlagBits::e1,
                s_Data->Transparency.AccumulationImage.Format,
                vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
                vk::MemoryPropertyFlagBits::eDeviceLocal,
                s_Data->Transparency.AccumulationImage.Image,
                s_Data->Transparency.AccumulationImage.ImageMemory);

    context.SetObjectDebugName(
        reinterpret_cast<uint64_t>(static_cast<VkImage>(*s_Data->Transparency.AccumulationImage.Image)),
        vk::ObjectType::eImage,
        "Accumulation Image");

    context.SetObjectDebugName(
        reinterpret_cast<uint64_t>(static_cast<VkDeviceMemory>(*s_Data->Transparency.AccumulationImage.ImageMemory)),
        vk::ObjectType::eDeviceMemory,
        "Accumulation Image Memory");

    s_Data->Transparency.AccumulationImage.ImageView = CreateImageView(device,
                                                                       s_Data->Transparency.AccumulationImage.Image,
                                                                       s_Data->Transparency.AccumulationImage.Format,
                                                                       vk::ImageAspectFlagBits::eColor,
                                                                       mipLevels);

    context.SetObjectDebugName(
        reinterpret_cast<uint64_t>(static_cast<VkImageView>(*s_Data->Transparency.AccumulationImage.ImageView)),
        vk::ObjectType::eImageView,
        "Accumulation Image View");

    // Revealage

    CreateImage(device,
                width,
                height,
                mipLevels,
                vk::SampleCountFlagBits::e1,
                s_Data->Transparency.RevealageImage.Format,
                vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
                vk::MemoryPropertyFlagBits::eDeviceLocal,
                s_Data->Transparency.RevealageImage.Image,
                s_Data->Transparency.RevealageImage.ImageMemory);

    context.SetObjectDebugName(
        reinterpret_cast<uint64_t>(static_cast<VkImage>(*s_Data->Transparency.RevealageImage.Image)),
        vk::ObjectType::eImage,
        "Revealage Image");

    context.SetObjectDebugName(
        reinterpret_cast<uint64_t>(static_cast<VkDeviceMemory>(*s_Data->Transparency.RevealageImage.ImageMemory)),
        vk::ObjectType::eDeviceMemory,
        "Revealage Image Memory");

    s_Data->Transparency.RevealageImage.ImageView = CreateImageView(device,
                                                                    s_Data->Transparency.RevealageImage.Image,
                                                                    s_Data->Transparency.RevealageImage.Format,
                                                                    vk::ImageAspectFlagBits::eColor,
                                                                    mipLevels);

    context.SetObjectDebugName(
        reinterpret_cast<uint64_t>(static_cast<VkImageView>(*s_Data->Transparency.RevealageImage.ImageView)),
        vk::ObjectType::eImageView,
        "Revealage Image View");

    // Distortion

    CreateImage(device,
                width,
                height,
                mipLevels,
                vk::SampleCountFlagBits::e1,
                s_Data->Transparency.DistortionImage.Format,
                vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
                vk::MemoryPropertyFlagBits::eDeviceLocal,
                s_Data->Transparency.DistortionImage.Image,
                s_Data->Transparency.DistortionImage.ImageMemory);

    context.SetObjectDebugName(
        reinterpret_cast<uint64_t>(static_cast<VkImage>(*s_Data->Transparency.DistortionImage.Image)),
        vk::ObjectType::eImage,
        "Distortion Image");

    context.SetObjectDebugName(
        reinterpret_cast<uint64_t>(static_cast<VkDeviceMemory>(*s_Data->Transparency.DistortionImage.ImageMemory)),
        vk::ObjectType::eDeviceMemory,
        "Distortion Image Memory");

    s_Data->Transparency.DistortionImage.ImageView = CreateImageView(device,
                                                                     s_Data->Transparency.DistortionImage.Image,
                                                                     s_Data->Transparency.DistortionImage.Format,
                                                                     vk::ImageAspectFlagBits::eColor,
                                                                     mipLevels);

    context.SetObjectDebugName(
        reinterpret_cast<uint64_t>(static_cast<VkImageView>(*s_Data->Transparency.DistortionImage.ImageView)),
        vk::ObjectType::eImageView,
        "Distortion Image View");
}

void Renderer::SetupTransparencyDescriptors()
{
    KBRAssert(s_Data->DescriptorPool != nullptr,
                    "Descriptor pool has to be created before setting up transparency descriptors");

    auto& context = VulkanContext::Get();
    const auto& device = context.GetDevice();

    std::vector<vk::DescriptorSetLayoutBinding> bindings = {
        vk::DescriptorSetLayoutBinding{ // Opaque color
                                        .binding = 0,
                                        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                        .descriptorCount = 1,
                                        .stageFlags = vk::ShaderStageFlagBits::eFragment,
                                        .pImmutableSamplers = nullptr },
        vk::DescriptorSetLayoutBinding{ // Opaque depth
                                        .binding = 1,
                                        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                        .descriptorCount = 1,
                                        .stageFlags = vk::ShaderStageFlagBits::eFragment,
                                        .pImmutableSamplers = nullptr },
        vk::DescriptorSetLayoutBinding{ // Accumulation
                                        .binding = 2,
                                        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                        .descriptorCount = 1,
                                        .stageFlags = vk::ShaderStageFlagBits::eFragment,
                                        .pImmutableSamplers = nullptr },
        vk::DescriptorSetLayoutBinding{ // Revealage
                                        .binding = 3,
                                        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                        .descriptorCount = 1,
                                        .stageFlags = vk::ShaderStageFlagBits::eFragment,
                                        .pImmutableSamplers = nullptr },
        vk::DescriptorSetLayoutBinding{ // Distortion
                                        .binding = 4,
                                        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                        .descriptorCount = 1,
                                        .stageFlags = vk::ShaderStageFlagBits::eFragment,
                                        .pImmutableSamplers = nullptr },
        vk::DescriptorSetLayoutBinding{ // Global lighting data
                                        .binding = 5,
                                        .descriptorType = vk::DescriptorType::eUniformBuffer,
                                        .descriptorCount = 1,
                                        .stageFlags = vk::ShaderStageFlagBits::eFragment,
                                        .pImmutableSamplers = nullptr },
    };

    const vk::DescriptorSetLayoutCreateInfo layoutInfo{ .bindingCount = static_cast<uint32_t>(bindings.size()),
                                                        .pBindings = bindings.data() };

    s_Data->DescriptorSetLayouts.composite = vk::raii::DescriptorSetLayout{ device, layoutInfo };
    context.SetObjectDebugName(s_Data->DescriptorSetLayouts.composite, "Composite Descriptor Set Layout");

    const std::vector<vk::DescriptorSetLayout> compositeSetLayouts(s_Data->DescriptorSets.size(),
                                                                   *s_Data->DescriptorSetLayouts.composite);

    const vk::DescriptorSetAllocateInfo allocInfo{ .descriptorPool = *s_Data->DescriptorPool,
                                                   .descriptorSetCount =
                                                       static_cast<uint32_t>(s_Data->DescriptorSets.size()),
                                                   .pSetLayouts = compositeSetLayouts.data() };

    std::vector<vk::raii::DescriptorSet> compositeDescriptorSets = device.allocateDescriptorSets(allocInfo);

    for (uint32_t i = 0; i < VulkanContext::MaxFramesInFlight; i++) {
        s_Data->DescriptorSets[i].resolve = std::move(compositeDescriptorSets[i]);
        context.SetObjectDebugName(s_Data->DescriptorSets[i].resolve,
                                   "Composite Descriptor Set[" + std::to_string(i) + "]");

        const vk::DescriptorImageInfo opaqueColorImageInfo{ .sampler = *s_Data->LinearSampler,
                                                            .imageView = *s_Data->ColorImage.ImageView,
                                                            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };

        const vk::DescriptorImageInfo opaqueDepthImageInfo{ .sampler = *s_Data->PointSampler,
                                                            .imageView = *s_Data->DepthImage.ImageView,
                                                            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };

        const vk::DescriptorImageInfo accumulationImageInfo{ .sampler = *s_Data->PointSampler,
                                                             .imageView =
                                                                 *s_Data->Transparency.AccumulationImage.ImageView,
                                                             .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };

        const vk::DescriptorImageInfo revealageImageInfo{ .sampler = *s_Data->PointSampler,
                                                          .imageView = *s_Data->Transparency.RevealageImage.ImageView,
                                                          .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };

        const vk::DescriptorImageInfo distortionImageInfo{ .sampler = *s_Data->LinearSampler,
                                                           .imageView = *s_Data->Transparency.DistortionImage.ImageView,
                                                           .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };

        const std::vector descriptorWrites = {
            vk::WriteDescriptorSet{ .dstSet = *s_Data->DescriptorSets[i].resolve,
                                    .dstBinding = 0,
                                    .dstArrayElement = 0,
                                    .descriptorCount = 1,
                                    .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                    .pImageInfo = &opaqueColorImageInfo },
            vk::WriteDescriptorSet{ .dstSet = *s_Data->DescriptorSets[i].resolve,
                                    .dstBinding = 1,
                                    .dstArrayElement = 0,
                                    .descriptorCount = 1,
                                    .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                    .pImageInfo = &opaqueDepthImageInfo },
            vk::WriteDescriptorSet{ .dstSet = *s_Data->DescriptorSets[i].resolve,
                                    .dstBinding = 2,
                                    .dstArrayElement = 0,
                                    .descriptorCount = 1,
                                    .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                    .pImageInfo = &accumulationImageInfo },
            vk::WriteDescriptorSet{ .dstSet = *s_Data->DescriptorSets[i].resolve,
                                    .dstBinding = 3,
                                    .dstArrayElement = 0,
                                    .descriptorCount = 1,
                                    .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                    .pImageInfo = &revealageImageInfo },
            vk::WriteDescriptorSet{ .dstSet = *s_Data->DescriptorSets[i].resolve,
                                    .dstBinding = 4,
                                    .dstArrayElement = 0,
                                    .descriptorCount = 1,
                                    .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                    .pImageInfo = &distortionImageInfo },
        };

        device.updateDescriptorSets(descriptorWrites, {});
    }
}

void Renderer::CreateGTAOImage(const uint32_t width, const uint32_t height)
{
    KBRAssert(s_Data->GTAOImage.Format != vk::Format::eUndefined,
                    "GTAO image format has to be set before creating GTAO image!");
    KBRAssert(s_Data->GTAOScratchImage.Format != vk::Format::eUndefined,
                    "GTAO scratch image format has to be set before creating GTAO image!");
    KBRAssert(s_Data->GTAOImage.Format == s_Data->GTAOScratchImage.Format,
                    "GTAO image format must be the same as GTAO scratch image format!");

    auto& context = VulkanContext::Get();
    const auto& device = context.GetDevice();

    constexpr uint32_t mipLevels = 1;

    CreateImage(device,
                width,
                height,
                mipLevels,
                vk::SampleCountFlagBits::e1,
                s_Data->GTAOImage.Format,
                vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled |
                    vk::ImageUsageFlagBits::eTransferDst,
                vk::MemoryPropertyFlagBits::eDeviceLocal,
                s_Data->GTAOImage.Image,
                s_Data->GTAOImage.ImageMemory);

    context.SetObjectDebugName(s_Data->GTAOImage.Image, "GTAO Image");
    context.SetObjectDebugName(s_Data->GTAOImage.ImageMemory, "GTAO Image Memory");

    s_Data->GTAOImage.ImageView = CreateImageView(
        device, s_Data->GTAOImage.Image, s_Data->GTAOImage.Format, vk::ImageAspectFlagBits::eColor, mipLevels);
    context.SetObjectDebugName(s_Data->GTAOImage.ImageView, "GTAO Image View");
    s_Data->GTAOImageLayout = vk::ImageLayout::eUndefined;

    CreateImage(device,
                width,
                height,
                mipLevels,
                vk::SampleCountFlagBits::e1,
                s_Data->GTAOScratchImage.Format,
                vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled |
                    vk::ImageUsageFlagBits::eTransferDst,
                vk::MemoryPropertyFlagBits::eDeviceLocal,
                s_Data->GTAOScratchImage.Image,
                s_Data->GTAOScratchImage.ImageMemory);

    context.SetObjectDebugName(s_Data->GTAOScratchImage.Image, "GTAO Scratch Image");
    context.SetObjectDebugName(s_Data->GTAOScratchImage.ImageMemory, "GTAO Scratch Image Memory");

    s_Data->GTAOScratchImage.ImageView = CreateImageView(device,
                                                         s_Data->GTAOScratchImage.Image,
                                                         s_Data->GTAOScratchImage.Format,
                                                         vk::ImageAspectFlagBits::eColor,
                                                         mipLevels);
    context.SetObjectDebugName(s_Data->GTAOScratchImage.ImageView, "GTAO Scratch Image View");
}

void Renderer::SetupGTAODescriptors()
{
    KBRAssert(s_Data->DescriptorPool != nullptr,
                    "Descriptor pool has to be created before setting up GTAO descriptors");
    KBRAssert(s_Data->DescriptorSetLayouts.gtao != nullptr,
                    "GTAO descriptor set layout has to be created before setting up GTAO descriptors");
    KBRAssert(s_Data->DescriptorSetLayouts.crossBilateralBlur != nullptr,
                    "Cross-bilateral blur descriptor set layout has to be created before setting up GTAO descriptors");

    auto& context = VulkanContext::Get();
    const auto& device = context.GetDevice();

    const std::vector<vk::DescriptorSetLayout> gtaoSetLayouts(s_Data->DescriptorSets.size(),
                                                              *s_Data->DescriptorSetLayouts.gtao);

    const vk::DescriptorSetAllocateInfo allocInfo{ .descriptorPool = *s_Data->DescriptorPool,
                                                   .descriptorSetCount =
                                                       static_cast<uint32_t>(s_Data->DescriptorSets.size()),
                                                   .pSetLayouts = gtaoSetLayouts.data() };

    const vk::DescriptorImageInfo opaqueDepthImageInfo{ .sampler = *s_Data->PointSampler,
                                                        .imageView = *s_Data->DepthImage.ImageView,
                                                        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };

    const vk::DescriptorImageInfo normalTextureImageInfo{ .sampler = *s_Data->PointSampler,
                                                          .imageView = *s_Data->NormalImage.ImageView,
                                                          .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };

    std::vector<vk::raii::DescriptorSet> gtaoDescriptorSets = device.allocateDescriptorSets(allocInfo);

    for (uint32_t i = 0; i < VulkanContext::MaxFramesInFlight; i++) {
        s_Data->DescriptorSets[i].gtao = std::move(gtaoDescriptorSets[i]);
        context.SetObjectDebugName(s_Data->DescriptorSets[i].gtao, "GTAO Descriptor Set[" + std::to_string(i) + "]");

        const vk::DescriptorBufferInfo gtaoConstantsBufferInfo{ .buffer = *s_Data->UniformBuffers[i].gtao->GetBuffer(),
                                                                .offset = 0,
                                                                .range = sizeof(GTAOConstants) };

        const vk::DescriptorImageInfo gtaoStorageImageInfo{ .sampler = *s_Data->PointSampler,
                                                            .imageView = *s_Data->GTAOImage.ImageView,
                                                            .imageLayout = vk::ImageLayout::eGeneral };

        const std::vector descriptorWrites = {
            vk::WriteDescriptorSet{ .dstSet = *s_Data->DescriptorSets[i].gtao,
                                    .dstBinding = 0,
                                    .dstArrayElement = 0,
                                    .descriptorCount = 1,
                                    .descriptorType = vk::DescriptorType::eUniformBuffer,
                                    .pBufferInfo = &gtaoConstantsBufferInfo },
            vk::WriteDescriptorSet{ .dstSet = *s_Data->DescriptorSets[i].gtao,
                                    .dstBinding = 1,
                                    .dstArrayElement = 0,
                                    .descriptorCount = 1,
                                    .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                    .pImageInfo = &opaqueDepthImageInfo },
            vk::WriteDescriptorSet{ .dstSet = *s_Data->DescriptorSets[i].gtao,
                                    .dstBinding = 2,
                                    .dstArrayElement = 0,
                                    .descriptorCount = 1,
                                    .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                    .pImageInfo = &normalTextureImageInfo },
            vk::WriteDescriptorSet{ .dstSet = *s_Data->DescriptorSets[i].gtao,
                                    .dstBinding = 3,
                                    .dstArrayElement = 0,
                                    .descriptorCount = 1,
                                    .descriptorType = vk::DescriptorType::eStorageImage,
                                    .pImageInfo = &gtaoStorageImageInfo }
        };

        device.updateDescriptorSets(descriptorWrites, {});
    }

    const std::vector<vk::DescriptorSetLayout> blurSetLayouts(s_Data->DescriptorSets.size(),
                                                              *s_Data->DescriptorSetLayouts.crossBilateralBlur);

    const vk::DescriptorSetAllocateInfo blurAllocInfo{ .descriptorPool = *s_Data->DescriptorPool,
                                                       .descriptorSetCount =
                                                           static_cast<uint32_t>(s_Data->DescriptorSets.size()),
                                                       .pSetLayouts = blurSetLayouts.data() };

    std::vector<vk::raii::DescriptorSet> horizontalBlurDescriptorSets = device.allocateDescriptorSets(blurAllocInfo);

    for (uint32_t i = 0; i < VulkanContext::MaxFramesInFlight; i++) {
        s_Data->DescriptorSets[i].crossBilateralBlurHorizontal = std::move(horizontalBlurDescriptorSets[i]);
        context.SetObjectDebugName(s_Data->DescriptorSets[i].crossBilateralBlurHorizontal,
                                   "Cross-Bilateral Blur Horizontal Descriptor Set[" + std::to_string(i) + "]");

        const vk::DescriptorImageInfo gtaoInputImageInfo{ .sampler = *s_Data->PointSampler,
                                                          .imageView = *s_Data->GTAOImage.ImageView,
                                                          .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };

        const vk::DescriptorImageInfo gtaoWriteStorageImageInfo{ .sampler = *s_Data->PointSampler,
                                                                 .imageView = *s_Data->GTAOScratchImage.ImageView,
                                                                 .imageLayout = vk::ImageLayout::eGeneral };

        const std::vector descriptorWrites = {
            vk::WriteDescriptorSet{ .dstSet = *s_Data->DescriptorSets[i].crossBilateralBlurHorizontal,
                                    .dstBinding = 0,
                                    .dstArrayElement = 0,
                                    .descriptorCount = 1,
                                    .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                    .pImageInfo = &gtaoInputImageInfo },
            vk::WriteDescriptorSet{ .dstSet = *s_Data->DescriptorSets[i].crossBilateralBlurHorizontal,
                                    .dstBinding = 1,
                                    .dstArrayElement = 0,
                                    .descriptorCount = 1,
                                    .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                    .pImageInfo = &opaqueDepthImageInfo },
            vk::WriteDescriptorSet{ .dstSet = *s_Data->DescriptorSets[i].crossBilateralBlurHorizontal,
                                    .dstBinding = 2,
                                    .dstArrayElement = 0,
                                    .descriptorCount = 1,
                                    .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                    .pImageInfo = &normalTextureImageInfo },
            vk::WriteDescriptorSet{ .dstSet = *s_Data->DescriptorSets[i].crossBilateralBlurHorizontal,
                                    .dstBinding = 3,
                                    .dstArrayElement = 0,
                                    .descriptorCount = 1,
                                    .descriptorType = vk::DescriptorType::eStorageImage,
                                    .pImageInfo = &gtaoWriteStorageImageInfo }
        };

        device.updateDescriptorSets(descriptorWrites, {});
    }

    std::vector<vk::raii::DescriptorSet> verticalBlurDescriptorSets = device.allocateDescriptorSets(blurAllocInfo);

    for (uint32_t i = 0; i < VulkanContext::MaxFramesInFlight; i++) {
        s_Data->DescriptorSets[i].crossBilateralBlurVertical = std::move(verticalBlurDescriptorSets[i]);
        context.SetObjectDebugName(s_Data->DescriptorSets[i].crossBilateralBlurVertical,
                                   "Cross-Bilateral Blur Vertical Descriptor Set[" + std::to_string(i) + "]");

        const vk::DescriptorImageInfo gtaoInputImageInfo{ .sampler = *s_Data->PointSampler,
                                                          .imageView = *s_Data->GTAOScratchImage.ImageView,
                                                          .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };

        const vk::DescriptorImageInfo gtaoWriteStorageImageInfo{ .sampler = *s_Data->PointSampler,
                                                                 .imageView = *s_Data->GTAOImage.ImageView,
                                                                 .imageLayout = vk::ImageLayout::eGeneral };

        const std::vector descriptorWrites = {
            vk::WriteDescriptorSet{ .dstSet = *s_Data->DescriptorSets[i].crossBilateralBlurVertical,
                                    .dstBinding = 0,
                                    .dstArrayElement = 0,
                                    .descriptorCount = 1,
                                    .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                    .pImageInfo = &gtaoInputImageInfo },
            vk::WriteDescriptorSet{ .dstSet = *s_Data->DescriptorSets[i].crossBilateralBlurVertical,
                                    .dstBinding = 1,
                                    .dstArrayElement = 0,
                                    .descriptorCount = 1,
                                    .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                    .pImageInfo = &opaqueDepthImageInfo },
            vk::WriteDescriptorSet{ .dstSet = *s_Data->DescriptorSets[i].crossBilateralBlurVertical,
                                    .dstBinding = 2,
                                    .dstArrayElement = 0,
                                    .descriptorCount = 1,
                                    .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                    .pImageInfo = &normalTextureImageInfo },
            vk::WriteDescriptorSet{ .dstSet = *s_Data->DescriptorSets[i].crossBilateralBlurVertical,
                                    .dstBinding = 3,
                                    .dstArrayElement = 0,
                                    .descriptorCount = 1,
                                    .descriptorType = vk::DescriptorType::eStorageImage,
                                    .pImageInfo = &gtaoWriteStorageImageInfo }
        };

        device.updateDescriptorSets(descriptorWrites, {});
    }
}

void Renderer::CreateTonemappedImage(const uint32_t width, const uint32_t height)
{
    KBRAssert(s_Data->TonemappedImage.Format != vk::Format::eUndefined,
                    "Tonemapped image format has to be set before creating tonemapped image!");

    auto& context = VulkanContext::Get();
    const auto& device = context.GetDevice();

    constexpr uint32_t mipLevels = 1;

    CreateImage(device,
                width,
                height,
                mipLevels,
                vk::SampleCountFlagBits::e1,
                s_Data->TonemappedImage.Format,
                vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
                vk::MemoryPropertyFlagBits::eDeviceLocal,
                s_Data->TonemappedImage.Image,
                s_Data->TonemappedImage.ImageMemory);

    context.SetObjectDebugName(s_Data->TonemappedImage.Image, "Tonemapped Image");
    context.SetObjectDebugName(s_Data->TonemappedImage.ImageMemory, "Tonemapped Image Memory");

    s_Data->TonemappedImage.ImageView = CreateImageView(device,
                                                        s_Data->TonemappedImage.Image,
                                                        s_Data->TonemappedImage.Format,
                                                        vk::ImageAspectFlagBits::eColor,
                                                        mipLevels);
    context.SetObjectDebugName(s_Data->TonemappedImage.ImageView, "Tonemapped Image View");
}

void Renderer::CreateFXAAImage(const uint32_t width, const uint32_t height)
{
    KBRAssert(s_Data->CompositeImage.Format != vk::Format::eUndefined,
                    "Composite image format has to be set before creating composite image!");

    auto& context = VulkanContext::Get();
    const auto& device = context.GetDevice();

    constexpr uint32_t mipLevels = 1;

    CreateImage(device,
                width,
                height,
                mipLevels,
                vk::SampleCountFlagBits::e1,
                s_Data->CompositeImage.Format,
                vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
                vk::MemoryPropertyFlagBits::eDeviceLocal,
                s_Data->CompositeImage.Image,
                s_Data->CompositeImage.ImageMemory);

    context.SetObjectDebugName(s_Data->CompositeImage.Image, "Composite Image");
    context.SetObjectDebugName(s_Data->CompositeImage.ImageMemory, "Composite Image Memory");

    s_Data->CompositeImage.ImageView = CreateImageView(device,
                                                       s_Data->CompositeImage.Image,
                                                       s_Data->CompositeImage.Format,
                                                       vk::ImageAspectFlagBits::eColor,
                                                       mipLevels);
    context.SetObjectDebugName(s_Data->CompositeImage.ImageView, "Composite Image View");
}

void Renderer::SetupTonemappingResolveDescriptors()
{
    KBRAssert(s_Data->DescriptorPool != nullptr,
                    "Descriptor pool has to be created before setting up tonemapping descriptors");
    KBRAssert(
        s_Data->DescriptorSetLayouts.tonemappingResolve != nullptr,
        "Tonemapping resolve descriptor set layout has to be created before setting up tonemapping descriptors");

    auto& context = VulkanContext::Get();
    const auto& device = context.GetDevice();

    const std::vector<vk::DescriptorSetLayout> tonemappingSetLayouts(s_Data->DescriptorSets.size(),
                                                                     *s_Data->DescriptorSetLayouts.tonemappingResolve);

    const vk::DescriptorSetAllocateInfo allocInfo{ .descriptorPool = *s_Data->DescriptorPool,
                                                   .descriptorSetCount =
                                                       static_cast<uint32_t>(s_Data->DescriptorSets.size()),
                                                   .pSetLayouts = tonemappingSetLayouts.data() };

    std::vector<vk::raii::DescriptorSet> tonemappingDescriptorSets = device.allocateDescriptorSets(allocInfo);

    for (uint32_t i = 0; i < VulkanContext::MaxFramesInFlight; i++) {
        s_Data->DescriptorSets[i].tonemappingResolve = std::move(tonemappingDescriptorSets[i]);
        context.SetObjectDebugName(s_Data->DescriptorSets[i].tonemappingResolve,
                                   "Tonemapping Resolve Descriptor Set[" + std::to_string(i) + "]");

        const vk::DescriptorImageInfo sceneColorImageInfo{ .sampler = nullptr,
                                                           .imageView = *s_Data->ResolveImage.ImageView,
                                                           .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };

        const vk::DescriptorImageInfo linearSamplerInfo{ .sampler = *s_Data->LinearSampler,
                                                         .imageView = nullptr,
                                                         .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };

        const vk::DescriptorImageInfo bloomTextureInfo{ .sampler = *s_Data->LinearSampler,
                                                        .imageView = *s_Data->Bloom.ImageViews[0],
                                                        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };

        const vk::DescriptorBufferInfo globalLightingBufferInfo{
            .buffer = *s_Data->UniformBuffers[i].globalLighting->GetBuffer(),
            .offset = 0,
            .range = sizeof(GlobalLighting)
        };

        const std::vector descriptorWrites = {
            vk::WriteDescriptorSet{ .dstSet = *s_Data->DescriptorSets[i].tonemappingResolve,
                                    .dstBinding = 0,
                                    .dstArrayElement = 0,
                                    .descriptorCount = 1,
                                    .descriptorType = vk::DescriptorType::eSampledImage,
                                    .pImageInfo = &sceneColorImageInfo },
            vk::WriteDescriptorSet{ .dstSet = *s_Data->DescriptorSets[i].tonemappingResolve,
                                    .dstBinding = 1,
                                    .dstArrayElement = 0,
                                    .descriptorCount = 1,
                                    .descriptorType = vk::DescriptorType::eSampler,
                                    .pImageInfo = &linearSamplerInfo },
            vk::WriteDescriptorSet{ .dstSet = *s_Data->DescriptorSets[i].tonemappingResolve,
                                    .dstBinding = 2,
                                    .dstArrayElement = 0,
                                    .descriptorCount = 1,
                                    .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                    .pImageInfo = &bloomTextureInfo },
            vk::WriteDescriptorSet{ .dstSet = *s_Data->DescriptorSets[i].tonemappingResolve,
                                    .dstBinding = 3,
                                    .dstArrayElement = 0,
                                    .descriptorCount = 1,
                                    .descriptorType = vk::DescriptorType::eUniformBuffer,
                                    .pBufferInfo = &globalLightingBufferInfo }
        };

        device.updateDescriptorSets(descriptorWrites, {});
    }
}

void Renderer::SetupFXAADescriptors()
{
    KBRAssert(s_Data->DescriptorPool != nullptr,
                    "Descriptor pool has to be created before setting up FXAA descriptors");
    KBRAssert(s_Data->DescriptorSetLayouts.fxaa != nullptr,
                    "FXAA descriptor set layout has to be created before setting up FXAA descriptors");

    auto& context = VulkanContext::Get();
    const auto& device = context.GetDevice();

    const std::vector<vk::DescriptorSetLayout> fxaaSetLayouts(s_Data->DescriptorSets.size(),
                                                              *s_Data->DescriptorSetLayouts.fxaa);

    const vk::DescriptorSetAllocateInfo allocInfo{ .descriptorPool = *s_Data->DescriptorPool,
                                                   .descriptorSetCount =
                                                       static_cast<uint32_t>(s_Data->DescriptorSets.size()),
                                                   .pSetLayouts = fxaaSetLayouts.data() };

    std::vector<vk::raii::DescriptorSet> fxaaDescriptorSets = device.allocateDescriptorSets(allocInfo);

    for (uint32_t i = 0; i < VulkanContext::MaxFramesInFlight; i++) {
        s_Data->DescriptorSets[i].fxaa = std::move(fxaaDescriptorSets[i]);
        context.SetObjectDebugName(s_Data->DescriptorSets[i].fxaa, "FXAA Descriptor Set[" + std::to_string(i) + "]");

        const vk::DescriptorImageInfo tonemappedImageInfo{ .sampler = nullptr,
                                                           .imageView = *s_Data->TonemappedImage.ImageView,
                                                           .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };

        const vk::DescriptorImageInfo linearSamplerInfo{ .sampler = *s_Data->LinearSampler,
                                                         .imageView = nullptr,
                                                         .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };

        const vk::DescriptorImageInfo bloomTextureInfo{ .sampler = *s_Data->LinearSampler,
                                                        .imageView = *s_Data->Bloom.ImageViews[0],
                                                        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };

        const vk::DescriptorBufferInfo globalLightingBufferInfo{
            .buffer = *s_Data->UniformBuffers[i].globalLighting->GetBuffer(),
            .offset = 0,
            .range = sizeof(GlobalLighting)
        };

        const std::vector descriptorWrites = {
            vk::WriteDescriptorSet{ .dstSet = *s_Data->DescriptorSets[i].fxaa,
                                    .dstBinding = 0,
                                    .dstArrayElement = 0,
                                    .descriptorCount = 1,
                                    .descriptorType = vk::DescriptorType::eSampledImage,
                                    .pImageInfo = &tonemappedImageInfo },
            vk::WriteDescriptorSet{ .dstSet = *s_Data->DescriptorSets[i].fxaa,
                                    .dstBinding = 1,
                                    .dstArrayElement = 0,
                                    .descriptorCount = 1,
                                    .descriptorType = vk::DescriptorType::eSampler,
                                    .pImageInfo = &linearSamplerInfo },
            vk::WriteDescriptorSet{ .dstSet = *s_Data->DescriptorSets[i].fxaa,
                                    .dstBinding = 2,
                                    .dstArrayElement = 0,
                                    .descriptorCount = 1,
                                    .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                    .pImageInfo = &bloomTextureInfo },
            vk::WriteDescriptorSet{ .dstSet = *s_Data->DescriptorSets[i].fxaa,
                                    .dstBinding = 3,
                                    .dstArrayElement = 0,
                                    .descriptorCount = 1,
                                    .descriptorType = vk::DescriptorType::eUniformBuffer,
                                    .pBufferInfo = &globalLightingBufferInfo }
        };

        device.updateDescriptorSets(descriptorWrites, {});
    }
}

void Renderer::CreateBloomImage(const uint32_t width, const uint32_t height)
{
    KBRAssert(s_Data->Bloom.Format != vk::Format::eUndefined,
                    "Bloom image format has to be set before creating bloom resources!");

    const uint32_t bloomWidth = width / 2;
    const uint32_t bloomHeight = height / 2;
    const uint32_t mipLevels = s_Data->Bloom.MipLevels;

    auto& context = VulkanContext::Get();
    const auto& device = context.GetDevice();

    CreateImage(device,
                bloomWidth,
                bloomHeight,
                mipLevels,
                vk::SampleCountFlagBits::e1,
                s_Data->Bloom.Format,
                vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled |
                    vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eStorage,
                vk::MemoryPropertyFlagBits::eDeviceLocal,
                s_Data->Bloom.Image,
                s_Data->Bloom.ImageMemory);

    context.SetObjectDebugName(s_Data->Bloom.Image, "Bloom Image");
    context.SetObjectDebugName(s_Data->Bloom.ImageMemory, "Bloom Image Memory");

    s_Data->Bloom.MipSizes.clear();
    s_Data->Bloom.MipSizes.reserve(mipLevels);

    s_Data->Bloom.ImageViews.clear();
    s_Data->Bloom.ImageViews.reserve(mipLevels);

    for (uint32_t i = 0; i < mipLevels; i++) {
        const uint32_t mipWidth = bloomWidth >> i;
        const uint32_t mipHeight = bloomHeight >> i;

        s_Data->Bloom.MipSizes.emplace_back(mipWidth, mipHeight);

        const vk::ImageViewCreateInfo viewInfo{ .flags = {},
                                                .image = *s_Data->Bloom.Image,
                                                .viewType = vk::ImageViewType::e2D,
                                                .format = s_Data->Bloom.Format,
                                                .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eColor,
                                                                      .baseMipLevel = i,
                                                                      .levelCount = 1,
                                                                      .baseArrayLayer = 0,
                                                                      .layerCount = 1 } };
        s_Data->Bloom.ImageViews.push_back(device.createImageView(viewInfo));
        context.SetObjectDebugName(s_Data->Bloom.ImageViews.back(), "Bloom Image View[" + std::to_string(i) + "]");
    }
}

void Renderer::SetupBloomDescriptors()
{
    auto& context = VulkanContext::Get();
    const auto& device = context.GetDevice();
    const uint32_t mipCount = s_Data->Bloom.MipLevels;

    if (s_Data->Bloom.ExtractSet == nullptr) {
        const vk::DescriptorSetAllocateInfo extractAllocInfo{ .descriptorPool = *s_Data->DescriptorPool,
                                                              .descriptorSetCount = 1,
                                                              .pSetLayouts = &*s_Data->DescriptorSetLayouts.bloom };

        s_Data->Bloom.ExtractSet = std::move(device.allocateDescriptorSets(extractAllocInfo).front());

        std::vector<vk::DescriptorSetLayout> mipLayouts(mipCount - 1, *s_Data->DescriptorSetLayouts.bloom);
        const vk::DescriptorSetAllocateInfo mipAllocInfo{ .descriptorPool = *s_Data->DescriptorPool,
                                                          .descriptorSetCount = mipCount - 1,
                                                          .pSetLayouts = mipLayouts.data() };

        s_Data->Bloom.DownsampleSets = device.allocateDescriptorSets(mipAllocInfo);
        s_Data->Bloom.UpsampleSets = device.allocateDescriptorSets(mipAllocInfo);
    }

    std::vector<vk::WriteDescriptorSet> writes;

    std::vector<vk::DescriptorImageInfo> imageInfos;
    imageInfos.reserve(2 + (mipCount - 1) * 4);

    // Write Extract Set (Resolve Image -> Bloom Mip 0)
    imageInfos.push_back(
        { *s_Data->LinearSampler, *s_Data->ResolveImage.ImageView, vk::ImageLayout::eShaderReadOnlyOptimal });
    imageInfos.push_back({ *s_Data->LinearSampler, *s_Data->Bloom.ImageViews[0], vk::ImageLayout::eGeneral });

    writes.push_back(vk::WriteDescriptorSet{ .dstSet = *s_Data->Bloom.ExtractSet,
                                             .dstBinding = 0,
                                             .dstArrayElement = 0,
                                             .descriptorCount = 1,
                                             .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                             .pImageInfo = &imageInfos[0] });
    writes.push_back(vk::WriteDescriptorSet{ .dstSet = *s_Data->Bloom.ExtractSet,
                                             .dstBinding = 1,
                                             .dstArrayElement = 0,
                                             .descriptorCount = 1,
                                             .descriptorType = vk::DescriptorType::eStorageImage,
                                             .pImageInfo = &imageInfos[1] });

    // Write Downsample and Upsample Sets
    for (uint32_t i = 0; i < mipCount - 1; i++) {
        const uint32_t baseIdx = static_cast<uint32_t>(imageInfos.size());

        // Downsample: Reads Mip i, Writes Mip i+1
        imageInfos.push_back({ *s_Data->LinearSampler, *s_Data->Bloom.ImageViews[i], vk::ImageLayout::eGeneral });
        imageInfos.push_back({ *s_Data->LinearSampler, *s_Data->Bloom.ImageViews[i + 1], vk::ImageLayout::eGeneral });
        writes.push_back(vk::WriteDescriptorSet{ .dstSet = *s_Data->Bloom.DownsampleSets[i],
                                                 .dstBinding = 0,
                                                 .dstArrayElement = 0,
                                                 .descriptorCount = 1,
                                                 .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                                 .pImageInfo = &imageInfos[baseIdx] });
        writes.push_back(vk::WriteDescriptorSet{ .dstSet = *s_Data->Bloom.DownsampleSets[i],
                                                 .dstBinding = 1,
                                                 .dstArrayElement = 0,
                                                 .descriptorCount = 1,
                                                 .descriptorType = vk::DescriptorType::eStorageImage,
                                                 .pImageInfo = &imageInfos[baseIdx + 1] });

        // Upsample: Reads Mip i+1, Writes Mip i
        imageInfos.push_back({ *s_Data->LinearSampler, *s_Data->Bloom.ImageViews[i + 1], vk::ImageLayout::eGeneral });
        imageInfos.push_back({ *s_Data->LinearSampler, *s_Data->Bloom.ImageViews[i], vk::ImageLayout::eGeneral });
        writes.push_back(vk::WriteDescriptorSet{ .dstSet = *s_Data->Bloom.UpsampleSets[i],
                                                 .dstBinding = 0,
                                                 .dstArrayElement = 0,
                                                 .descriptorCount = 1,
                                                 .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                                 .pImageInfo = &imageInfos[baseIdx + 2] });
        writes.push_back(vk::WriteDescriptorSet{ .dstSet = *s_Data->Bloom.UpsampleSets[i],
                                                 .dstBinding = 1,
                                                 .dstArrayElement = 0,
                                                 .descriptorCount = 1,
                                                 .descriptorType = vk::DescriptorType::eStorageImage,
                                                 .pImageInfo = &imageInfos[baseIdx + 3] });
    }

    device.updateDescriptorSets(writes, {});
}

void Renderer::CreateBloomResources(const uint32_t width, const uint32_t height)
{
    auto& context = VulkanContext::Get();
    const auto& device = context.GetDevice();

    s_Data->Bloom.Format = context.FindSupportedFormat({ vk::Format::eR16G16B16A16Sfloat },
                                                       vk::ImageTiling::eOptimal,
                                                       vk::FormatFeatureFlagBits::eStorageImage |
                                                           vk::FormatFeatureFlagBits::eSampledImage);

    CreateBloomImage(width, height);

    std::vector<vk::DescriptorSetLayoutBinding> bindings = {
        vk::DescriptorSetLayoutBinding{ // Source
                                        .binding = 0,
                                        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                        .descriptorCount = 1,
                                        .stageFlags = vk::ShaderStageFlagBits::eCompute,
                                        .pImmutableSamplers = nullptr },
        vk::DescriptorSetLayoutBinding{ // Destination
                                        .binding = 1,
                                        .descriptorType = vk::DescriptorType::eStorageImage,
                                        .descriptorCount = 1,
                                        .stageFlags = vk::ShaderStageFlagBits::eCompute,
                                        .pImmutableSamplers = nullptr },
    };

    const vk::DescriptorSetLayoutCreateInfo layoutInfo{ .bindingCount = static_cast<uint32_t>(bindings.size()),
                                                        .pBindings = bindings.data() };

    s_Data->DescriptorSetLayouts.bloom = vk::raii::DescriptorSetLayout{ device, layoutInfo };
    context.SetObjectDebugName(s_Data->DescriptorSetLayouts.bloom, "Bloom Descriptor Set Layout");

    const std::array setLayouts = { *s_Data->DescriptorSetLayouts.bloom };

    constexpr vk::PushConstantRange downsamplePushConstantRange{ .stageFlags = vk::ShaderStageFlagBits::eCompute,
                                                                 .offset = 0,
                                                                 .size = sizeof(BloomData::DownsamplePushConstants) };

    const vk::PipelineLayoutCreateInfo bloomDowsamplePipelineLayoutInfo{ .setLayoutCount =
                                                                             static_cast<uint32_t>(setLayouts.size()),
                                                                         .pSetLayouts = setLayouts.data(),
                                                                         .pushConstantRangeCount = 1,
                                                                         .pPushConstantRanges =
                                                                             &downsamplePushConstantRange };

    s_Data->Bloom.DownsamplePipelineLayout = vk::raii::PipelineLayout{ device, bloomDowsamplePipelineLayoutInfo };
    context.SetObjectDebugName(s_Data->Bloom.DownsamplePipelineLayout, "Bloom Downsample Pipeline Layout");

    ComputePipelineSpecification bloomDownsamplePipelineSpec{};
    bloomDownsamplePipelineSpec.Name = "Bloom Downsample Pipeline";
    bloomDownsamplePipelineSpec.Shader = CreateRef<Shader>("bloom_downsample", "Bloom Downsample");
    bloomDownsamplePipelineSpec.PipelineLayout = *s_Data->Bloom.DownsamplePipelineLayout;

    s_Data->Bloom.DownsamplePipeline = CreateRef<ComputePipeline>(bloomDownsamplePipelineSpec);

    constexpr vk::PushConstantRange upsamplePushConstantRange{ .stageFlags = vk::ShaderStageFlagBits::eCompute,
                                                               .offset = 0,
                                                               .size = sizeof(BloomData::UpsamplePushConstants) };

    const vk::PipelineLayoutCreateInfo bloomUpsamplePipelineLayoutInfo{ .setLayoutCount =
                                                                            static_cast<uint32_t>(setLayouts.size()),
                                                                        .pSetLayouts = setLayouts.data(),
                                                                        .pushConstantRangeCount = 1,
                                                                        .pPushConstantRanges =
                                                                            &upsamplePushConstantRange };

    s_Data->Bloom.UpsamplePipelineLayout = vk::raii::PipelineLayout{ device, bloomUpsamplePipelineLayoutInfo };
    context.SetObjectDebugName(s_Data->Bloom.UpsamplePipelineLayout, "Bloom Upsample Pipeline Layout");

    ComputePipelineSpecification bloomUpsamplePipelineSpec{};
    bloomUpsamplePipelineSpec.Name = "Bloom Upsample Pipeline";
    bloomUpsamplePipelineSpec.Shader = CreateRef<Shader>("bloom_upsample", "Bloom Upsample");
    bloomUpsamplePipelineSpec.PipelineLayout = *s_Data->Bloom.UpsamplePipelineLayout;

    s_Data->Bloom.UpsamplePipeline = CreateRef<ComputePipeline>(bloomUpsamplePipelineSpec);
}

void Renderer::CreateSMAATextures()
{
    auto& context = VulkanContext::Get();
    const auto& device = context.GetDevice();

    constexpr uint64_t areaWidth = 160, areaHeight = 560;
    constexpr vk::DeviceSize areaSize = areaWidth * areaHeight * 2; // 2 bytes per pixel
    constexpr vk::Format areaFormat = vk::Format::eR8G8Unorm;

    constexpr uint64_t searchWidth = 66, searchHeight = 33;
    constexpr vk::DeviceSize searchSize = searchWidth * searchHeight * 1; // 1 byte per pixel
    constexpr vk::Format searchFormat = vk::Format::eR8Unorm;

    auto uploadLUT = [&](ImageData& outImage,
                         const unsigned char* byteData,
                         uint32_t width,
                         uint32_t height,
                         vk::Format format,
                         vk::DeviceSize size,
                         const std::string& debugName) {
        vk::raii::Buffer stagingBuffer = nullptr;
        vk::raii::DeviceMemory stagingMemory = nullptr;
        CreateBuffer(device,
                     size,
                     vk::BufferUsageFlagBits::eTransferSrc,
                     vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
                     stagingBuffer,
                     stagingMemory);

        void* mappedData = stagingMemory.mapMemory(0, size);
        std::memcpy(mappedData, byteData, size);
        stagingMemory.unmapMemory();

        CreateImage(device,
                    width,
                    height,
                    1,
                    vk::SampleCountFlagBits::e1,
                    format,
                    vk::ImageTiling::eOptimal,
                    vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
                    vk::MemoryPropertyFlagBits::eDeviceLocal,
                    outImage.Image,
                    outImage.ImageMemory);

        context.SetObjectDebugName(outImage.Image, debugName);
        outImage.Format = format;

        outImage.ImageView = CreateImageView(device, outImage.Image, format, vk::ImageAspectFlagBits::eColor, 1);
        context.SetObjectDebugName(outImage.ImageView, debugName + " View");

        auto cmd = context.BeginSingleTimeCommands();

        const vk::ImageMemoryBarrier2 toTransferBarrier = { .srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe,
                                                            .srcAccessMask = {},
                                                            .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
                                                            .dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
                                                            .oldLayout = vk::ImageLayout::eUndefined,
                                                            .newLayout = vk::ImageLayout::eTransferDstOptimal,
                                                            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                                            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                                            .image = *outImage.Image,
                                                            .subresourceRange = {
                                                                vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 } };
        cmd.pipelineBarrier2({ .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &toTransferBarrier });

        const vk::BufferImageCopy copyRegion{ .bufferOffset = 0,
                                              .bufferRowLength = 0,
                                              .bufferImageHeight = 0,
                                              .imageSubresource = { vk::ImageAspectFlagBits::eColor, 0, 0, 1 },
                                              .imageOffset = { 0, 0, 0 },
                                              .imageExtent = { width, height, 1 } };
        cmd.copyBufferToImage(*stagingBuffer, *outImage.Image, vk::ImageLayout::eTransferDstOptimal, copyRegion);

        const vk::ImageMemoryBarrier2 toShaderReadBarrier = {
            .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
            .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
            .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
            .oldLayout = vk::ImageLayout::eTransferDstOptimal,
            .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = *outImage.Image,
            .subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 }
        };
        cmd.pipelineBarrier2({ .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &toShaderReadBarrier });

        context.EndSingleTimeCommands(cmd);
    };

    uploadLUT(s_Data->SMAAResources.AreaTexture,
              areaTexBytes,
              areaWidth,
              areaHeight,
              areaFormat,
              areaSize,
              "SMAA Area Texture");
    uploadLUT(s_Data->SMAAResources.SearchTexture,
              searchTexBytes,
              searchWidth,
              searchHeight,
              searchFormat,
              searchSize,
              "SMAA Search Texture");
}

void Renderer::CreateSMAADescriptorSetAndPipelineLayouts()
{

    auto& context = VulkanContext::Get();
    const auto& device = context.GetDevice();

    constexpr vk::PushConstantRange pushConstantRange{ .stageFlags = vk::ShaderStageFlagBits::eVertex |
                                                                     vk::ShaderStageFlagBits::eFragment,
                                                       .offset = 0,
                                                       .size = sizeof(SMAAData::PushConstants) };

    // Edge detection
    {
        std::vector<vk::DescriptorSetLayoutBinding> bindings = {
            vk::DescriptorSetLayoutBinding{ // Tonemapped image
                                            .binding = 0,
                                            .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                            .descriptorCount = 1,
                                            .stageFlags = vk::ShaderStageFlagBits::eFragment,
                                            .pImmutableSamplers = nullptr },
            vk::DescriptorSetLayoutBinding{ // Linear sampler
                                            .binding = 1,
                                            .descriptorType = vk::DescriptorType::eSampler,
                                            .descriptorCount = 1,
                                            .stageFlags = vk::ShaderStageFlagBits::eFragment,
                                            .pImmutableSamplers = nullptr },
            vk::DescriptorSetLayoutBinding{ // Point sampler
                                            .binding = 2,
                                            .descriptorType = vk::DescriptorType::eSampler,
                                            .descriptorCount = 1,
                                            .stageFlags = vk::ShaderStageFlagBits::eFragment,
                                            .pImmutableSamplers = nullptr },
        };

        const vk::DescriptorSetLayoutCreateInfo layoutInfo{ .bindingCount = static_cast<uint32_t>(bindings.size()),
                                                            .pBindings = bindings.data() };

        s_Data->SMAAResources.DescriptorSetLayouts.EdgeDetection = vk::raii::DescriptorSetLayout{ device, layoutInfo };
        context.SetObjectDebugName(s_Data->SMAAResources.DescriptorSetLayouts.EdgeDetection,
                                   "SMAA Edge Detection Set Layout");

        const std::array setLayouts = { *s_Data->SMAAResources.DescriptorSetLayouts.EdgeDetection };

        const vk::PipelineLayoutCreateInfo pipelineLayoutInfo{ .setLayoutCount =
                                                                   static_cast<uint32_t>(setLayouts.size()),
                                                               .pSetLayouts = setLayouts.data(),
                                                               .pushConstantRangeCount = 1,
                                                               .pPushConstantRanges = &pushConstantRange };

        s_Data->SMAAResources.EdgeDetectionPipelineLayout = vk::raii::PipelineLayout{ device, pipelineLayoutInfo };
        context.SetObjectDebugName(s_Data->SMAAResources.EdgeDetectionPipelineLayout,
                                   "SMAA Edge Detection Pipeline Layout");
    }

    // Blend Weight Calculation
    {
        std::vector<vk::DescriptorSetLayoutBinding> bindings = {
            vk::DescriptorSetLayoutBinding{ // Edges texture
                                            .binding = 0,
                                            .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                            .descriptorCount = 1,
                                            .stageFlags = vk::ShaderStageFlagBits::eFragment,
                                            .pImmutableSamplers = nullptr },
            vk::DescriptorSetLayoutBinding{ // Area texture
                                            .binding = 1,
                                            .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                            .descriptorCount = 1,
                                            .stageFlags = vk::ShaderStageFlagBits::eFragment,
                                            .pImmutableSamplers = nullptr },
            vk::DescriptorSetLayoutBinding{ // Search texture
                                            .binding = 2,
                                            .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                            .descriptorCount = 1,
                                            .stageFlags = vk::ShaderStageFlagBits::eFragment,
                                            .pImmutableSamplers = nullptr },
            vk::DescriptorSetLayoutBinding{ // Linear sampler
                                            .binding = 3,
                                            .descriptorType = vk::DescriptorType::eSampler,
                                            .descriptorCount = 1,
                                            .stageFlags = vk::ShaderStageFlagBits::eFragment,
                                            .pImmutableSamplers = nullptr },
            vk::DescriptorSetLayoutBinding{ // Point sampler
                                            .binding = 4,
                                            .descriptorType = vk::DescriptorType::eSampler,
                                            .descriptorCount = 1,
                                            .stageFlags = vk::ShaderStageFlagBits::eFragment,
                                            .pImmutableSamplers = nullptr },
        };

        const vk::DescriptorSetLayoutCreateInfo layoutInfo{ .bindingCount = static_cast<uint32_t>(bindings.size()),
                                                            .pBindings = bindings.data() };

        s_Data->SMAAResources.DescriptorSetLayouts.BlendWeight = vk::raii::DescriptorSetLayout{ device, layoutInfo };
        context.SetObjectDebugName(s_Data->SMAAResources.DescriptorSetLayouts.BlendWeight,
                                   "SMAA Blend Weights Set Layout");

        const std::array setLayouts = { *s_Data->SMAAResources.DescriptorSetLayouts.BlendWeight };

        const vk::PipelineLayoutCreateInfo pipelineLayoutInfo{ .setLayoutCount =
                                                                   static_cast<uint32_t>(setLayouts.size()),
                                                               .pSetLayouts = setLayouts.data(),
                                                               .pushConstantRangeCount = 1,
                                                               .pPushConstantRanges = &pushConstantRange };

        s_Data->SMAAResources.BlendWeightPipelineLayout = vk::raii::PipelineLayout{ device, pipelineLayoutInfo };
        context.SetObjectDebugName(s_Data->SMAAResources.BlendWeightPipelineLayout,
                                   "SMAA Blend Weights Pipeline Layout");
    }

    // Neighbourhood Blend
    {
        std::vector<vk::DescriptorSetLayoutBinding> bindings = {
            vk::DescriptorSetLayoutBinding{ // Tonemapped image
                                            .binding = 0,
                                            .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                            .descriptorCount = 1,
                                            .stageFlags = vk::ShaderStageFlagBits::eFragment,
                                            .pImmutableSamplers = nullptr },
            vk::DescriptorSetLayoutBinding{ // Blend weights texture
                                            .binding = 1,
                                            .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                            .descriptorCount = 1,
                                            .stageFlags = vk::ShaderStageFlagBits::eFragment,
                                            .pImmutableSamplers = nullptr },
            vk::DescriptorSetLayoutBinding{ // Linear sampler
                                            .binding = 2,
                                            .descriptorType = vk::DescriptorType::eSampler,
                                            .descriptorCount = 1,
                                            .stageFlags = vk::ShaderStageFlagBits::eFragment,
                                            .pImmutableSamplers = nullptr },
            vk::DescriptorSetLayoutBinding{ // Point sampler
                                            .binding = 3,
                                            .descriptorType = vk::DescriptorType::eSampler,
                                            .descriptorCount = 1,
                                            .stageFlags = vk::ShaderStageFlagBits::eFragment,
                                            .pImmutableSamplers = nullptr },
        };

        const vk::DescriptorSetLayoutCreateInfo layoutInfo{ .bindingCount = static_cast<uint32_t>(bindings.size()),
                                                            .pBindings = bindings.data() };

        s_Data->SMAAResources.DescriptorSetLayouts.NeighbourhoodBlend =
            vk::raii::DescriptorSetLayout{ device, layoutInfo };
        context.SetObjectDebugName(s_Data->SMAAResources.DescriptorSetLayouts.NeighbourhoodBlend,
                                   "SMAA Neighbourhood Blend Set Layout");

        const std::array setLayouts = { *s_Data->SMAAResources.DescriptorSetLayouts.NeighbourhoodBlend };

        const vk::PipelineLayoutCreateInfo pipelineLayoutInfo{ .setLayoutCount =
                                                                   static_cast<uint32_t>(setLayouts.size()),
                                                               .pSetLayouts = setLayouts.data(),
                                                               .pushConstantRangeCount = 1,
                                                               .pPushConstantRanges = &pushConstantRange };

        s_Data->SMAAResources.NeighborhoodBlendingPipelineLayout =
            vk::raii::PipelineLayout{ device, pipelineLayoutInfo };
        context.SetObjectDebugName(s_Data->SMAAResources.NeighborhoodBlendingPipelineLayout,
                                   "SMAA Neighbourhood Blend Pipeline Layout");
    }
}

void Renderer::CreateSMAAImages(const uint32_t width, const uint32_t height)
{
    KBRAssert(s_Data->SMAAResources.EdgesImage.Format != vk::Format::eUndefined,
                    "SMAA edge detection image format has to be set before creating SMAA images!");
    KBRAssert(s_Data->SMAAResources.BlendImage.Format != vk::Format::eUndefined,
                    "SMAA blend image format has to be set before creating SMAA images!");

    auto& context = VulkanContext::Get();
    const auto& device = context.GetDevice();

    constexpr uint32_t mipLevels = 1;

    CreateImage(device,
                width,
                height,
                mipLevels,
                vk::SampleCountFlagBits::e1,
                s_Data->SMAAResources.EdgesImage.Format,
                vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
                vk::MemoryPropertyFlagBits::eDeviceLocal,
                s_Data->SMAAResources.EdgesImage.Image,
                s_Data->SMAAResources.EdgesImage.ImageMemory);

    context.SetObjectDebugName(s_Data->SMAAResources.EdgesImage.Image, "SMAA Edges Image");
    context.SetObjectDebugName(s_Data->SMAAResources.EdgesImage.ImageMemory, "SMAA Edges Image Memory");

    s_Data->SMAAResources.EdgesImage.ImageView = CreateImageView(device,
                                                                 s_Data->SMAAResources.EdgesImage.Image,
                                                                 s_Data->SMAAResources.EdgesImage.Format,
                                                                 vk::ImageAspectFlagBits::eColor,
                                                                 mipLevels);
    context.SetObjectDebugName(s_Data->SMAAResources.EdgesImage.ImageView, "SMAA Edges Image View");

    CreateImage(device,
                width,
                height,
                mipLevels,
                vk::SampleCountFlagBits::e1,
                s_Data->SMAAResources.BlendImage.Format,
                vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
                vk::MemoryPropertyFlagBits::eDeviceLocal,
                s_Data->SMAAResources.BlendImage.Image,
                s_Data->SMAAResources.BlendImage.ImageMemory);

    context.SetObjectDebugName(s_Data->SMAAResources.BlendImage.Image, "SMAA Blend Image");
    context.SetObjectDebugName(s_Data->SMAAResources.BlendImage.ImageMemory, "SMAA Blend Image Memory");

    s_Data->SMAAResources.BlendImage.ImageView = CreateImageView(device,
                                                                 s_Data->SMAAResources.BlendImage.Image,
                                                                 s_Data->SMAAResources.BlendImage.Format,
                                                                 vk::ImageAspectFlagBits::eColor,
                                                                 mipLevels);
    context.SetObjectDebugName(s_Data->SMAAResources.BlendImage.ImageView, "SMAA Blend Image View");
}

void Renderer::SetupSMAADescriptors()
{
    KBRAssert(s_Data->DescriptorPool != nullptr,
                    "Descriptor pool has to be created before setting up transparency descriptors");

    auto& context = VulkanContext::Get();
    const auto& device = context.GetDevice();

    const vk::DescriptorImageInfo linearSamplerInfo{
        .sampler = *s_Data->LinearSampler,
        .imageView = nullptr,
    };
    const vk::DescriptorImageInfo pointSamplerInfo{
        .sampler = *s_Data->PointSampler,
        .imageView = nullptr,
    };
    const vk::DescriptorImageInfo tonemappedImageInfo{ .sampler = *s_Data->LinearSampler,
                                                       .imageView = *s_Data->TonemappedImage.ImageView,
                                                       .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };

    for (uint32_t i = 0; i < VulkanContext::MaxFramesInFlight; i++) {
        // Edge Detection Set
        {
            const vk::DescriptorSetAllocateInfo allocInfo{
                .descriptorPool = *s_Data->DescriptorPool,
                .descriptorSetCount = 1,
                .pSetLayouts = &*s_Data->SMAAResources.DescriptorSetLayouts.EdgeDetection
            };
            s_Data->SMAAResources.DescriptorSets[i].EdgeDetection =
                std::move(device.allocateDescriptorSets(allocInfo).front());
            context.SetObjectDebugName(s_Data->SMAAResources.DescriptorSets[i].EdgeDetection,
                                       "SMAA Edge Detection Descriptor Set[" + std::to_string(i) + "]");

            const std::array write = {
                vk::WriteDescriptorSet{ .dstSet = *s_Data->SMAAResources.DescriptorSets[i].EdgeDetection,
                                        .dstBinding = 0,
                                        .dstArrayElement = 0,
                                        .descriptorCount = 1,
                                        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                        .pImageInfo = &tonemappedImageInfo },
                vk::WriteDescriptorSet{ .dstSet = *s_Data->SMAAResources.DescriptorSets[i].EdgeDetection,
                                        .dstBinding = 1,
                                        .dstArrayElement = 0,
                                        .descriptorCount = 1,
                                        .descriptorType = vk::DescriptorType::eSampler,
                                        .pImageInfo = &linearSamplerInfo },
                vk::WriteDescriptorSet{ .dstSet = *s_Data->SMAAResources.DescriptorSets[i].EdgeDetection,
                                        .dstBinding = 2,
                                        .dstArrayElement = 0,
                                        .descriptorCount = 1,
                                        .descriptorType = vk::DescriptorType::eSampler,
                                        .pImageInfo = &pointSamplerInfo }
            };
            device.updateDescriptorSets(write, {});
        }

        // Blend Weight Calculation Set
        {
            const vk::DescriptorSetAllocateInfo allocInfo{
                .descriptorPool = *s_Data->DescriptorPool,
                .descriptorSetCount = 1,
                .pSetLayouts = &*s_Data->SMAAResources.DescriptorSetLayouts.BlendWeight
            };
            s_Data->SMAAResources.DescriptorSets[i].BlendWeight =
                std::move(device.allocateDescriptorSets(allocInfo).front());
            context.SetObjectDebugName(s_Data->SMAAResources.DescriptorSets[i].BlendWeight,
                                       "SMAA Blend Weights Descriptor Set[" + std::to_string(i) + "]");

            const vk::DescriptorImageInfo edgesImageInfo{ .sampler = *s_Data->LinearSampler,
                                                          .imageView = *s_Data->SMAAResources.EdgesImage.ImageView,
                                                          .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };
            const vk::DescriptorImageInfo areaTextureInfo{ .sampler = *s_Data->LinearSampler,
                                                           .imageView = *s_Data->SMAAResources.AreaTexture.ImageView,
                                                           .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };
            const vk::DescriptorImageInfo searchTextureInfo{ .sampler = *s_Data->LinearSampler,
                                                             .imageView =
                                                                 *s_Data->SMAAResources.SearchTexture.ImageView,
                                                             .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };

            const std::array write = {
                vk::WriteDescriptorSet{ .dstSet = *s_Data->SMAAResources.DescriptorSets[i].BlendWeight,
                                        .dstBinding = 0,
                                        .dstArrayElement = 0,
                                        .descriptorCount = 1,
                                        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                        .pImageInfo = &edgesImageInfo },
                vk::WriteDescriptorSet{ .dstSet = *s_Data->SMAAResources.DescriptorSets[i].BlendWeight,
                                        .dstBinding = 1,
                                        .dstArrayElement = 0,
                                        .descriptorCount = 1,
                                        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                        .pImageInfo = &areaTextureInfo },
                vk::WriteDescriptorSet{ .dstSet = *s_Data->SMAAResources.DescriptorSets[i].BlendWeight,
                                        .dstBinding = 2,
                                        .dstArrayElement = 0,
                                        .descriptorCount = 1,
                                        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                        .pImageInfo = &searchTextureInfo },
                vk::WriteDescriptorSet{ .dstSet = *s_Data->SMAAResources.DescriptorSets[i].BlendWeight,
                                        .dstBinding = 3,
                                        .dstArrayElement = 0,
                                        .descriptorCount = 1,
                                        .descriptorType = vk::DescriptorType::eSampler,
                                        .pImageInfo = &linearSamplerInfo },
                vk::WriteDescriptorSet{ .dstSet = *s_Data->SMAAResources.DescriptorSets[i].BlendWeight,
                                        .dstBinding = 4,
                                        .dstArrayElement = 0,
                                        .descriptorCount = 1,
                                        .descriptorType = vk::DescriptorType::eSampler,
                                        .pImageInfo = &pointSamplerInfo }
            };
            device.updateDescriptorSets(write, {});
        }

        // Neighbourhood Blend Set
        {
            const vk::DescriptorSetAllocateInfo allocInfo{
                .descriptorPool = *s_Data->DescriptorPool,
                .descriptorSetCount = 1,
                .pSetLayouts = &*s_Data->SMAAResources.DescriptorSetLayouts.NeighbourhoodBlend
            };
            s_Data->SMAAResources.DescriptorSets[i].NeighbourhoodBlend =
                std::move(device.allocateDescriptorSets(allocInfo).front());
            context.SetObjectDebugName(s_Data->SMAAResources.DescriptorSets[i].NeighbourhoodBlend,
                                       "SMAA Neighbourhood Blend Descriptor Set[" + std::to_string(i) + "]");

            const vk::DescriptorImageInfo blendWeightsImageInfo{ .sampler = *s_Data->LinearSampler,
                                                                 .imageView =
                                                                     *s_Data->SMAAResources.BlendImage.ImageView,
                                                                 .imageLayout =
                                                                     vk::ImageLayout::eShaderReadOnlyOptimal };

            const std::array write = {
                vk::WriteDescriptorSet{ .dstSet = *s_Data->SMAAResources.DescriptorSets[i].NeighbourhoodBlend,
                                        .dstBinding = 0,
                                        .dstArrayElement = 0,
                                        .descriptorCount = 1,
                                        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                        .pImageInfo = &tonemappedImageInfo },
                vk::WriteDescriptorSet{ .dstSet = *s_Data->SMAAResources.DescriptorSets[i].NeighbourhoodBlend,
                                        .dstBinding = 1,
                                        .dstArrayElement = 0,
                                        .descriptorCount = 1,
                                        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                        .pImageInfo = &blendWeightsImageInfo },
                vk::WriteDescriptorSet{ .dstSet = *s_Data->SMAAResources.DescriptorSets[i].NeighbourhoodBlend,
                                        .dstBinding = 2,
                                        .dstArrayElement = 0,
                                        .descriptorCount = 1,
                                        .descriptorType = vk::DescriptorType::eSampler,
                                        .pImageInfo = &linearSamplerInfo },
                vk::WriteDescriptorSet{ .dstSet = *s_Data->SMAAResources.DescriptorSets[i].NeighbourhoodBlend,
                                        .dstBinding = 3,
                                        .dstArrayElement = 0,
                                        .descriptorCount = 1,
                                        .descriptorType = vk::DescriptorType::eSampler,
                                        .pImageInfo = &pointSamplerInfo }
            };
            device.updateDescriptorSets(write, {});
        }
    }
}
} // namespace Kerberos