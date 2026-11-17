# Tracy profiling

The engine keeps its existing Chrome-trace JSON profiler and can additionally
send live CPU and Vulkan GPU data to Tracy when configured with
`-DKBR_ENABLE_TRACY=ON`.

## Build the client

Tracy is tracked as the `KerberosEngine/ThirdParty/tracy` submodule. Initialize
all submodules before configuring the project, then use the dedicated preset
(the client needs an optimized build with symbols for useful stacks):

```powershell
git submodule update --init --recursive
cmake --preset vs2026-tracy-relwithdebinfo
cmake --build --preset vs2026-tracy-relwithdebinfo
```

The `vs2026-tracy-relwithdebinfo` preset sets `KBR_ENABLE_TRACY=ON`, which links
`Tracy::TracyClient` and propagates `KBR_ENABLE_TRACY` to the engine and editor.
The application is the Tracy client. It does not require the viewer to be
running; because `TRACY_ON_DEMAND` is off, it starts streaming as soon as the
viewer connects.

### Memory profiling

Tracy CPU allocation tracking (`operator new`/`delete` overrides in
`KerberosEditor/Source/Profiling/TracyMemory.cpp`) is enabled with
`KBR_ENABLE_TRACY`. It records allocation/free events only. Capturing a
callstack for every allocation is much more expensive and is opt-in:

```powershell
cmake --preset vs2026-tracy-relwithdebinfo -DKBR_ENABLE_TRACY_MEMORY_CALLSTACKS=ON
```

Use the callstack option for focused allocation investigations, not for general
frame profiling or GPU captures.

## Build and run the viewer

Build the separate Tracy Profiler application from the submodule:

```powershell
.\Scripts\BuildTracyProfiler.ps1
.\Scripts\RunTracyProfiler.ps1
```

Start the viewer before the instrumented `KerberosEditor` for a live capture.
Use the viewer to save a capture after reproducing the workload. The Tracy
viewer is not part of the KerberosRenderer application target and is not
required for normal builds. Rebuild the viewer whenever the Tracy submodule
changes: the viewer and the client must come from the same commit, otherwise
the viewer reports `Incompatible protocol! (used: <client>, required:
<viewer>)` and refuses the connection.

## Vulkan GPU profiling

GPU zones come from the render-pass debug-label call sites
(`BeginRenderPassDebugLabel`/`EndRenderPassDebugLabel` in `Renderer`,
`Graph`, and `ParticleSystem`). The `KBR_TRACY_GPU_ZONE` macro is a scope guard,
so it is declared next to the `Begin` call and the enclosing C++ scope is closed
right after the matching `End` call; keep that pairing when adding labels.

`TracyVkCollect` runs once per recorded frame at the end of
`VulkanContext::RecordCommandBuffer`, after all zones are recorded and outside
any render pass instance. Without it the viewer receives zone boundaries but no
GPU timestamps.

GPU profiling needs:

- `VK_EXT_calibrated_timestamps` (requested as an optional device extension and
  enabled when the physical device advertises it), and
- a graphics queue with `timestampValidBits > 0`.

If either is unavailable, the Tracy Vulkan context is not created, a warning is
logged, and CPU zones/memory events still work. `TracyVkContextCalibrated` owns
a query pool, so the context is destroyed in `VulkanContext::Cleanup` before the
device is released. Only one context exists per graphics queue for now; add one
context per queue if zones are eventually recorded on compute or transfer
queues.

## Plots

`Renderer::ResolveGPUTimings` publishes the existing per-pass GPU timings as
`KBR_TRACY_PLOT` values. `MemoryBudget::UpdateMemoryBudgetInfo` additionally
publishes `VRAM Used (MB)` and `VRAM Budget (MB)` from the VMA heap budgets,
summed over the device-local heaps (falling back to heap 0 when no heap is
flagged device-local). Both update once per frame and are no-ops when Tracy is
disabled.

## Instrumentation

Use `KBR_ENABLE_TRACY` only for profiling builds. `KBR_TRACY_SCOPE`,
`KBR_TRACY_FRAME_MARK`, `KBR_TRACY_GPU_ZONE`, and the other `KBR_TRACY_*`
macros become no-ops when Tracy is disabled, while the existing
`KBR_PROFILE_*` macros continue to produce the current JSON profiles. For
runtime-built zone names (render-graph pass names, `std::format` labels) use
`KBR_TRACY_GPU_ZONE_TRANSIENT`; the plain macro requires a string literal.
