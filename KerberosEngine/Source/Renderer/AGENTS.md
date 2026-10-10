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

Uploads commonly use staging buffers and synchronous waits. This is safe but
can stall; preserve the existing render-thread assertions and exception
propagation when changing upload paths.

## Vulkan device and swapchain

The device requires Vulkan 1.3 and required graphics features/extensions.
Dedicated compute/transfer queues may be selected, while optional descriptor
buffer, mesh shader, shader object, ray query, and acceleration-structure
features are enabled when available.

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

1. depth pre-pass
2. shadows
3. GTAO
4. opaque
5. particles
6. grass
7. transparent
8. transparency resolve
9. bloom
10. anti-aliasing
11. upscaling
12. tonemapping
13. presentation
14. mouse picking

`RenderGraph.hpp/.cpp` is a legacy graph with different resource ownership,
semaphores, and queue submissions. Do not extend or mix it with `Graph`
without first proving it is still used.

## Resource lifetime and descriptors

Renderer resources must remain alive until all submitted frames that use them
complete. Deferred renderer actions currently use a device-wide wait to make
resource mutation safe; preserve that guarantee unless introducing explicit
per-resource synchronization.

Buffers use a mixture of device-local staging allocations and persistently
mapped host-visible uniform/storage buffers. Images/textures own their Vulkan
image, memory, view, and sampler state. Texture uploads transition through
transfer-destination to shader-read-only and commonly wait synchronously.

`TextureManager` owns the global update-after-bind texture table and sampler
objects. Texture slots are packed with sampler selection and exhaustion is an
explicit error. `Material::ResolveIndices` converts material texture handles
to global texture indices.

Dynamic uniform offsets must respect
`minUniformBufferOffsetAlignment`; current per-object allocation also has a
capacity limit. Graph image subresource ranges and overlapping layout
declarations must resolve to compatible mip/layer counts and layouts.

## Shaders and working directory

Runtime shader loading expects paths below the project `assets\shaders`
directory, compiles Slang to SPIR-V 1.6, caches `.spv`, reflects resources,
and supports recompilation. `Application` changes to the project directory
before creating the Vulkan context/renderer; shader path assumptions depend
on this working-directory invariant.
Tests may request a hidden GLFW window through
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
