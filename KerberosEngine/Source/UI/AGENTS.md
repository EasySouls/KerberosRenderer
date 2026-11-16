# Game UI context

Applies to `KerberosEngine/Source/UI/**`,
`KerberosEngine/Source/Renderer/UI/**`,
`KerberosEngine/Source/Scene/Components/UIComponents.hpp`,
`KerberosEditor/Assets/Shaders/ui.slang`, and the game-UI parts of
`KerberosEditor/Source/Windows/HierarchyPanel.cpp`,
`KerberosEditor/Source/EditorLayer.cpp`, and
`KerberosEngine/Source/Scripting/ScriptInterface.cpp`.

This is the runtime game UI, not the editor's ImGui UI. The editor's own panels
remain ImGui-based.

## Ownership

- `UISystem` is a value member of `Scene`, like `AnimationRuntime` and
  `RagdollSystem`. A runtime scene copy therefore has independent transient
  hover/press/focus state, and `Scene::Copy` clears it.
- `UIRenderer` is owned by `Renderer` (`RendererData::UIRender`). It owns the
  pipeline, pipeline layout, and two host-visible staging buffers per frame in
  flight. It owns no images and needs no resize path.
- `UIFontLibrary` is a process-wide cache initialized with `TextureManager` by
  `Renderer::Init` and cleared by `Renderer::Shutdown`. It owns every `Ref<Font>`
  it creates and registers each MSDF atlas once with
  `TextureManager::RegisterTexture`.
- `UIElementComponent` and `UICanvasComponent` are authored scene data; their
  transient fields (`IsHovered`, `IsPressed`, `IsFocused`, `Clicked`,
  `InputBuffer`, `CaretIndex`) are never serialized and are reset by
  `Scene::Copy` and `UISystem::ResetTransientState`.

## Authoring model

`UICanvasComponent` defines a screen-space scope: visibility, sort order, a
reference resolution, `InputEnabled`, and `BlockSceneInput` (modal). It may sit
on an entity that also carries a `UIElementComponent` (for example a full-screen
backdrop).

`UIElementComponent::Type` selects the element behavior:

- `Panel` — rounded/gradient rectangle with optional border.
- `Text` — word-wrapped, aligned MSDF text, optional drop shadow.
- `Button` — panel + centered label; hover/press/disabled colors are tint
  multipliers (`HoverColor`/`PressColor`/`DisabledColor` multiply `Color`).
- `Toggle` — button that flips `IsOn` on click and draws a knob.
- `Bar` — background plus a normalized `Value` fill in one of four directions.
- `TextInput` — single-line field with placeholder, caret, max length, and
  `InputBuffer` for scripts.
- `Image` — textured quad using `Texture` (a `Texture2D` asset handle).

Layout is anchor/pivot based: `Anchor` picks a point in the parent rect (the
canvas rect or the nearest ancestor `UIElementComponent`), `Offset` and `Size`
are design pixels, and `Pivot` positions the element around the anchor point.
`Anchor::Stretch` uses `Margin` (left, top, right, bottom) instead. `SortOrder`
orders siblings; the canvas `SortOrder` orders canvases.

`Space::World` elements ignore the canvas hierarchy and project their entity's
`TransformComponent` world position to screen space. This makes a "Press E"
label a normal child of a pickup entity, movable with the transform gizmo.

Canvas scaling uses `ScaleMode`: `MatchHeight` scales by
`screenHeight / ReferenceHeight`, `MatchWidth` by `screenWidth / ReferenceWidth`,
and `None` keeps 1:1 design pixels.

## Update and render order

- `Scene::OnUpdateRuntime` resolves the primary camera, then calls
  `UISystem::Update(..., interact = true)` **before** scripts and **outside**
  the paused block, so `Clicked`/`IsHovered` are visible to scripts in the same
  frame and pause menus stay interactive while the scene is paused.
- `Scene::OnUpdateEditor` and `Scene::OnUpdateSimulation` call
  `UISystem::Update(..., interact = false)`. Layout still runs so authored UI is
  visible while editing, but pointer/keyboard interaction is disabled.
- The renderer's "Game UI" graph pass runs between `Tonemapping` and
  `Presentation`. It calls `UISystem::BuildDrawData` with the pending render's
  **unjittered** view-projection, then records one indexed draw into
  `FinalImage` with `loadOp = LOAD`.

`FinalImage` is display-referred (sRGB-encoded values in a float format), so UI
colors are authored in display space and written without any transfer
conversion. Do not add exposure or tonemapping to the UI shader.

`BuildDrawData` re-reads component values at record time; this is what makes a
script assigning `Bar.Value` visible in the same frame. Do not cache draw data
across frames.

## Input contract

`EditorLayer::FeedUIInput` builds a `UIInputState` each frame from the viewport
bounds, window mouse position, and buffered keyboard events, then calls
`Scene::SetUIInput`.

- Mouse coordinates are converted into output-image pixels using
  `Renderer::GetOutputImageSize()` and the ImGui viewport bounds.
- `MouseInsideViewport` gates hover; clicks outside the viewport are ignored.
- `TypedCharacters` only accumulates while the viewport is focused and ImGui is
  not capturing text input (`KeyTypedEvent`, ASCII only).
- Backspace/Enter/Tab/Escape flags are edge flags consumed by `UISystem::Update`.
- `UISystem` computes press/release edges from `LeftDown`; the host does not
  send click events.

`UISystem::IsInputCaptured()` is true while a visible canvas with
`BlockSceneInput` exists or a text field is focused. `Scene::IsUIInputCaptured`
exposes it, and C# reads it as `Kerberos.Core.UI.InputCaptured`. Gameplay
scripts must skip camera/character input while it is true.

## Fonts

`UIFontLibrary::Get(authoredPath, fontSize)` resolves the path against the
project asset directory first and `EnginePaths::Assets()` second, then caches a
`Font` per `(path, emSize bucket)`. Buckets are 16/20/24/32/40/48/64 to keep
small text crisp; layout uses the requested `FontSize` because glyph plane bounds
are in em units.

`Font::Font(name, path, emSize)` rasterizes the atlas at the requested em size.
`UIFontLibrary` never depends on `EditorAssetManager`, so a future standalone
game executable can use the same path.

## Shader contract

`ui.slang` binds the global bindless texture set at set 0 (bindings 0 and 1),
one push constant block (`screenSize`, vertex + fragment stages), and these
vertex attributes in order:

0. `float2 position` (output pixels, top-left origin)
1. `float2 uv`
2. `float4 color`
3. `float2 localUV` (0..1 inside the quad)
4. `float4 style` (xy = half size, z = radius, w = border width)
5. `uint textureIndex` (`TextureManager::Pack(index, sampler)`)
6. `uint mode` (0 rect, 1 text, 2 image, 3 border ring)

`UIVertex` must remain 64 bytes and match the attribute layout in
`UIRenderer::Init`. Integer attributes must not be marked `nointerpolation` in
`VSInput` (SPIR-V forbids `Flat` on vertex inputs) but must be
`nointerpolation` in `VSOutput`.

The preset pipeline uses no depth attachment, no culling, alpha blending,
dynamic viewport/scissor, and `UsingDescriptorBuffers = false`; the global
texture set is bound with `cmd.bindDescriptorSets`.

## Editor authoring

The hierarchy "Add Component" popup has a `UI` submenu: `UI Canvas`, plus
presets for Panel, Text, Button, Toggle, Bar, Text Input, Image, and World Label.
Presets add a preconfigured `UIElementComponent`; the inspector edits layout,
style, type-specific fields, fonts (discovered `.ttf`/`.otf` files), and shows
runtime state while the scene is running.

Undo for UI component edits is a single `SetComponentValueCommand` per gesture,
committed when the last ImGui item in the component block deactivates.

Scene serialization writes the full authored component state; enum fields are
stored as the strings returned by the `ToString` helpers in
`UIComponents.hpp`. Read paths use defaults so older scenes load.

## Known limitations

- No editor viewport hit-testing or drag-to-position of UI elements; use the
  inspector fields (world labels use the transform gizmo).
- Text is ASCII/Latin-1 (the font charset); no RTL, shaping, or horizontal
  scrolling in text fields.
- No general scissor clipping: `ClipChildren` clips by dropping out-of-bounds
  glyphs and is intended for simple overlays, not scroll views.
- World labels have no occlusion test or stacking; they draw in canvas/world
  order.
- No UI-specific asset format; UI lives in the scene.

## Validation

Build and compile the shader:

```powershell
cmake --preset vs2026
cmake --build --preset vs2026-debug
cd KerberosEditor\Assets\Shaders
.\compile_shaders.bat
```

Validate: creating each preset, nesting elements, anchor/offset/size changes at
several viewport sizes, gradient/border/radius rendering, MSDF text at small and
large sizes, Play-mode hover/press/click, `UI.InputCaptured` suppressing gameplay
input, bar values from C#, text input focus/backspace/max length, pause-menu
interaction, scene save/load round-trip, Play→Stop→Play transient reset, world
labels following entities and hiding behind the camera, and Vulkan +
synchronization validation with two frames in flight and FSR3 on/off.
