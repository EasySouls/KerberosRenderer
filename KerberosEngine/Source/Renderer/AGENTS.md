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
3. shadows
4. GTAO
5. opaque
6. particles
7. grass
8. transparent
9. transparency resolve
10. bloom
11. anti-aliasing
12. upscaling
13. tonemapping
14. presentation
15. mouse picking

The skinning pass is added before the depth pre-pass; animation has no graph
pass because pose evaluation happens on the CPU in `Scene::AnimationRuntime` and
the skin-matrix upload happens on the CPU in `AnimationSystem::BeginFrame`. The
skinning pass currently ends with an explicit
compute-write to vertex-read barrier because the SVB is bound directly by raster
passes and is not declared as a render-graph resource.

The grass pass is always registered so the pipeline-statistics query split
(`query 0` ends, `query 1` begins) stays balanced, but its mesh-statistics query
and the grass draw only run when grass is supported on the device.

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

sRGB textures rely on hardware decode: color textures are stored with `*Srgb`
formats so sampling returns linear values, and the image view must carry the
same sRGB format as the image (`KTX2FormatSelector::Select`/`ToSrgb`). The
output chain stays linear HDR until `tonemapping_resolve` applies manual gamma
into a UNORM swapchain, so there is exactly one decode and one encode; do not
switch the swapchain to sRGB without removing that shader gamma. sRGB images
must remain sampled-only (no storage usage).

`Texture2D`/`TextureCube` file-loading constructors accept only `.ktx2`.
KTX1 and raster conversion is the asset pipeline's responsibility (see
`TexturePipelineImporter`), not the loader's; do not reintroduce conversion in
the loaders.

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

Shader source/build mapping is duplicated between runtime shader usage and
`KerberosEditor/Assets/Shaders/compile_shaders.bat`. Update both when adding
or renaming shaders.

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
descriptor lifetime, and shader compilation. Keep this file updated whenever
renderer ownership, pass order, synchronization, resource lifetime, or
validation procedures change.

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
`AdvancePlaybackTime` owns loop/clamp/stop semantics. Keep it Vulkan-free so it
stays unit-testable.

`AnimationClip` now carries sorted `AnimationMarker` and `AnimationEvent` data
loaded from the native clip. The runtime exposes marker queries and the
`PendingDispatch` surface, but crossing detection, duration windows, event
dispatch, editor timeline authoring, and cross-fade weight filtering are not
implemented yet.

Entities use `SkeletonComponent` for the skeleton instance and joint list,
`JointComponent` for joint ordering, `SkinnedMeshComponent` for the mesh /
skeleton-entity link, and `AnimatorComponent` for the skeleton-entity reference,
clip handles, and playback state. The animator replaces the old single-clip
`AnimationComponent`; legacy serialized animation components are migrated on
load. `AnimatorComponent::LoadedClips` is transient: `AnimationRuntime` resolves
`AnimationClips` through the active asset manager when the handle list changes
or the cached artifact was evicted by a reimport.

## Evaluation and versioning

`Scene::OnUpdateEditor`/`OnUpdateSimulation`/`OnUpdateRuntime` own the frame
order. They calculate transforms, call `AnimationRuntime::AdvanceClocks`,
`EvaluatePose`, run scripts/physics, then `FinalizePose`, and only then queue the
render. The runtime resolves the animator that drives each skeleton (same entity
first, then `SkeletonEntity`, with a warning when several animators target one
skeleton). A skeleton with no animator or no resolvable active clip stays in its
rest pose.

`OnUpdateRuntime` evaluates once before scripts and again after scripts when
playback state changed, so `Play`/`Stop`/`SetTime` from C# still applies before
physics and rendering. `FinalizePose` is the post-physics hook reserved for
ragdoll blending, IK, root motion, and sockets.

Runtime playback state is authored through `AnimatorComponent::PlayOnStart`.
`Scene::Copy` resets each animator's time, clears resolved clips, and sets
`IsPlaying` from `PlayOnStart`; `Scene::StartAnimators` applies the same rule on
`OnRuntimeStart`/`OnSimulationStart`, and `Scene::StopAnimators` resets playback
on stop. Scripts can override the state after startup.

`AnimationInstance::PoseVersion` is bumped only when the evaluated pose changes
(clip, time, animator, or first evaluation). Each GPU instance tracks the last
version uploaded into each frame slot, so a paused instance still initializes a
frame slot that has not seen the current pose. `SkinningSystem` tracks the same
per-frame version and re-dispatches only when its slot is stale. Renderer
`BeginFrame` must run after `FinalizePose`, or skinning will use a stale pose.

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
- Clip cross-fades, pose blending, additive layers, root motion, event
  dispatch, event timeline authoring, and sockets/attachments are not
  implemented. Marker/event data now round-trips from `.meta` through the
  native clip into `AnimationClip`; `AnimationRuntime` exposes the query and
  pending-dispatch surfaces for the next milestone.
- `FinalizePose` is an empty post-physics hook; ragdoll blending and IK still
  need to fill it and bump `PoseVersion`.
- Physics/ragdoll integration and Jolt bone proxies are not implemented.
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
