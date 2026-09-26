---
applyTo: "KerberosEditor/Source/**/*,KerberosEngine/Source/Editor*,KerberosEngine/Source/Application*,KerberosEngine/Source/Project/**/*,KerberosEngine/Source/Scene/**/*"
---

# Editor system context

## Lifecycle and ownership

`Application` owns the GLFW window, layer stack, `VulkanContext`, main-thread
queue, GPU upload queue, and frame loop. `EditorLayer` owns editor-facing
project/scene state, cameras, panels/windows, viewport state, selection, and
editor UI lifecycle.

The application loop polls events, updates layers, builds ImGui, drains
uploads, records/submits/presents Vulkan work, and advances frames.
`EditorLayer::OnAttach`, `OnUpdate`, `OnImGuiRender`, and event handlers are
the primary editor lifecycle points.

The working directory is changed to the project directory before renderer and
shader initialization. Do not add path assumptions that bypass this project
root convention.

## Main-thread and UI rules

The Vulkan boundary and ImGui calls are main/render-thread operations.
Background work must enqueue application main-thread callbacks before mutating
editor state or invoking UI-dependent systems. GPU upload jobs are drained on
the render thread and propagate failures rather than silently succeeding.

Editor panels should not mutate collections while iterating them. For
filesystem-backed views such as `AssetsPanel`, defer refresh/navigation until
after the current ImGui loop and stack cleanup.

Preserve ImGui Begin/End, popup, ID, style-color, columns/table, and drag/drop
pairing rules. Give repeated controls stable `##` IDs and avoid duplicate
labels across windows.

## Panels and integration

Editor windows live mainly under
`KerberosEditor/Source/Windows`. Important integrations include:

- `AssetsPanel`: filesystem browser, previews, context menus, material editor,
  asset drag/drop, and source import.
- `HierarchyPanel`: entity selection, component add/remove, and component
  editing, including particle emitters.
- `ViewportPanel`: rendered image display, viewport resize, camera input, and
  GPU mouse picking.
- project/scene panels: project settings, serialization, scene lifecycle, and
  runtime/editor mode transitions.

Panels consume engine-owned project, scene, asset, renderer, and event
systems. Keep ownership clear: UI state belongs to the panel, persistent
scene/project state belongs to the corresponding engine object, and GPU
resources belong to Vulkan/renderer owners.

## Project and scene flow

Project loading creates/activates an editor asset manager, loads the asset
registry, ensures metadata, builds sources, logs build diagnostics, and starts
watching. Project reconfiguration follows a separate path and may not perform
all of the same registry/build steps; preserve or explicitly reconcile that
behavior when changing project lifecycle.

Scene updates drive editor/runtime simulation and then queue rendering. Scene
serialization must preserve authored component data while leaving transient
runtime state transient. Scene and project transitions must not leave
non-owning pointers to destroyed managers or stale selected entities.

## Events, selection, and resources

Use the repository’s event classes and layer dispatch path rather than
introducing ad-hoc global callbacks. Selection-dependent panels should handle
entity destruction and scene changes without dereferencing stale handles.

The editor displays renderer-owned images in ImGui but does not own their
Vulkan lifetime. Viewport/output resources must remain valid until the GPU has
finished using them, and resize requests must follow the renderer’s deferred
resource/lifetime rules.

Asset watcher callbacks arrive from a worker thread and are handed to the
main-thread queue. Asset panels should request refreshes after event handling,
not race the filesystem or registry while rendering.

## Error handling and UX

Surface failures through the project’s logging and notification systems.
Avoid broad catches, silent fallback assets, or success-shaped UI state when
loading/importing/serializing fails. Preserve actionable paths and handles in
errors.

Context menus that mutate files or entities should account for confirmation,
registry/metadata cleanup, selection invalidation, and a subsequent panel
refresh. Direct filesystem operations can bypass asset-system bookkeeping;
prefer the asset manager/pipeline integration where available.

## Validation

Use the repository’s Windows CMake workflow:

```powershell
cmake --preset vs2026
cmake --build --preset vs2026-debug
```

For editor changes, validate startup/project load, scene open/save, panel
navigation, selection/entity deletion, viewport resize, ImGui stack balance,
drag/drop payloads, asset watcher updates, and runtime/editor transitions.
Run shader compilation when the editor changes shader assets. Update this
file whenever editor lifecycle, panel ownership, threading, UI conventions,
project/scene integration, or validation procedures change.
