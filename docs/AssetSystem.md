# The Kerberos Asset System

This document describes how the Kerberos engine imports, stores, and loads
assets. It is aimed at engine developers. It documents the *current* state of
the system, which is mid-refactor: the glTF/GLB import path is fully
implemented end-to-end, while the standalone-source path (PNG, WAV, authored
materials, etc.) is still a placeholder that emits no cache payload.

> The source code is the source of truth. The subsystem notes live in
> `KerberosEngine/Source/Assets/AGENTS.md`; if behavior and this document ever
> disagree, trust the code and `AGENTS.md`, and update this document.

---

## 1. Overview

The asset system turns *authoring files* (glTF/GLB models, images, audio, and
Kerberos-authored formats) into *engine-loadable artifacts*, and provides a
stable, handle-based API so the rest of the engine never has to touch raw
source files.

There are four layers, with clear ownership:

| Layer | Responsibility | Key files |
|---|---|---|
| **Identity / contracts** | `AssetHandle`, `AssetType`, `AssetMetadata`, `AssetRegistry` | `Assets/Asset.hpp`, `AssetMetadata.hpp`, `AssetRegistry.hpp` |
| **Editor management** | Registry + loaded cache + metadata service + importer registry + build coordinator + file watcher | `Assets/EditorAssetManager.hpp/.cpp` |
| **Build / import pipeline** | `.meta` files, source scanning, staleness, staged cache transactions, registered importers | `Assets/Pipeline/*`, `Assets/Importers/*` |
| **Runtime loading** | Resolve registry entries and load native artifacts | `Assets/Runtime/RuntimeAssetLoader.*`, `RuntimeAssetResolver.*`, `Assets/RuntimeAssetManager.*` |

The active manager is selected through `Project`. `AssetManager` is the static
facade used by engine and editor consumers:

```cpp
// Static facade -> Project::GetActive()->GetAssetManager()->...
Ref<Mesh> mesh = AssetManager::GetAsset<Mesh>(handle);
Ref<Material> mat = AssetManager::AsAsset<Material>(materialHandle);
```

---

## 2. Core concepts and types

### `AssetHandle`

```cpp
using AssetHandle = UUID;   // Assets/Asset.hpp
```

A 64-bit UUID. Every asset — both *source assets* and their *generated
sub-assets* — is identified by an `AssetHandle`. Handles are stable across
editor sessions because they are persisted in `.meta` files and the registry
(see §7 and §8).

### `AssetType`

```cpp
enum class AssetType : uint8_t {
    Texture2D = 0, TextureCube, Material, Mesh, Scene, Sound,
    ModelDEPRECATED,   // deprecated: use Prefab for imported glTF sources
    Prefab, Animation, Skin
};
```

### `Asset` (base class)

```cpp
class Asset {
    virtual AssetType GetType() = 0;
    AssetHandle GetHandle() const;
protected:
    AssetHandle m_Handle{};  // auto-generated UUID
};
```

Concrete assets (`Mesh`, `Material`, `Texture2D`, `Prefab`, …) derive from
`Asset` and expose a static `Type` constant plus `GetType()`.

### `AssetMetadata`

```cpp
struct AssetMetadata {
    AssetType Type;
    std::filesystem::path Filepath;       // source-relative path (root) or cache-relative (sub-asset)
    std::filesystem::path LibraryPath;    // cache-relative artifact path (empty if none)
    AssetHandle RootSourceHandle;         // the source asset this belongs to
    AssetHandle ParentHandle;             // source asset for generated sub-assets
    std::string SubAssetKey;              // stable key, e.g. "mesh:0_2"
    uint64_t ImportVersion = 1;
    std::vector<AssetHandle> Dependencies;
};
```

Key distinction (also in `AGENTS.md`):

- **Root source entries** use a source-relative `Filepath` (e.g.
  `Models/sponza/sponza.gltf`).
- **Generated sub-asset entries** use a cache-relative `LibraryPath` and point
  back at the source via `RootSourceHandle`/`ParentHandle`, with a stable
  `SubAssetKey`.

### `AssetRegistry`

```cpp
class AssetRegistry {
    std::map<AssetHandle, AssetMetadata> m_Registry;
};
```

The in-memory `handle -> metadata` map. Lookups by path are linear and
case-normalized (`NormalizePath` lowercases a lexically-normalized generic
string). `AssetRegistryTypes.hpp` contains a newer serialized-entry model that
is **not** the active in-memory representation.

### `AssetManagerBase`

```cpp
class AssetManagerBase {
    virtual Ref<Asset> GetAsset(AssetHandle) = 0;
    virtual bool IsAssetHandleValid(AssetHandle) const = 0;
    virtual bool IsAssetLoaded(AssetHandle) const = 0;
    virtual AssetType GetAssetType(AssetHandle) const = 0;
};
```

`EditorAssetManager` and `RuntimeAssetManager` both implement this interface.

---

## 3. Directory layout

A project has:

```
<project>/
  <Project>.kbrproj            # project descriptor (AssetDirectory, start scene, ...)
  Assets/                      # authoring assets (the "assets root")
    AssetRegistry.kbrar        # persisted registry index
    Models/sponza/sponza.gltf  # a source file
    Models/sponza/sponza.meta  # its .meta sidecar
    ...
    Cache/                     # generated artifacts (engine-owned, gitignored/repairable)
      Models/sponza/sponza/    # per-source namespace
        sponza.kbrprefab
        sponza.kbrscene
        Meshes/...
        Materials/...
        Textures/...
```

`Project::GetAssetDirectory()` returns the configured asset directory (default
`"Assets"`), and `EditorAssetManager` is constructed with
`assetsRoot = projectDir / AssetDirectory` and `cacheRoot = assetsRoot / "Cache"`.

- **Source files** stay exactly where the user put them and are never modified
  or moved. (Two *incidental* exceptions: the Assets Panel thumbnail preview
  encodes a `.ktx2` next to source images, and converting a KTX1 texture runs
  the external `ktx2ktx2` tool which writes a `.ktx2` next to the source `.ktx`.
  Neither touches the original file's bytes.)
- **`.meta` sidecars** live next to each source file.
- **`Cache/`** holds all generated artifacts, namespaced per source.
- **`AssetRegistry.kbrar`** is the persisted registry index (derived and
  repairable from `.meta` + a successful build).

---

## 4. The import pipeline

### Registration

`EditorAssetManager::ConfigurePipeline` registers importers in an
`ImporterRegistry`:

```cpp
m_ImporterRegistry.Register(CreateRef<GLTFPipelineImporter>());   // .gltf/.glb
for (auto& [extension, type] : sourceAssetExtensionMap)
    if (extension != ".gltf" && extension != ".glb")
        m_ImporterRegistry.Register(CreateRef<StandaloneAssetPipelineImporter>(extension, type));
```

The source extension map (`EditorAssetManager.cpp`) is:

| Extensions | AssetType |
|---|---|
| `.png .jpg .jpeg .ktx .ktx2` | `Texture2D` |
| `.kbrcubemap` | `TextureCube` |
| `.fbx .obj` | `Mesh` |
| `.gltf .glb` | `Prefab` (imported as a prefab graph) |
| `.kerberos` | `Scene` |
| `.wav` | `Sound` |
| `.kbrmat` | `Material` (authored YAML) |
| `.kbrprefab` | `Prefab` (authored YAML) |
| `.kbranim` | `Animation` |

`ImporterRegistry::Find(extension)` normalizes the leading dot and case before
matching. Unknown extensions must not silently become `Texture2D`.

### The importer interface

```cpp
class IAssetImporter {
    virtual bool SupportsExtension(std::string_view) const = 0;
    virtual ImportResult Import(const ImportContext&) = 0;
    virtual ImporterType Type() const;            // None/Texture2D/GLTFScene/Standalone
    virtual uint32_t Version() const;             // bump to force re-import
    virtual AssetType SourceAssetType() const;    // root type produced for the source
};
```

`ImportContext` carries `SourceAbsolutePath`, `AssetRootAbsolutePath`,
`CacheRootAbsolutePath` (the staging dir for *this* build), and `Meta` (the
existing `AssetMetaFile`, used to preserve handles).

`ImportResult` carries `SourceHandle`, `SourceLibraryPath` (root artifact path),
`Outputs` (generated `BuiltSubAsset`s), and `Warnings`.

### `AssetBuildCoordinator::Build`

The orchestrator (`Assets/Pipeline/AssetBuildCoordinator.cpp`):

1. Verify the source is a regular file and find a registered importer.
2. `AssetMetaService::EnsureMetaForSource` — load or create the `.meta`.
3. Evaluate staleness (`AssetStalenessEvaluator`).
4. If up-to-date and the importer type/version match, return early.
5. Create an `AssetBuildTransaction` (a `.kbr-build-<stem>-<stamp>` staging
   directory under the cache root).
6. Call `importer->Import(context)` with the staging dir as the cache root.
7. `transaction.Commit()` — move staged files into the shared cache root.
8. Update `meta` (source handle, importer type/version, source hash, sub-assets).
9. Reconcile the registry: remove obsolete generated handles, then add/update
   the root and sub-asset entries.

Handles are reused across rebuilds because importers match `SubAssetKey`
against `meta.SubAssets[].LocalKey`.

### Staleness

`AssetStalenessEvaluator::Evaluate` returns one of:

- `Forced` — an explicit force rebuild.
- `SourceChanged` — source missing, or its content hash changed.
- `SettingsChanged` — importer type/version changed, or a sub-asset handle is
  invalid.
- `RegistryMissing` — source/sub-asset not present in the registry.
- `OutputMissing` — (reserved; output-file existence is not fully validated
  today).
- `None` — up to date.

The importer `Version()` is the signal to re-import existing sources when the
import format changes; `GLTFPipelineImporter::Version()` is currently `3`.

### File watching

`AssetFileWatchService` watches the assets root, filters to supported
extensions, ignores hidden/cache/staging paths, debounces events, and produces
`Added`/`Modified`/`Removed`/`Renamed` events. Its callback runs off the main
thread; `EditorAssetManager` marshals it onto the application main-thread
queue and rebuilds the affected source.

---

## 5. glTF import in detail (the fully-implemented path)

`GLTFPipelineImporter` (type `GLTFScene`, root type `Prefab`) is the real
converter. It delegates to `GLTFSceneImporter::Import`, which parses the file
with `tinygltf` and emits derived native artifacts.

### Per-source namespace

Every glTF source gets its own cache namespace derived from its path relative
to the assets root, extension stripped. For
`Assets/Models/FlightHelmet/glTF/FlightHelmet.gltf` this is:

```
Cache/Models/FlightHelmet/glTF/FlightHelmet/
```

This guarantees **one source never overwrites another source's artifacts**,
even two glTF files with the same stem.

### Artifacts produced

| File | Format | Content |
|---|---|---|
| `<stem>.kbrprefab` | `NativeAssetRecord` (kind `prefab`) | root prefab record |
| `<stem>.kbrscene` | `GltfSceneManifest` (binary) | imported node graph + primitive/material indices |
| `Meshes/mesh_<m>_<p>[_<meshName>].kbrmesh` | `NativeMeshPayload` | one file per mesh primitive: vertex blob + indices |
| `Materials/<name>.kbrmaterial` | `NativeAssetRecord` (kind `material`) | factors + texture references |
| `Textures/texture_<i>[_<imageName>].ktx2` | raw KTX2 | one file per referenced image |
| `Skeletons/<name>.kbrskeleton` | `NativeAssetRecord` (kind `skeleton`) | skin bind data |
| `Animations/<name>.kbranimation` | `NativeAssetRecord` (kind `animation`) | animation summary |

### Naming and stable sub-asset keys

Sub-asset file names are *readability hints* only. The **stable identity** is
the `SubAssetKey`, which is derived from the leading index tokens:

| Sub-asset | `SubAssetKey` | File name |
|---|---|---|
| mesh primitive | `mesh:<m>_<p>` | `mesh_<m>_<p>[_<meshName>].kbrmesh` |
| material | `material:<i>` | `<materialName>.kbrmaterial` |
| texture | `texture:<i>` | `texture_<i>[_<imageName>].ktx2` |
| skeleton | `skeleton:<i>` | `<skinName>.kbrskeleton` |
| animation | `animation:<i>` | `<animationName>.kbranimation` |
| prefab | `prefab:0` | `<stem>.kbrprefab` |

Names come from the glTF where available (`mesh.name`, `material.name`,
`image.name` or `image.uri` stem), with an index fallback, and are sanitized
(`<>:"/\|?*;=,` → `_`, leading/trailing spaces and dots trimmed). Materials,
skeletons, and animations are de-duplicated so two same-named entries never
collide within one source. `GLTFPipelineImporter` recovers the `SubAssetKey`
from the leading index tokens via `MeshKeyFromStem`/`TextureKeyFromStem`; the
readable suffix never affects identity.

### Texture import

`GLTFSceneImporter::WriteTextureKtx2` imports each image into `Textures/`:

- Already KTX2 bytes → copied directly.
- KTX1 bytes → converted with the external `ktx2ktx2` tool (or copied from an
  existing `.ktx2` sibling).
- Decoded pixels (PNG/JPG/embedded) → re-encoded via `KTX2Encoder`.

Textures are referenced *by image index* in the material record; the material
record itself stores no texture paths, only `texture:<imageIndex>` references.

### Native binary formats (`Assets/Formats/NativeAssetFormat.hpp`)

All native files start with `NativeAssetHeader { Magic, Version, Type,
PayloadSize }` (format version `3`):

- Mesh: magic `KBRM`, type `1`.
- Scene manifest: magic `KRGS`, type `2`.
- Record (material/skeleton/animation/prefab): magic `KBRR`, type `3`.

`GltfSceneManifest` stores `SourcePath`, a `Subassets` list
(`StableId/Kind/Name/SourceIndex`), and a `Nodes` list
(`StableId/Name/ParentIndex/SkinIndex/Primitives[MeshIndex, PrimitiveIndex,
MaterialIndex]`).

---

## 6. Standalone imports (placeholder)

`StandaloneAssetPipelineImporter` is a lightweight adapter for complete
single-file sources (textures, audio, authored `.kbrmat` materials, etc.). It
supplies extension/type/build registration but **emits no cache payload**:

```cpp
ImportResult result;
result.SourceHandle = context.Meta.SourceHandle.IsValid()
    ? context.Meta.SourceHandle : AssetHandle();
return result;   // SourceLibraryPath empty, no Outputs
```

Consequence: standalone sources currently have an **empty `LibraryPath`**, so
`RuntimeAssetLoader` cannot load them yet. The asset browser works around this
for images by calling `TextureImporter::ImportTexture` directly for thumbnails
(see §10). The legacy `AssetImporter` (a direct object-loading switch) is
deprecated and is not the active load path.

---

## 7. `.meta` files

Each source file has a `.meta` sidecar (same path, `.meta` extension). It owns
**importer state and stable source/sub-asset identity**. `AssetMetaService`
owns these files and writes them atomically (temp file + rename).

Structure (YAML, written by `AssetMetaService::SaveMeta`):

```yaml
SchemaVersion: 1
SourceHandle: <uint64>          # stable handle for this source
ImporterType: <uint8>           # ImporterType enum
ImporterVersion: <uint32>       # IAssetImporter::Version()
SourceHash: <hex>               # FNV-1a hash of the source bytes
SubAssets:
  - LocalKey: "mesh:0_2"        # stable sub-asset key
    Handle: <uint64>
    Kind: <uint8>               # SubAssetKind enum
```

`SubAssetKind` is `Prefab/Material/Mesh/Skeleton/AnimationClip/Texture`.

- `ComputeSourceHash` is a 64-bit FNV-1a over the file bytes.
- `MetaPathFor` is `sourcePath.replace_extension(".meta")`.
- `RebindMetaOnRename` renames the `.meta` when a source is renamed.

`.meta` files are what let sub-asset handles be reused across rebuilds: the
importer matches its produced `SubAssetKey` against `meta.SubAssets[].LocalKey`.

---

## 8. The asset registry

The active in-memory registry is `AssetRegistry` (`std::map<AssetHandle,
AssetMetadata>`). It is persisted as `Assets/AssetRegistry.kbrar` (YAML):

```yaml
SchemaVersion: 1
AssetRegistry:
  - Handle: <uint64>
    Type: "Prefab"             # AssetTypeToString
    Path: "Models/sponza/sponza.gltf"   # Filepath
    LibraryPath: "Models/sponza/sponza/sponza.kbrprefab"  # optional
    RootSourceHandle: <uint64>
    ParentHandle: <uint64>
    SubAssetKey: "prefab:0"
    ImportVersion: 3
    Dependencies: [ <uint64>, ... ]      # optional
```

`EditorAssetManager::SerializeAssetRegistry` writes it after successful
builds; `DeserializeAssetRegistry` loads it at startup. The registry is
**derived and repairable**: `.meta` owns source/sub-asset identity, and a
successful build reconciles handle→library metadata. Path lookups are linear
and case-normalized.

---

## 9. Runtime loading

### Resolving and loading

`EditorAssetManager::GetAsset(handle)`:

1. `IsAssetHandleValid` — in registry, has a non-empty `LibraryPath`, artifact
   exists.
2. Resolve `artifactPath = m_CacheRoot / metadata.LibraryPath` (or absolute).
3. `RuntimeAssetLoader::Load(handle, metadata, artifactPath, &registry, m_CacheRoot)`.
4. Cache the result in `m_LoadedAssets`.

`RuntimeAssetManager` does the same via `RuntimeAssetResolver` (a registry
snapshot + library root) and does **not** retain an editor-manager pointer.

### `RuntimeAssetLoader::Load` dispatch

| `AssetType` | Loader |
|---|---|
| `Texture2D` | `LoadTexture` → `Texture2D::FromFile` (KTX2) |
| `Material` | `LoadNativeMaterial` (native record) or `LoadMaterial` (YAML fallback) |
| `Mesh` | `LoadMesh` (native payload, with a legacy-format fallback) |
| `ModelDEPRECATED` | `LoadSceneManifest` |
| `Prefab` | `LoadNativePrefab` (native record) or `PrefabSerializer::DeserializePrefab` (YAML fallback) |

### Native prefab loading

`LoadNativePrefab` reads the `.kbrprefab` record and its sibling `.kbrscene`
manifest, then builds a `Prefab`:

- One entity per scene node (parent relationships preserved).
- One child entity per mesh primitive; its `StaticMeshAsset` is resolved from
  the registry via `FindSubasset(registry, root, "mesh:<m>_<p>")`, and
  `StaticMaterialAsset` via `"material:<i>"`.
- `RequiresInstanceRoot = true` so files with multiple glTF roots stay grouped.

### Native material loading

`LoadNativeMaterial` parses the record `Data` string
(`name;baseColor;metallic;roughness;emissive;baseColorTexture=…;normalTexture=…;…`)
and, for each texture image index, resolves `texture:<i>` through the registry
and loads the KTX2. The metallicRoughness texture is bound to both the metallic
and roughness slots (glTF packs them into one texture's G/B channels).

### Instantiation into a scene

`Scene::InstantiatePrefab(handle)`:

```cpp
Ref<Prefab> prefab = AssetManager::GetAsset<Prefab>(handle);
for (auto& e : prefab->Entities) e.ToEntity(newEntity);
```

`PrefabEntityTemplate::ToEntity` (in `Assets/Formats/PrefabAsset.hpp`) adds a
`StaticMeshComponent` with:

```cpp
c.StaticMesh   = AssetManager::ResolveMeshAsset(StaticMeshAsset);
c.MeshMaterial = AssetManager::AsAsset<Material>(StaticMaterialAsset);
```

The renderer later calls `Material::ResolveIndices(textureManager)` to bind the
material's texture objects to global texture-table indices.

---

## 10. The Assets Panel (editor)

`KerberosEditor/Source/Windows/AssetsPanel.cpp`. The panel is a **filesystem
browser**, not a registry browser: `RefreshAssetTree` enumerates the current
directory with `std::filesystem::directory_iterator`.

### What is shown

- Source files and folders under `Assets/`.
- Hidden entries (dot-prefixed) and the `Cache`/`Staging` directories are
  **filtered out**, so generated artifacts are never shown.
- Each file's handle is looked up via `FindAssetHandle` (registry `ContainsPath`
  → `GetHandle` on the assets-relative path).

### Per-item behavior

- **Thumbnails**: image files (`.png .jpg .jpeg .ktx .ktx2`) get a live preview
  via `TextureImporter::ImportTexture`. (Side effect: this encodes a `.ktx2`
  next to the source image for the preview.)
- **Tooltip** (registered assets): path, size, handle, type, root handle,
  `Filepath`, `LibraryPath`.
- **Double-click**: materials (or `.kbrmat`) open the material editor; other
  files open with the OS default handler.
- **File context menu**: Open, Reveal in Explorer, Delete File, Import as asset.
- **Folder context menu**: Open, Reveal in Explorer, Delete Folder.
- **Empty-space context menu**: New Folder, Create Material (writes a `.kbrmat`).

### Importing

- The **Import Asset** button (or "Import as asset" context menu) calls
  `EditorAssetManager::ImportAsset(path)`, which requires the file to be inside
  the assets root, then builds and registers it.

### Drag and drop

`HandleAssetDragAndDrop` sets an ImGui drag/drop payload based on the asset
type, carrying an `AssetDragPayload { Handle, RootHandle, PrimitiveIndex }`.
Consumers decode it with `ReadAssetDragPayload` rather than casting to a bare
handle.

### Material editor

Double-clicking a material opens an editor window with: name, albedo, roughness,
metallic, emissive color/intensity, and texture slots (Albedo/Normal/Metallic/
Roughness/AO/Emissive), each with Set/Clear. **Save** calls
`MaterialImporter::SaveMaterial` (writes the authored `.kbrmat` YAML) and
re-imports it.

---

## 11. Correspondence: source files ↔ generated artifacts

```
source: Assets/Models/sponza/sponza.gltf
   │   (never modified)
   ├─ Assets/Models/sponza/sponza.meta         ← stable identity + sub-asset keys
   │
   └─ build → Cache/Models/sponza/sponza/
          ├─ sponza.kbrprefab                  ← root Prefab asset (LibraryPath)
          ├─ sponza.kbrscene                   ← node graph manifest
          ├─ Meshes/mesh_0_0_*.kbrmesh         ← mesh sub-assets
          ├─ Materials/<name>.kbrmaterial      ← material sub-assets
          ├─ Textures/texture_0_*.ktx2         ← texture sub-assets
          ├─ Skeletons/<name>.kbrskeleton
          └─ Animations/<name>.kbranimation

registry (in-memory + AssetRegistry.kbrar):
   source handle  -> { Type: Prefab, Filepath: "Models/sponza/sponza.gltf",
                       LibraryPath: "Models/sponza/sponza/sponza.kbrprefab" }
   sub-asset handles -> { Type, LibraryPath (cache-relative), RootSourceHandle,
                          SubAssetKey }
```

The Assets Panel shows the **source** file (`sponza.gltf`); when that file is
dropped into a scene, its prefab artifact (`sponza.kbrprefab`) is loaded from
the cache, and its mesh/material/texture sub-assets are resolved through the
registry.

---

## 12. Current limitations / in-transition notes

- **Standalone sources** (PNG/JPG/WAV/authored `.kbrmat`) go through the
  placeholder `StandaloneAssetPipelineImporter` and have no cache payload yet,
  so they cannot be loaded through `RuntimeAssetLoader`.
- The legacy `AssetImporter` (direct object-loading switch) is deprecated.
- The runtime loader supports only the listed `AssetType`s; unsupported types
  produce an explicit error rather than a fallback.
- Authored `.kbrmat` materials still load through the legacy
  `MaterialImporter` (YAML) rather than a native material artifact.
- Cache-artifact existence is not fully validated during staleness checks, and
  stale cache/`.meta` files are not always cleaned up on deletion.
- The texture reader (`Texture2D::FromFile`) and the KTX conversion live partly
  in the renderer; the long-term plan is to move all format handling into the
  import pipeline.
