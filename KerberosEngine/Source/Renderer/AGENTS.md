# Renderer context

Applies to `KerberosEngine/Source/VulkanContext*`,
`KerberosEngine/Source/Renderer/**`, `KerberosEngine/Source/Buffer*`,
`KerberosEngine/Source/Image*`, and `KerberosEditor/Assets/Shaders/**`.
This file also covers the particle system (see the "Particle system context"
section below).

## Ownership and initialization

`Application` owns the window and `VulkanContext`. `VulkanContext` is a
process-level singleton that owns the Vulkan instance, surface, physical and
logical devices, queues, allocator, swapchain, command pools/buffers, frame
synchronization, and ImGui Vulkan state.

`Renderer` is a static facade backed by one renderer-data allocation. It owns
renderer resources, descriptor allocators/managers, texture/material systems,
the active render graph, post-processing resources, upscaling, ray tracing,
grass, and particles. `VulkanContext` must be initialized before
`Renderer::Init`; shutdown must wait for GPU completion before releasing
renderer resources.

Relevant ownership is in `KerberosEngine/Source/Application.cpp`,
`VulkanContext.hpp/.cpp`, and `Renderer.cpp`.

## Frame and thread model

The main loop polls events, updates layers, builds ImGui, drains GPU upload
jobs, records/submits the frame, and presents. The Vulkan boundary is
single-threaded. Worker threads may enqueue main-thread functions and GPU
upload jobs, but renderer scene queuing is not generally thread-safe.

Scene rendering is queued before `VulkanContext::Draw()` records commands.
`Renderer::RecordQueuedSceneRender` consumes pending render state and resets
the current frame descriptor allocator. Descriptors allocated from a
frame-local allocator must not outlive that frame slot.

`RenderSceneEditor` takes an editor `Camera`; `RenderSceneRuntime` takes the
scene's `SceneCamera` plus the primary camera entity world transform. Runtime
view/projection matrices come from the `SceneCamera`, not from the editor
camera.

Uploads commonly use staging buffers and synchronous waits. This is safe but
can stall; preserve the existing render-thread assertions and exception
propagation when changing upload paths.

## Vulkan device and swapchain

The device requires Vulkan 1.3 and required graphics features/extensions.
Dedicated compute/transfer queues may be selected, while optional descriptor
buffer, mesh shader, shader object, ray query, and acceleration-structure
features are enabled when available.

Optional extensions are enabled whenever the physical device advertises them,
so subsystems that need one must gate on capability, not on the extension
being compiled in. `VulkanContext::IsExtensionActive`/`UseDescriptorBuffers`
answers that for the active device, and `SupportsMeshShaders`,
`SupportsShaderObjects`, and `SupportsRayTracing` also check the enabled
features. Creating a layout, pipeline, query pool, or buffer usage that belongs
to a disabled extension is invalid Vulkan (and aborts on drivers that lack the
entry point), so:

- grass (`GrassSystem::IsSupportedByDevice`) needs mesh shaders, shader objects,
  and descriptor buffers; otherwise `Init` logs and skips and `RecordDraw` is a
  no-op;
- task/mesh pipeline-statistics query pools are only created with mesh shaders,
  and the reset/resolve paths tolerate their absence;
- the ray-query PBR pipelines, the scene set's TLAS binding (7), the
  acceleration-structure pool entry, and the TLAS descriptor write only exist
  when ray tracing is supported, and `Renderer::IsUsingAccelerationStructures`
  reports false otherwise (the editor disables the toggle through
  `Renderer::IsRayTracingSupported`);
- vertex/index buffers only request
  `ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_KHR` when the acceleration
  structure extension is active.

The point/omni shadow path additionally requires
`VkPhysicalDeviceFeatures::imageCubeArray`, which is the core device feature
needed for the `CUBE_ARRAY` view sampled through `SamplerCubeArrayShadow`.
`VulkanContext` enables it for the active device; devices that do not
advertise it cannot create or sample the point-shadow cube array.

Swapchain format/present-mode selection and image-count rules live in
`VulkanContext.cpp`. Swapchain recreation rebuilds swapchain-owned views,
color, and depth resources, but does not recreate renderer-owned offscreen
images. Viewport/resource resizing is a separate renderer operation.

Do not assume queue-family ownership transfers are handled by the render
graph; the active graph generally uses ignored queue-family indices.

## Active render graph

The active implementation is `Kerberos::RenderGraph::Graph` in
`Renderer/Graph.hpp/.cpp`. It imports externally owned images/buffers,
records per-pass read/write usage, builds dependencies, topologically orders
passes, compiles synchronization2 barriers, and executes one graphics command
buffer with debug labels.

The renderer rebuilds/imports the graph each scene frame and tracks current
layouts on persistent renderer resources. Imported graph resources are
non-owning; their owning renderer objects handle destruction/recreation.
Layout state must be updated consistently after execution because it seeds
the next frame.

The active pass order is approximately:

1. skinning (skinned meshes; CPU animation evaluation happens earlier in
   `Scene::AnimationRuntime` and the skin-matrix upload happens in
   `AnimationSystem::BeginFrame`)
2. depth pre-pass
3. directional shadow cascades
4. omni (point light) shadow cubemaps
5. GTAO
6. opaque
7. particles
8. grass
9. transparent
10. transparency resolve
11. bloom
12. anti-aliasing
13. upscaling
14. tonemapping
15. presentation
16. mouse picking

The omni shadow pass is a separate graph pass after the directional
cascades. It writes the fixed `PointShadowMap` cube array, and the opaque and
transparent passes read it. The pass is registered when point shadows are
enabled, cube arrays are supported, and ray-query shadows are off; it may still
run with zero active casters. When it is omitted, the cube array stays in its
shader-read initialization layout and the timestamp fallback described below
keeps the timing queries complete.

The skinning pass is added before the depth pre-pass; animation has no graph
pass because pose evaluation happens on the CPU in `Scene::AnimationRuntime` and
the skin-matrix upload happens on the CPU in `AnimationSystem::BeginFrame`. The
skinning pass currently ends with an explicit
compute-write to vertex-read barrier because the SVB is bound directly by raster
passes and is not declared as a render-graph resource. The barrier's destination
stage includes `VERTEX_INPUT` (not only `VERTEX_SHADER`) so the
`VERTEX_ATTRIBUTE_READ` access is valid.

The grass pass is always registered so the pipeline-statistics query split
(`query 0` ends, `query 1` begins) stays balanced, but its mesh-statistics query
and the grass draw only run when grass is supported on the device.

## Debug line overlay

`DisplayPhysicsColliders` and `DisplayAIPerception` share one depth-tested line
pass recorded after transparency. `Renderer::RenderDebugLines` draws whichever
vertex sets are enabled through the existing line pipeline and per-frame line
buffer; `RenderStatistics::ColliderLineVertexCount` now reports the combined
debug-line count. Physics geometry comes from
`GetColliderLineVerticesFromScene`; AI perception geometry comes from
`GetAIPerceptionLineVerticesFromScene` and
`Renderer/AIPerceptionDebugHelpers.hpp`.

The AI overlay draws a ground-plane sight sector (`SightRadius`), a retain
sector (`LoseRadius`), FOV edge/centre rays, an eye-height marker, and a target
line from the blackboard (`TargetEntity`/`HasLineOfSight`) or an amber memory
line to `LastKnownPosition`. It is observation-only and never mutates AI state.
Line colours come from the per-vertex `LineVertex::Color` attribute; the shared
line shader already consumes it.

When FSR3 is active, the spatial anti-aliasing pass is skipped and FSR3
consumes the pre-AA HDR resolve so its internal temporal history receives the
original jittered samples. Native presentation continues to use the
anti-aliasing pass and composite image.

FSR3 receives the sub-pixel jitter through `UpscalerDispatchInfo`/the upscaler
dispatch; the scene projection carries that jitter for rendering, while the
motion-vector pass uses unjittered current/previous projections. Because the
motion buffer is jitter-free, FSR must not enable motion-vector jitter
cancellation, and pixels without prepass geometry (sky, transparent-only
areas) legitimately keep a zero motion vector. The projection already applies
the Vulkan Y flip, so both jitter axes use the same sign in the projection;
the jitter offset reported to FSR must always equal the resulting image
displacement. Keep these conventions in sync or the accumulated image shakes
every frame.

`RenderGraph.hpp/.cpp` is a legacy graph with different resource ownership,
semaphores, and queue submissions. Do not extend or mix it with `Graph`
without first proving it is still used.

## Lighting, exposure, and environment

`GlobalLighting` is three `float4`s, mirrored by the Slang `GlobalLighting`
struct: `sunDirection` (xyz direction, w = sun enabled), `sunColor` (rgb linear
color, w = sun illuminance in lux), and `lightingParams` (x = linear exposure
scale, y = environment intensity, z = wide point-shadow PCF toggle, w = RTAO
toggle). Keep the C++ static asserts and the Slang layout in sync. Output
transfer is fixed to exact sRGB; there is no runtime gamma control.

Lighting uses physical units. `DirectionalLight::Intensity` is lux and
`PointLight`/`SpotLight::Intensity` is candela, attenuated by inverse square
distance and the range window. Authoring colors are sRGB and are converted to
linear when uploaded (`Renderer::GetLightsFromScene` and the sun path in
`RecordQueuedSceneRender`). Scene serialization writes `IntensityUnit`; older
directional lights without that key are migrated from a relative multiplier to
lux on load. The renderer does not synthesize a sun when the scene has no
enabled `DirectionalLightComponent`; `Scene::GetSunlight` returns a disabled
zero-intensity light instead.

Exposure is EV100 plus exposure compensation:
`exposureScale = 1 / (1.2 * 2^(EV100 + compensation))`. The bloom extract pass
applies `exposureScale`, `tonemapping_resolve` applies it to the scene, and the
tonemap operators receive exposure 1 so exposure is applied exactly once. FSR3
receives the same value as `UpscalerDispatchInfo::preExposure`.

`EnvironmentComponent` selects the scene cubemap in the editor. `Renderer::
UpdateEnvironment` resolves the first valid `SkyboxTexture` handle, applies the
component's calibration `Intensity`, and regenerates the irradiance and
prefiltered cubemaps (`IsSkyboxDirty`) after waiting for the device to idle.
The bundled Pisa cube is only the fallback when no environment is authored; its
relative HDR values are calibrated with `DefaultEnvironmentIntensity`. The sky
shader and PBR ambient terms multiply by the same environment intensity.
Packed runtime cubemap artifacts are not implemented yet; the editor loads
`.kbrcubemap` sources directly through `RuntimeAssetLoader`.

## Point and omni shadows

Point lights use a fixed hardware-filtered cube-map array; there is no 2D
shadow atlas. `Renderer.cpp` owns `PointShadowMap`: a `CUBE_COMPATIBLE`
`D32_SFLOAT` image of `16 * 6 = 96` layers at `512 x 512`, a `CUBE_ARRAY` read
view for `SamplerCubeArrayShadow`, and one 2D view per face for rendering. The
comparison sampler is linear with `GreaterOrEqual` and an opaque-white border,
matching the CSM sampler; the shader treats a comparison result of 1 as
occluded and returns `1 - SampleCmp(...)` as visibility.

`GPULight` is 80 bytes and carries `ShadowSlot` (`-1` = no shadow),
`ShadowNear`, `ShadowFar`, and `ShadowCasterEntityID` (the owning light entity,
used to exclude its lamp/fixture mesh from its own cube map) after the existing
material block. Keep the C++ static asserts and the `lights.slang` layout in
sync. `ShadowSlot` selects the
cube-array layer; a light without a slot still illuminates.

`AssignOmniShadowCasters` runs before the light buffer upload. It builds the
main-camera frustum, rejects point-light spheres outside it, scores the
remaining lights with `intensity * range / distanceToCamera^2`, sorts by score,
clamps to `min(PointShadowMaxCasters, PointShadowMap::MaxCasters)`, and assigns
slots `0..N-1`. The active casters are copied into `PointShadowCasters` for the
render pass. Lights beyond the budget keep `ShadowSlot = -1` and illuminate
without shadows.

If point shadows are disabled, cube arrays are unsupported, or ray-query
shadows are enabled, the assignment clears the caster list and both
`RenderStatistics` counts, leaves every `ShadowSlot` at `-1`, and returns. The
omni graph pass is omitted in that case. Ray-query opaque lighting traces
point-light shadows itself, so raster transparent geometry does not receive
point-light shadows while ray-query shadows are enabled.

`RenderOmniShadowPass` runs as the "Omni Shadow Pass" graph pass. For each
caster it pre-filters `allObjects` once with a light-sphere vs. world-AABB test,
then for each of the six cube faces constructs the 90-degree perspective face
view (using the same face directions/up vectors as the skybox capture), builds
the face frustum, and draws only objects whose AABB intersects it. The CSM and
omni shadow passes both skip `RenderObject.CastShadows == false`; the static and
skinned mesh component flags are copied into `RenderObject.CastShadows`. The
pre-filter also skips the light's owning entity mesh (`ShadowCasterEntityID`)
and small meshes whose AABB contains the light, so an authored lamp/fixture mesh
cannot fill its own cube map with near inside faces and shadow the whole scene.
Transparent materials are skipped; alpha-masked materials use the shared global
texture set.
The pass clears each active face and renders with `shadowmap_omni` using
front-face culling and depth bias, pushing the face view-projection as a vertex
push constant.

The cube array is transitioned to shader-read once at creation, so it remains a
valid scene-descriptor target when the omni pass is skipped. When the pass runs,
the render graph transitions it to a depth attachment and back to shader-read.

The cube array is bound to scene-set binding `10` and sampled in `pbrtextured`
and `transparent` through `lights.slang::CalculatePointShadowVisibility`. The
helper reconstructs the zero-to-one perspective depth from
`ShadowNear`/`ShadowFar`, applies a distance-scaled normal offset, and uses
`SampleCmp` for hardware PCF. When `lightingParams.z > 0.5`, it widens the
filter with a 3x3 tangent-plane loop. `pbr_ray_query_shadows` already traces
point-light shadows and does not sample binding 10.

`OmniShadowBegin`/`OmniShadowEnd` are separate GPU timestamp queries and
resolve to `GPUTimings::OmniShadowPassMilliseconds`, independent of the
directional `ShadowPassMilliseconds`. Every timestamp query must be written
each frame: `vkGetQueryPoolResults` reports the entire pool as not ready if even
one query is unwritten. `WriteMissingGPUTimestamps` tracks written queries per
frame and writes paired zero-duration fallbacks for skipped passes (for example
anti-aliasing under FSR3, or an omitted omni pass). `RenderStatistics` exposes
`PointShadowCandidateCount` and `PointShadowCasterCount` for the debug panel,
which also has enable/max-caster/wide-PCF/depth-bias controls and a slot/face
preview.

Point/omni shadow invariants:

- The omni graph pass must import and transition `PointShadowMap.Image` over all
  96 layers; it must not import the CSM `ShadowMap.Image` with the omni range.
- The opaque pass and the transparent pass must declare reads of the
  point-shadow cube array. The transparent pass also samples the CSM map, so it
  must declare that read too.
- `PointShadowMaxCasters` is a user cap, not the allocation size; clamp it to
  `PointShadowMap::MaxCasters` before use.
- Clear `PointShadowCasters` and the candidate/caster statistics even when point
  shadows are disabled, unsupported, or superseded by ray-query shadows, and
  check the active condition in the pass as well. Omit the omni graph pass when
  it is inactive; the cube array's creation-time shader-read transition keeps
  the scene descriptor valid.
- Every GPU timestamp query must be written every frame. Skipped or conditional
  passes must still receive their timestamp pair through
  `WriteMissingGPUTimestamps`, or the whole timing readback becomes
  `eNotReady`.
- Shadow vertex shaders that sample the albedo alpha must declare `Pos`,
  `Normal`, `Tangent`, and `TexCoord` in order (or use explicit vertex
  locations) so `TexCoord` is location 3. `shadowmap_csm` and
  `shadowmap_omni` share this requirement.
- Assign casters after the `MaxLights` clamp so selected lights are actually in
  the uploaded light buffer.
- The debug preview descriptor lookup is
  `slot * PointShadowMap::FaceCount + face`; the slot/face selectors map
  directly onto the 96 per-face views.
- The point-shadow path requires `imageCubeArray` and a depth format with
  comparison sampling; gate resource creation on device support. Without cube
  arrays the renderer disables the pass and preview, leaves all shadow slots at
  `-1`, and binds the CSM depth view as a descriptor placeholder so the shared
  scene layout remains populated.

## Materials, alpha modes, and ambient occlusion

`Material::AlphaMode` mirrors glTF's `OPAQUE`/`MASK`/`BLEND` modes (default
`OPAQUE`) with `AlphaCutoff` default `0.5`. glTF materials import both fields;
authored `.kbrmat` files serialize them as `AlphaMode`/`AlphaCutoff`, and
legacy materials that relied on base-color alpha below one are loaded as
`BLEND`. `IsTransparent()` is true only for `BLEND`, so `MASK` and `OPAQUE`
both stay in the depth/shadow/opaque passes. The alpha mask is applied in
`pbrtextured`, `pbr_ray_query_shadows`, `depthprepass`, `shadowmap_csm`, and
`shadowmap_omni` using `ShouldDiscardAlpha` from `common.slang` with
`textureAlpha * baseColorFactor.a`; the shadow passes bind the global texture
set for this reason. `BLEND` materials render through the existing
weighted-blended OIT pass, where alpha is `textureAlpha * baseColorFactor.a`
and emissive/AO participate in the accumulated color.

Alpha mode and cutoff travel in `PerObject` after the 60-byte material block
(`alphaMode` at offset 256, `alphaCutoff` at 260, `entityID` at 264); keep the
C++ `PerObjectData` static asserts and the Slang `PerObject` layout in sync.

`lightingParams.w` selects the ambient-occlusion source for the ray-query PBR
path: `0` samples GTAO and `1` traces RTAO. `UseRayQueryAmbientOcclusion`
only takes effect when ray-query shadows and the acceleration structures are
active. The two sources are mutually exclusive: when RTAO is active the GTAO
compute pass is skipped and the AO image is cleared to neutral white, so no
frame pays for both. Non-ray-query PBR always samples GTAO.

`pbr_helpers.slang::ApplyNormalVarianceToRoughness` is the derivative-based
specular anti-aliasing path: it adds the screen-space variance of the shading
normal to the GGX roughness, which suppresses normal-map minification
sparkle until Toksvig-style normal-map filtering is added. Non-finite derivative
variance is ignored, because screen-space derivatives at primitive edges/helper
lanes can otherwise turn roughness (and all downstream lighting) into NaN. It is
not Toksvig (Toksvig folds a normal-length variance into roughness during
texture prefiltering).

Planned, not implemented:
- multi-scatter GGX / energy compensation for rough metals
- Toksvig-style normal-map variance in generated mip chains
- per-material normal scale and occlusion strength
- clearcoat/sheen material layers (add only when a project needs them)

## Resource lifetime and descriptors

Renderer resources must remain alive until all submitted frames that use them
complete. Deferred renderer actions currently use a device-wide wait to make
resource mutation safe; preserve that guarantee unless introducing explicit
per-resource synchronization.

`DescriptorAllocator` (persistent and per-frame slots) owns every descriptor
set it hands out: sets are allocated from its pool and ownership stays with
that pool, so `ShaderResourceSet::DescriptorSet` is a non-owning handle. Do not
wrap the result of `allocateDescriptorSets` in a temporary RAII handle that is
destroyed at the end of the statement; the set would be freed immediately.
`Reset()` destroys the pool contents, so a set from a frame slot is only valid
for that frame slot. `DescriptorAllocator` has two backends: descriptor buffers
(`VK_EXT_descriptor_buffer`, bump-allocated from a mapped buffer, `BufferAddress`
/`BufferOffset`/`MappedData`) and traditional descriptor sets (`DescriptorSet`, a
pool). GPUs without descriptor-buffer support take the traditional path, so both
paths must stay exercised.

Buffers use a mixture of device-local staging allocations and persistently
mapped host-visible uniform/storage buffers. Images/textures own their Vulkan
image, memory, view, and sampler state. Texture uploads transition through
transfer-destination to shader-read-only and commonly wait synchronously.

`TextureManager` owns the global update-after-bind texture table and sampler
objects. Texture slots are packed with sampler selection and exhaustion is an
explicit error. `Material::ResolveIndices` converts material texture handles
to global texture indices.

**Authored colors are sRGB.** Editor material/light/particle colors are stored
as normalized sRGB factors and converted to linear at the GPU upload boundary
(`Core/Color.hpp`). `Material::ColorEncoding` distinguishes authored `.kbrmat`
materials (`Srgb`) from native glTF factors and built-in physical metals
(`Linear`); `UpdatePerObjectUniformBuffer` converts only the former. glTF
`baseColorFactor`/`emissiveFactor` remain linear per the glTF 2.0 spec. Lights,
sun colors, and particle spawn colors are likewise authored sRGB and converted
before reaching shaders. Editor `ColorEdit` widgets use
`ImGuiColorEditFlags_Uint8` as a display choice only.

sRGB textures rely on hardware decode: color textures are stored with `*Srgb`
formats so sampling returns linear values, and the image view must carry the
same sRGB format as the image (`KTX2FormatSelector::Select`/`ToSrgb`). For ImGui
previews/icons, `Texture2D::GetImGuiImageView` supplies a non-sRGB view of the
same image (the image is created with `MUTABLE_FORMAT`), because ImGui renders
into a UNORM swapchain and would otherwise decode texels a second time. The
output chain stays linear HDR until `tonemapping_resolve` applies exposure,
ACES Fitted (default), clamping, and the exact sRGB encode into the
display-referred `FinalImage`; that image is sampled by ImGui and presented
through a UNORM swapchain, so there is exactly one decode and one encode. Do
not switch the swapchain to sRGB without removing that shader encode. sRGB
images must remain sampled-only (no storage usage).

Bloom extraction runs in exposed units with a luminance-based prefilter and a
default intensity of 0.05; keep the threshold/knee/max brightness in exposed
units when changing the tone-mapping exposure. `ColorImage.a` is still the bloom
mask (opaque geometry = 1, sky/particles = 0) rather than material opacity; a
dedicated mask attachment is future work. FXAA derives tone-compressed luma
itself. SMAA edge detection receives the scene exposure through its push
constants and tone-compresses edge samples; its neighborhood blend remains in
linear HDR. TAA is intentionally absent from the UI until implemented. The
`TonemappedImage` intermediate was removed; `FinalImage` is display-referred,
not linear HDR.

`Texture2D`/`TextureCube` file-loading constructors accept only `.ktx2`.
KTX1 and raster conversion is the asset pipeline's responsibility (see
`TexturePipelineImporter`), not the loader's; do not reintroduce conversion in
the loaders.

KTX2 loading uses the embedded `vkFormat` as the source of truth, but
`KTX2Loader::ResolveVulkanFormat` also reinterprets a UNORM format as sRGB when
the data format descriptor declares an sRGB transfer function. This covers
cubemaps/textures converted from PNG that carry UNORM plus sRGB metadata; do not
skip that check when adding another KTX2 entry point.

Textures upload every mip level present in the KTX2 file. When a texture has only
the base level, `MipmapGenerator` builds a full chain with `vkCmdBlitImage`
(linear filtering, one blit per level across all array layers) if the format
supports blit source/destination and linear filtering; otherwise the texture
stays at one level. The image must include
`TRANSFER_SRC` usage and the generator leaves every level in `TRANSFER_SRC` for
the caller's final transition to `SHADER_READ_ONLY_OPTIMAL`. Compressed formats
are uploaded as authored and are not generated.

`TextureSpecification` supports 8-bit UNORM/sRGB, 16-bit UNORM (`R16`/`RG16`/
`RGB16`/`RGBA16`), and `RGBA32F`. HDR `.hdr` sources load as `RGBA32F`; 16-bit
raster sources keep 16-bit precision. Vulkan has no 16-bit UNORM+sRGB format, so
16-bit color sources are linearized on import and marked with
`TextureSpecification::SourceSrgb` (also persisted as the KTX2
`KBR_SourceSrgb` key) for editor validation; the shader receives linear values.
KTX2 uploads are limited to blit-compatible formats for generated mips, so
compressed KTX2 inputs should carry their own chain.

Dynamic uniform offsets must respect
`minUniformBufferOffsetAlignment`; current per-object allocation also has a
capacity limit. Graph image subresource ranges and overlapping layout
declarations must resolve to compatible mip/layer counts and layouts.

## Shaders and working directory

Runtime shader loading resolves source and cache paths through
`Core/EnginePaths` (`EnginePaths::Shaders()` and `EnginePaths::ShaderCache()`),
not the process working directory. `EnginePaths` is initialized by
`Application` from the engine content root (defaulting to the working
directory, overridable via `ApplicationSpecification::EngineRoot`). Shader
compilation compiles Slang to SPIR-V 1.6, caches `.spv`, reflects resources,
and supports recompilation. Tests may request a hidden GLFW window through
`ApplicationSpecification::Headless`; this still creates the normal Vulkan
context and renderer and is not a software or no-GPU mode.

Cache freshness is dependency-aware. `SlangCompiler::CompileToSpirv` returns a
`CompiledShader` carrying the transitive dependency list reported by
`IModule::getDependencyFileCount`/`getDependencyFilePath`: the entry source,
transitively `#include`d files, and imported modules (Slang records module
imports as file dependencies). `ShaderCache` writes `<name>.spv.deps` beside
each artifact with a manifest format version, the Slang build tag, and
dependency paths relative to `EnginePaths::Shaders()` when possible. `Shader`
and `GrassSystem` only load a cached artifact when `ShaderCache::IsFresh`
reports every dependency unchanged and the cached SPIR-V has a valid header.
Artifacts and manifests are written to temp files and renamed, so a partial
write can never be observed as fresh. Changing `common.slang`,
`pbr_helpers.slang`, `lights.slang`, `ao.slang`, `particles.slang`,
`tonemapping.slang`, or `SMAA.hlsl` therefore recompiles exactly the shaders
that depend on it, both at startup and through `Shader::Recompile()`, which
early-outs for fresh shaders and rewrites the on-disk cache after a successful
compile. `GrassSystem` caches its shader-object stages under `grass_task`,
`grass_mesh`, and `grass_fragment` keys. `shadowmap_omni` is the point-shadow
depth-only shader; its `shadowmap_omni.spv` artifact is listed in
`compile_shaders.bat` alongside `shadowmap_csm.spv`.

Shader source/build mapping is duplicated between runtime shader usage and
`KerberosEditor/Assets/Shaders/compile_shaders.bat`. Update both when adding
or renaming shaders. `compile_shaders.bat` writes `Assets/Cache/Shaders/*.spv`
while Debug builds read `Assets/Cache/Shaders/Debug`; because batch-produced
artifacts have no `.deps` manifest, the first runtime load recompiles them once
and writes the manifest.

## Validation and diagnostics

Use:

```powershell
cmake --preset vs2026
cmake --build --preset vs2026-debug
```

Compile shaders from `KerberosEditor\Assets\Shaders`:

```powershell
.\compile_shaders.bat
```

Vulkan validation, synchronization validation, debug labels, shader printf,
and Tracy zones are important diagnostics. For renderer changes, validate
swapchain/viewport resize, frame synchronization, graph barriers/layouts,
descriptor lifetime, and shader compilation. For point/omni shadows, validate
0, 1, 16, and 17+ candidates, camera-frustum light culling, per-face mesh
culling, a point light authored inside its lamp/fixture mesh, alpha-masked
casters, slot reassignment, the enable toggle, mesh `CastShadows` toggles,
ray-query shadow mode, wide PCF, depth bias, the slot/face preview, and the
separate OmniShadow GPU timing line; run Vulkan validation and synchronization
validation to catch cube-array layout or descriptor mistakes. Validate GPU timings with
FSR3 on and off: every timing line must remain valid, and skipped anti-aliasing
or omni-shadow passes must report a near-zero duration rather than invalidating
the whole query pool. For the AI perception overlay,
validate Edit/Simulate/Play, FOV changes, 360-degree FOV, sight/lose radii,
occlusion by depth-tested geometry, many simultaneous agents, and toggling the
overlay off. Keep this file updated whenever renderer ownership, pass order,
synchronization, resource lifetime, or validation procedures change.

# Animation and skinning context

## Ownership

`Scene::AnimationRuntime` (`Scene/AnimationRuntime.*`) owns the authoritative CPU
animation state: per-skeleton-instance pose data, resolved clips, playback
clocks, pose versions, and the future marker/event/ragdoll/IK state. It is a
value member of `Scene`, so every scene owns its own runtime and the renderer
never drives playback.

`AnimationSystem` (`Renderer/Animation/AnimationSystem.*`) is the GPU upload
bridge. It owns one host-visible skin-matrix buffer per frame in flight, one GPU
slice per skeleton instance, and publishes a `SkinningFrameSnapshot`
(per-instance `SkeletonFrame` records). `BeginFrame(scene, frameIndex)` reads
`scene->GetAnimationRuntime().GetInstances()`, uploads changed poses for that
frame slot, and never advances time or evaluates animation. The snapshot buffer
field mirrors the current frame's buffer, but `SkinningSystem` binds its own
per-frame descriptor set.

`AnimationEvaluator` (`Renderer/Animation/AnimationEvaluator.*`) is the pure CPU
pose layer: `BuildClipTrackCache` maps a clip to a skeleton, `SampleTrack`
implements `STEP`/`LINEAR`/`CUBICSPLINE`, `EvaluatePose` produces local TRS,
and `ComputeGlobalAndSkinMatrices` derives globals and skin matrices.
`EvaluatePoseLocal`/`EvaluateRestPoseLocal` produce local-only poses for
composition, `BlendPose` blends two local poses (translation/scale mix,
shortest-path slerp, dominant-pose fallback on joint-count mismatch),
`AccumulatePose` is the normalized weighted average used for layered
composition, and `ComputeCrossFadeWeight` owns elapsed/duration clamping.
`AdvancePlaybackTime` owns loop/clamp/stop semantics. Keep it Vulkan-free so it
stays unit-testable.

`AnimationClip` carries sorted `AnimationMarker` and `AnimationEvent` data
loaded from the native clip. Events carry an optional list of named typed
`AnimationEventParameter` values (`Bool`, `Int`, `Float`, `Vector3`, `String`,
`Asset`); the engine does not interpret the names, games do. `AnimationEvaluator`
exposes pure helpers for playback spans, point-event crossing, wrapped duration
windows, and `MinTriggerWeight` filtering. `AnimationRuntime::AdvanceClocks`
collects `DispatchedAnimationEvent` records; `Scene::DispatchAnimationEvents`
validates and dispatches them before scripts in Play mode. The managed
`AnimationEvent` exposes the parameter list plus `GetBool`/`GetInt`/`GetFloat`/
`GetVector3`/`GetString`/`GetAsset` helpers. Duration events use
`Begin`/`Tick`/`End`; point events use `Point`. Reverse playback suppresses
events, and `AnimatorComponent::PlaybackRevision` suppresses point events across
time jumps. Active duration windows are closed with `End` when playback is
interrupted by a clip switch, revision jump, stop, skeleton replacement, reverse
playback, or natural clip end; each clip playback state keeps its own clip
reference so cleanup can still be delivered after a reload.

Cross-fades keep a `Current` and `Previous` `ClipPlaybackState`, each with its
own clock, weight, and duration-event windows. The incoming clip's weight rises
from 0 to 1 while the outgoing clip's weight falls; point and duration events are
filtered by that contributing weight, and the outgoing clip stops emitting once
its weight reaches zero. The fade completes when the incoming weight reaches 1,
which closes any remaining outgoing windows and drops the previous state.
Interrupting an in-flight cross-fade snapshots the current composed pose and
starts the new fade from that snapshot instead of the old raw clip. Direct
`Play`/`SetActiveClip` changes snap; only `CrossFade` blends. Requests are
delivered through the transient `AnimatorComponent::PendingCrossFadeClip`/
`PendingCrossFadeDuration`/`PendingCrossFadeRestart` fields and consumed by the
runtime. Timeline authoring lives in the editor's Animation Editor panel, which
edits the source `.meta` markers/events/parameters and reimports the clip on
save; the runtime itself never edits authored event data.

Entities use `SkeletonComponent` for the skeleton instance and joint list,
`JointComponent` for joint ordering, `SkinnedMeshComponent` for the mesh /
skeleton-entity link, `AnimatorComponent` for the skeleton-entity reference,
clip handles, and playback state, and `SocketComponent` to attach an entity to
a named joint.

## Sockets and pose authority

Each `AnimationInstance` carries `AnimationPose.Local` (the composed clip layer)
and `FinalPose` (the authoritative final pose after physics/IK). Consumers read
`FinalPose`; `AnimationPose` is retained for cross-fade snapshots and future
ragdoll blending. The CPU `SkeletonPose` remains authoritative. Pose publication
writes derived entity transforms, never authored local TRS, so `JointComponent`
entities stay rest-authored. `AnimationRuntime::UpdateJointTransforms` writes each joint
entity's `WorldTransform` from the pose's global matrices and then propagates
that world transform through non-skeletal descendants such as weapons, props, or
garment pieces parented under a joint. The propagation skips nested joints,
skinned meshes, and sockets because those have their own drivers. The pose's
global space is anchored to a skinned mesh bound to the skeleton when one
exists; otherwise the skeleton entity's world is used after removing its rest
transform when the skeleton entity is itself a joint. This is what keeps rigid
attachments in the hand of an animated character instead of leaving them at the
bind-pose location.

A socket is a derived attachment, not a second animation source:
`SocketComponent` stores the target skeleton entity, joint name, and local
offset, while `AnimationRuntime::UpdateSockets` resolves the joint through
`Skeleton::GetJointIndex` and writes the attached entity's `WorldTransform`
from the published joint world transform times `localOffset`. Authored
translation/rotation/scale are never overwritten; child transforms are
recalculated from the socket world transform. Joint and socket transforms update
after each pose evaluation and again in `FinalizePose`, so event handlers and
scripts see the current attachment before physics and the renderer sees the
final post-physics pose. Socket-derived transforms currently update rendering
and hierarchy only; moving a physics body with a socket-attached entity requires
an explicit kinematic body update. Kinematic rigid bodies are moved from their
`TransformComponent` world transform immediately before each Jolt step, while
only dynamic bodies are read back into entity transforms. Collider components
expose `IsTrigger` and `Enabled`, and the C# API supports body type, mass,
gravity, friction, restitution, angular velocity, collider shape/offset, and
trigger/enabled changes. Changing these marks the owning rigid body dirty; the
physics system rebuilds the body on the next update. The physics facade also
exposes standalone body creation, motion/transform/velocity access, swing-twist
constraints (used by the ragdoll), and one-shot `OverlapBox`/`OverlapSphere`
queries. Overlap queries return unique entities with contact point/normal, do
not require a collider component, and are exposed to C# as
`Physics.OverlapBox`/`Physics.OverlapSphere`; the entity-relative overloads use
the entity's world transform, which makes them suitable for socket-driven
weapon hitboxes.

## Ragdoll physics

`Scene::RagdollSystem` (`Scene/RagdollSystem.*`) owns the runtime bodies and
constraints for each `RagdollComponent`. The component stores only authored
setup (skeleton entity, blend duration, start-in-ragdoll, joint radius scale);
the system owns body/constraint handles, per-joint offsets, and blend state.

Each joint gets one proxy body: a capsule spanning the parent-to-joint segment
(or a sphere for a root or zero-length joint). The joint transform relative to
its proxy body is captured at creation, so a rigid proxy does not stretch when
the animation stretches a bone, but its joint end still tracks the pose. While
`Animated`, the proxies are kinematic: `RagdollSystem::PreparePhysics` computes
target body transforms from the authoritative `FinalPose`, moves the proxies,
and records those transforms for velocity handoff.

Enabling the ragdoll creates swing-twist constraints from the current pose,
transfers linear/angular velocities from the previous animated frame, switches
the proxies to dynamic, and ramps `BlendWeight` from 0 to 1.
`ReadPhysicsPose` runs after `PhysicsSystem::Update`, reads the body
transforms, converts them from world space into the skeleton's model space
through `AnimationRuntime::TryGetSkeletonWorldTransform`, builds a local joint
pose, and calls `AnimationRuntime::ApplyRagdollPose`. That blends the physics
local pose with `AnimationPose` using `BlendPose` (the weight is the physics
contribution), writes the result into `FinalPose`, recomputes globals/skin
matrices, and bumps `PoseVersion` only when the physics pose or weight actually
changed. Disabling the ragdoll ramps the weight back to 0 while physics still
drives the pose, then destroys the constraints, returns the proxies to
kinematic motion, and snaps them to the animation pose.

`FinalizePose` runs after the ragdoll readback, so joint entities and sockets
are published from the ragdoll-blended `FinalPose`. The skeleton pose therefore
stays authoritative: physics bodies are proxies, and `JointComponent` entity
transforms are never the source of truth.

The Play/Simulate frame order is: calculate transforms -> animation
clocks/pose -> events -> scripts -> post-script pose ->
`RagdollSystem::PreparePhysics` -> `PhysicsSystem::Update` ->
`RagdollSystem::ReadPhysicsPose` -> `AnimationRuntime::FinalizePose` -> queue
render. Edit mode does not create or simulate ragdoll bodies.

Ragdoll bodies use a shared Jolt `GroupFilterTable` with 256 subgroups and all
intra-group pairs disabled. Each ragdoll uses a unique collision group, so its
own bones do not collide but separate ragdolls still interact with each other
and with ECS bodies. Ragdoll bodies carry the ragdoll entity UUID as Jolt user
data, so raycasts and collision events attribute hits to the ragdoll entity.

Animation clocks and events keep advancing while a ragdoll is active so the
clip can resume during recovery. Gameplay scripts can inspect
`RagdollComponent::IsRagdoll`/`State` or the blend weight to ignore events that
should not fire while physics dominates; duration windows are not force-closed
merely by entering the ragdoll state.

Ragdoll constraints are swing-twist limits selected heuristically from joint
names (neck/head, shoulder/hip, elbow/knee, otherwise a default). There are no
authored per-joint limits yet, no world anchor for the root (a ragdoll falls
freely), no contact-driven hit reactions, no joint motors/IK recovery, and no
debug proxy drawing. Ragdoll bodies live in world space, so moving the skeleton
entity while it is ragdolling shifts the simulated pose relative to the model.
The runtime preallocates per-joint scratch buffers, avoids
per-frame matrix inversions in readback, creates constraints only while the
ragdoll is dynamic, and skips pose-version bumps when a sleeping ragdoll is
unchanged.

The animator replaces the old single-clip `AnimationComponent`; legacy serialized
animation components are migrated on load. `AnimatorComponent::LoadedClips` is
transient: `AnimationRuntime` resolves `AnimationClips` through the active asset
manager when the handle list changes or the cached artifact was evicted by a
reimport.

## Evaluation and versioning

`Scene::OnUpdateEditor`/`OnUpdateSimulation`/`OnUpdateRuntime` own the frame
order. Play/Simulate evaluate animation, run scripts, prepare ragdoll proxies,
step physics, read the ragdoll pose back, then call `FinalizePose`; only then is
the render queued. The runtime resolves the animator that drives each skeleton
(same entity first, then `SkeletonEntity`, with a warning when several animators
target one skeleton). A skeleton with no animator or no resolvable active clip
stays in its rest pose.

`OnUpdateRuntime` advances clocks and collects events, evaluates a provisional
pose, dispatches animation events, then runs scripts. It evaluates again after
scripts when playback state changed, so `Play`/`Stop`/`SetTime`/`CrossFade` from
C# applies before physics and rendering. A cross-fade request made by a script
is applied by the post-script evaluation; its fade elapsed time starts advancing
on the next clock update. `FinalizePose` publishes the already-composed
`FinalPose` to joint entities and sockets; ragdoll readback happens just before
it, and future IK/root motion must write into `FinalPose` and bump
`PoseVersion` before this call. Events are dispatched to
scripts on the animator entity through `Entity.OnAnimationEvent`; Edit and
Simulate clear pending events without invoking gameplay scripts. Runtime
dispatch happens once before scripts and again after the post-script pose
evaluation so interruption cleanup is not delayed.

Runtime playback state is authored through `AnimatorComponent::PlayOnStart`.
`Scene::Copy` resets each animator's time, clears resolved clips, and sets
`IsPlaying` from `PlayOnStart`; `Scene::StartAnimators` applies the same rule on
`OnRuntimeStart`/`OnSimulationStart`, and `Scene::StopAnimators` resets playback
on stop. Scripts can override the state after startup.

`AnimationInstance::PoseVersion` is bumped only when the final pose inputs
change (current/previous clip, either clock, fade weight, animator, or first
evaluation). Each GPU instance tracks the last version uploaded into each frame
slot, so a paused instance still initializes a frame slot that has not seen the
current pose. `SkinningSystem` tracks the same per-frame version and re-dispatches
only when its slot is stale. Physics/IK changes made in `FinalizePose` must bump
`PoseVersion`. Renderer `BeginFrame` must run after `FinalizePose`, or skinning
will use a stale pose.

`SkinningSystem` (`Renderer/Animation/SkinningSystem.*`) owns the global source
vertex/joint/weight/index buffers, the shared vertex buffer (SVB), the SVB
region allocator, one skinning descriptor set per frame in flight, and the
skinning compute pipeline. It consumes the snapshot from `AnimationSystem`; it
does not resolve or mutate skeleton assets.

`Renderer` owns both systems as value members, calls `BeginFrame` in the order
Animation → Skinning with the current `frameIndex`, adds the skinning pass
before the depth pre-pass, and resets both when the rendered scene pointer
changes.

`RayTracingSceneCache` and physics integration are not wired to the SVB or the
CPU pose yet.

## Buffers and shader contract

`skinning.slang` bindings:

- 0: `StructuredBuffer<InputVertex>` source vertices (48 B: position, normal,
  tangent, UV; same layout as `Renderer::Vertex`)
- 1: `RWStructuredBuffer<OutputVertex>` shared vertex buffer (same 48 B layout)
- 2: `StructuredBuffer<BoneMatrix>` skin matrices, owned by `AnimationSystem`
- 3: `StructuredBuffer<uint4>` joint indices
- 4: `StructuredBuffer<float4>` joint weights

`BoneMatrix` is three explicit `float4` rows (48 B). `SkinningPushConstant`
carries `sourceFirst`, `outputFirst`, `vertexCount`, `outputByteOffset`,
`firstSkinMatrix`, and `jointCount`. The shader indexes source data and the
skin matrix buffer with those offsets; do not bind per-mesh slices at offset 0.
Weights are normalized defensively in the shader.

The SVB stores model-space vertices. Raster uses the existing per-object model
matrix; RT and physics, when added, must apply the instance transform to the
SVB rather than expecting world-space output.

## Current animation limitations

- Pose evaluation and skin-matrix generation are CPU-only. There is no
  evaluation/precompute compute pass and no GPU pose path yet.
- Two-clip cross-fades, local-pose composition, ragdoll two-way pose
  synchronization, event parameter authoring, and the editor event timeline are
  implemented. Additive layers, root motion, authored per-joint ragdoll limits,
  and joint motors are not. Marker/event data round-trips from `.meta` through
  the native clip into `AnimationClip`, and point/duration events dispatch to
  scripts with per-clip fade weights and typed parameters. `FireInEditor`
  preview dispatch remains future work.
- Ragdoll proxies are rigid capsules/spheres created from the pose at the time
  the ragdoll is first initialized. They do not rescale with bone length, and
  the root has no anchor, so an enabled ragdoll falls freely.
- `FinalizePose` publishes `FinalPose` and does not itself modify it; future IK
  and root motion must write into `FinalPose` and bump `PoseVersion` before it.
- Only direct per-mesh dispatches are recorded; no indirect dispatch or
  per-frame job buffer exists.
- Skeleton skin-matrix slices and SVB regions are bump-allocated and only
  returned by `Reset()`; there is no per-entity free list yet.
- The SVB and index buffer do not yet request
  `ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_KHR`; add it when skinned BLAS
  support is implemented.
- Skinned meshes are not added to the TLAS, and acceleration structures are
  still built before skinning.
- Gameplay/physics should consume `SkeletonPose`/skin matrices, not the SVB.

## Validation

Build and compile shaders:

```powershell
cmake --preset vs2026
cmake --build --preset vs2026-debug
cd KerberosEditor\Assets\Shaders
.\compile_shaders.bat
```

The editor hierarchy window context menu has "Create Skinned Test Mesh", which
creates a procedural two-bone strip for validating the skinning path without
glTF import. Validate that it appears in depth/shadow/opaque passes, that resize
and scene switching do not leave stale instances, and that Vulkan validation
reports no buffer synchronization errors.

`Tests/AnimationEvaluatorTests.cpp` covers rest poses, step/linear/cubic
sampling, shortest-path rotation slerp, out-of-range tracks, loop wrapping,
reverse playback, and zero-duration playback. For the GPU path, validate
CesiumMan in Edit/Simulate/Play: play, pause, scrub, loop, reverse, clip
switching, two instances sharing one clip, scene switch, and two frames in
flight under synchronization validation.

# Particle system context

Applies to `KerberosEngine/Source/Renderer/ParticleSystem*`,
`KerberosEngine/Source/Scene/**/*Particle*`,
`KerberosEditor/Assets/Shaders/particle*`,
`KerberosEditor/Source/Windows/HierarchyPanel.cpp`, and
`KerberosEngine/Source/Serialization/SceneSerializer.cpp`.

## Architecture

Particles are one global persistent GPU simulation owned by
`KerberosEngine/Source/Renderer/ParticleSystem.hpp/.cpp`. The ECS
`ParticleEmitterComponent` stores authoring parameters and a transient CPU
spawn accumulator; individual particles exist only in GPU buffers.

The system owns a fixed pool (currently one million particles), a dead-index
list, two ping-pong active-index lists, counters, per-frame spawn/frame-data
buffers, and per-frame indirect dispatch/draw buffers. The pool is not
scene-local and has no explicit scene-switch reset path.

## CPU update and GPU sequence

`ParticleSystem::UpdateParticles` scans scene entities containing
`ParticleEmitterComponent` and `TransformComponent`, skips inactive emitters,
accumulates `SpawnRate * deltaTime`, emits whole-particle spawn requests, and
retains the fractional remainder. It uploads emitter requests and frame data;
it does not read particle state back from the GPU.

The recorded GPU sequence must remain ordered:

1. emit requests from the dead list into the current active list
2. prepare/reset next-active counters and generate indirect dispatch
3. simulate, integrate, kill, and compact survivors into the opposite list
4. finalize counts, toggle active-list index, and generate indirect draw
5. draw the published active list

The compute/graphics barriers in `ParticleSystem.cpp` and the active renderer
graph pass ordering are part of this contract.

## C++/shader contracts

`GPUParticle`, `SpawnRequest`, and `Counters` CPU layouts must remain binary
compatible with `Assets/Shaders/particles.slang` and the stage shaders.
Synchronize field order, scalar widths, alignment, and padding whenever either
side changes.

The particle capacity is duplicated in C++ and `particle_emit.slang`; keep
both values synchronized or replace the duplication with a shared/generated
constant. Counters have distinct meanings: dead count, current active count,
next active count, and active-list selector. The list selector toggles only
after compaction.

Particle rendering is a procedural billboard draw with no vertex buffer.
Depth testing is enabled, depth writes are disabled, and blending is additive.
The particle pass reads depth and writes color after opaque rendering.

## Textures and shader behavior

Emitters currently resolve texture information on the CPU for frame aspect,
but the descriptor path binds one particle texture and all requests use
texture index zero. Multiple simultaneous emitters with different textures
therefore are not independently represented; the last selected spawning
texture wins. Do not claim bindless/multi-texture support unless that path is
actually implemented.

SubUV grid dimensions are clamped in the draw shader, but CPU aspect
calculation can still see invalid zero/negative values. Validate serialized
and programmatic values before division. Frame-aspect correction and depth
softening are currently computed/populated but visually inactive because
their shader applications are commented out. Preserve the intentional
alpha-zero bloom-mask behavior unless changing the rendering design.

## ECS/editor/serialization

The component is added and edited through
`KerberosEditor/Source/Windows/HierarchyPanel.cpp`. Its active state, spawn
rate, lifetime, velocity/acceleration ranges, colors, sizes, SubUV grid, and
texture handle are exposed there. Texture drag/drop must validate a
`Texture2D` handle and resolve it through the active asset manager. Asset
validity includes a registered, existing imported artifact; a valid handle
must not resolve to a null texture.

`SceneSerializer.cpp` serializes/deserializes authored emitter fields.
`spawnAccumulator` is transient and should reset on construction/deserialization.
The scene component-added callback is intentionally empty; particle setup is
driven by the renderer scan.

The accumulator is mutated during renderer command recording and must not be
edited concurrently by the editor or another scene-update thread.

## Safety and common pitfalls

- Bound CPU spawn-request writes by the actual request buffer capacity.
- Keep maximum particle capacity synchronized between C++ and Slang.
- Ensure `maxLife > 0` before shader age interpolation/division.
- Keep lifetime ranges ordered and sane even for serialized/programmatic data.
- Avoid scene changes leaving particles from a previous scene unless an
  explicit reset policy is introduced.
- Preserve dead/active/next-active atomic list invariants.
- Keep particle pass depth layout and graph declarations compatible.

## Validation

Build and compile shaders:

```powershell
cmake --preset vs2026
cmake --build --preset vs2026-debug
cd KerberosEditor\Assets\Shaders
.\compile_shaders.bat
```

Particle shader artifacts are `particle_emit.spv`,
`particle_prepare_simulation.spv`, `particle_simulate.spv`,
`particle_finalize_simulation.spv`, and `particle_draw.spv`. Validate
component serialization, paused/runtime timing, multiple emitters, texture
selection, scene switching, resize, Vulkan synchronization, and GPU validation
messages. Update this file whenever particle data layout, simulation order,
resource ownership, editor controls, or shader behavior changes.
