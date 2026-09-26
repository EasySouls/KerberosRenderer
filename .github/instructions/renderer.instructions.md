---
applyTo: "KerberosEngine/Source/VulkanContext*,KerberosEngine/Source/Renderer/**/*,KerberosEngine/Source/Buffer*,KerberosEngine/Source/Image*,KerberosEditor/Assets/Shaders/**/*"
---

# Renderer context

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
