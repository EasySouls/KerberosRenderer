# Editor system context

Applies to `KerberosEditor/Source/**`,
`KerberosEngine/Source/Editor*`, `KerberosEngine/Source/Application*`,
`KerberosEngine/Source/Project/**`, and `KerberosEngine/Source/Scene/**`.

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
  asset drag/drop, source import, and source-asset selection (single click).
  The material editor loads materials through the asset manager (authored
  `.kbrmat` sources are imported into cache artifacts) and edits texture slots
  with a filterable dropdown backed by `AssetManager::GetAssetsOfType`. Saving
  writes handle-based texture slots to the source YAML, updates the already
  loaded material instance, and force-reimports the source so the cache artifact
  and future loads stay in sync.
- `HierarchyPanel`: entity selection, component add/remove, and component
  editing, including particle emitters.
- `InspectorPanel`: the unified right-side "Properties" window. It shows entity
  components when an entity is selected and asset import options when a source
  asset is selected in `AssetsPanel` (asset selection takes display precedence).
  Selecting an entity in `HierarchyPanel` clears the asset selection so entity
  properties become visible again; the hierarchy exposes this through its
  `SetOnEntitySelected` callback.
  Texture assets show a preview image plus an sRGB toggle; glTF assets list
  their generated texture sub-assets with per-texture sRGB toggles. Edits are
  staged locally and applied with Apply/Revert, which persists the `.meta` and
  re-imports the source.
- `ViewportPanel`: rendered image display, viewport resize, camera input, and
  GPU mouse picking.
- project/scene panels: project settings, serialization, scene lifecycle, and
  runtime/editor mode transitions.

Panels consume engine-owned project, scene, asset, renderer, and event
systems. Keep ownership clear: UI state belongs to the panel, persistent
scene/project state belongs to the corresponding engine object, and GPU
resources belong to Vulkan/renderer owners.

## C# script components

`ScriptComponent` (`KerberosEngine/Source/Scene/Components.hpp`) stores an
ordered `ClassNames` list, so a single entity can run several C# scripts.
`ScriptEngine` keys both runtime instances and authored field initializers by
`(entity UUID, script class full name)`; the managed `ScriptGlue` bridge stores
instances per entity in a class-name dictionary. `Entity.As<T>()` and
`Entity.GetScript<T>()` resolve a specific script instance by type.

`HierarchyPanel` renders one entry per script class with add/remove controls
and per-script field editors. `SceneSerializer` emits a `Scripts` sequence
(each entry has `ClassName` and `ScriptFields`) and still accepts the legacy
single `ClassName`/`ScriptFields` form. When script lifecycle, managed interop
signatures, or the serialized script format changes, update this file and keep
the native callback/bridge signatures synchronized.

Script fields support `PrefabRef`, `TextureRef`, and `AssetHandle` reference
types with asset-browser drag/drop. `Entity.Instantiate(PrefabRef)` spawns a
prefab through `Scene::InstantiatePrefab`; while the scene is running, spawned
scripted-prefab entities get script instances created and `OnCreate` invoked
immediately. `Scene::UpdateScripts`/`OnRuntimeStart` snapshot the script entity
list before invoking callbacks so scripts may spawn prefabs during lifecycle
methods.

Script classes use the `Kerberos.*` namespace (renamed from
`Kerberos.Source.Kerberos.*`); `ScriptUtils::MigrateLegacyScriptClassName`
rewrites the old prefix when scenes/prefabs are deserialized. The built-in
sample scripts (`Player`, `Camera`, `CameraFollow`) live in `KerberosScriptCoreLib`
under `Kerberos`; user scripts live in their own per-project assembly.

Each project owns a C# project at `<project>/Assets/Scripts/<PascalName>.csproj`
(referencing `KerberosScriptCoreLib` through `$(KerberosEnginePath)`, with
`<Private>false</Private>` so the core library is provided by the host),
generated on project creation by `WriteUserScriptProject` with the project name
PascalCased (`Project::ToPascalCaseName`). `ScriptEngine::OnProjectOpened`
builds it with `dotnet build -c Debug -o <project>/Library/Scripts`, loads it
into a collectible `AssemblyLoadContext`, and watches `Assets/Scripts` for
rebuilds. The user load context resolves `KerberosScriptCoreLib` to the already
loaded core assembly (not a copy from the output directory) so user script
types derive from the same `Entity` base type. The core assembly path comes from
`EnginePaths::ScriptAssemblies()`.

## Project and scene flow

`File > New Project...` asks for an empty folder through
`FileDialog::SelectFolder`; the selected folder becomes the project directory
and its name becomes the project name. The editor creates `Assets/Scenes`,
writes a default `Untitled Scene.kerberos`, activates the new project, registers
the start scene, and persists `<ProjectName>.kbrproj`. The default scene is
written before the project is activated so a failure leaves the current project
untouched.

The UI thread is initialized as a single-threaded apartment with `OleInitialize`
at the start of `Application` construction, before audio/other COM consumers
run. Shell dialogs (`IFileOpenDialog`) require an STA thread; the audio backends
otherwise initialize the thread as MTA. `FileDialog::SelectFolder` shows the
modern dialog synchronously on the UI thread and falls back to the legacy
`SHBrowseForFolder` only if the thread is unexpectedly MTA.

Project switches triggered from the menu (`New Project...` and `Open Project...`)
are deferred with `Application::SubmitToMainThreadQueue` and run at the start of
the next frame; `CreateProject`/`OpenProject` call `VulkanContext::WaitIdle()`
before releasing the old project. Never replace the active project or scene
synchronously from an ImGui callback: releasing the old project destroys cached
GPU mesh/texture buffers while the current frame's command buffer may still
reference them (validation error `vkFreeMemory ... currently in use`, followed
by a fence wait hang).

Project-relative paths must resolve through `Project::GetProjectDirectory()` and
`Project::GetAssetDirectory()`. `GetAssetDirectory()` returns an absolute
project-directory/asset-directory path; do not assume the process working
directory is the project directory. Engine-owned content (fonts, icons, shaders,
and the managed script assembly) resolves through `Core/EnginePaths`
(`EnginePaths::Assets()`, `Shaders()`, `ScriptAssemblies()`, ...), initialized
by `Application` from `ApplicationSpecification::EngineRoot`.

Project loading creates/activates an editor asset manager, loads the asset
registry, ensures metadata, builds sources, logs build diagnostics, and starts
watching. Project reconfiguration follows a separate path and may not perform
all of the same registry/build steps; preserve or explicitly reconcile that
behavior when changing project lifecycle.
`Project::Load` also supports an explicit headless option for tests; headless
loads validate project state without creating the editor asset manager or
starting its watcher.
`ApplicationSpecification::Headless` is available to `TestApplication` for
hidden-window Vulkan lifecycle tests. Those tests still use the real
application, renderer, scripting, asset-manager, and scene-loading paths, but
must close and release project resources before the application is destroyed.

Scene updates drive editor/runtime simulation and then queue rendering. Edit and
Simulate render through the editor camera; Play renders through the runtime
scene's primary `CameraComponent`. The runtime view matrix is the inverse of the
camera entity's world transform, and both editor/runtime scene projection
matrices apply the Vulkan Y flip. The editor camera is neither updated nor given
input while Play is active. Scene serialization must preserve authored component
data while leaving transient runtime state transient. Scene and project
transitions must not leave non-owning pointers to destroyed managers or stale
selected entities.

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
