---
applyTo: "KerberosEngine/Source/Renderer/ParticleSystem*,KerberosEngine/Source/Scene/**/*Particle*,KerberosEditor/Assets/Shaders/particle*,KerberosEditor/Source/Windows/HierarchyPanel.cpp,KerberosEngine/Source/Serialization/SceneSerializer.cpp"
---

# Particle system context

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
`Texture2D` handle and resolve it through the active asset manager.

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
