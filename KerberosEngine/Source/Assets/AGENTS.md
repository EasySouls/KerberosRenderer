# Asset system context

Applies to `KerberosEngine/Source/Assets/**`, `KerberosEngine/Source/Project/**`,
`KerberosEditor/Source/Windows/AssetsPanel.cpp`,
`KerberosEditor/Source/AssetConstants.hpp`, and `KerberosEditor/Assets/**`.

## Responsibilities and ownership

The asset system has four related layers:

- Identity/contracts: `AssetHandle` is a `UUID`; `AssetType`, `AssetMetadata`,
  and `AssetRegistry` define identity and relationships.
- Editor management: `EditorAssetManager` owns the editor registry, loaded
  asset cache, metadata service, importer registry, build coordinator, and
  file watcher.
- Build/import pipeline: `.meta` files, source scanning, staleness checks,
  staged cache transactions, and registered pipeline importers.
- Runtime loading: `RuntimeAssetManager`, `RuntimeAssetResolver`, and
  `RuntimeAssetLoader` resolve registry entries and load supported native data.

The active manager is selected through `Project`; `AssetManager` is the static
facade used by engine and editor consumers. See
`KerberosEngine/Source/Assets/Asset.hpp`,
`AssetMetadata.hpp`, `AssetRegistry.hpp`,
`EditorAssetManager.hpp`, `RuntimeAssetManager.hpp`, and
`KerberosEngine/Source/Project/Project.cpp`.
Normal editor project loads create and activate an `EditorAssetManager`;
headless `Project::Load` callers may intentionally omit manager initialization
for project-state tests and must not use asset-manager APIs in that mode.

## Root assets versus generated sub-assets

Do not confuse a source asset with its generated library outputs.

- Root source metadata uses a source-relative `Filepath`.
- Generated entries use a cache-relative `LibraryPath`, point to the root with
  `RootSourceHandle`/`ParentHandle`, and have a stable `SubAssetKey`.
- glTF/GLB sources are imported as prefab root assets and can generate meshes,
  materials, textures, skeletons, animations, and prefab subassets.
- Stable local keys are required to reuse sub-asset handles across rebuilds.
- Generated cache/staging outputs must not recursively become watched sources.
- Source files may live anywhere below the assets root; importers must not
  require user-facing folders such as `Assets/Prefabs`. Cache subdirectories
  are an internal organization choice only.

The active in-memory registry is `std::map<AssetHandle, AssetMetadata>`.
`AssetRegistryTypes.hpp` contains a newer serialized-entry model that is not
the active in-memory representation; do not assume both models are equivalent.
`AssetRegistry.kbrar` is required persisted editor/runtime index state, but it
is derived and repairable: source `.meta` files own importer state and stable
source/subasset identity, while successful builds reconcile the registry's
handle-to-library metadata. Path lookups are linear and case-normalized.

## Source formats and import paths

`EditorAssetManager.cpp` currently defines source extensions for textures
(`.png`, `.jpg`, `.jpeg`, `.ktx`, `.ktx2`), cubemaps, FBX/OBJ/glTF/GLB models,
Kerberos scenes, WAV audio, materials, prefabs, and animations.

There are two import concepts:

- `AssetImporter` is the legacy/direct object-loading switch used by
  `GetAsset()` and `ImportAsset()`.
- `ImporterRegistry`/`IAssetImporter` are build-pipeline importers used by
  `AssetBuildCoordinator`.

`GLTFPipelineImporter` performs real conversion and emits derived native
outputs. Its public root type is `Prefab`: a glTF file is the source input,
while the imported binary `.kbrprefab` artifact plus its `.kbrscene` manifest
are what runtime loading consumes. A scene must not resolve the glTF source
directly, and native cache prefabs must not be sent through the legacy YAML
`PrefabSerializer`.
The `.kbrscene` manifest stores the imported node graph and one record for
each mesh primitive, including its generated mesh and material indices. Native
glTF loading preserves source parent relationships, creates primitive child
entities, assigns `StaticMeshComponent::MeshMaterial`, and adds a synthetic
instance root so files with multiple glTF roots remain grouped. Legacy YAML
prefabs do not receive this wrapper behavior.
The importer also emits one native material record per glTF material (named
after the material, not just its index) and one `.ktx2` texture artifact per
referenced image. Material records reference their textures by image index so
runtime loading can resolve the corresponding `texture:<imageIndex>`
sub-assets. Decoded glTF images are flipped vertically before encoding to KTX2
so they match the renderer's sampling convention; this is the same flip
`TextureImporter::LoadTextureData` applies to standalone images and cubemap
faces.

All artifacts produced by a glTF import are written under a per-source cache
namespace derived from the source path relative to the assets root (extension
stripped), e.g. `Models/FlightHelmet/glTF/FlightHelmet/...`. This guarantees
one source's cache entries never overwrite another source's, even when two
glTF files share the same stem. Within that namespace, sub-asset file names
are readable hints rather than bare indices: meshes use
`mesh_<m>_<p>[_<meshName>]`, textures use `texture_<i>[_<imageName>]`, and
materials/skeletons/animations use their glTF name with an index fallback.
The leading index tokens are the source of the stable `SubAssetKey`
(`mesh:<m>_<p>`, `texture:<i>`, etc.), so the readable suffix never changes
sub-asset identity.
`StandaloneAssetPipelineImporter` is currently a lightweight adapter for
complete single-file sources: it supplies extension/type/build registration
but emits no cache payload. Do not describe it as a full format converter.
If standalone formats gain cache conversion, replace the adapter behavior with
format-specific importers and update this file.

Importer extension lookup normalizes the leading dot and case. Unknown file
types must not silently become `Texture2D`; preserve explicit error/warning
behavior when changing type classification.

## Metadata, registry, and staleness

`AssetMetaService` owns source `.meta` files. Metadata includes schema version,
source handle, importer type/version, source hash, and sub-asset handle/key
records. Missing metadata is created; writes use a temporary file and rename.
`AssetStalenessEvaluator` checks force rebuilds, source existence/hash,
registry completeness, and sub-asset handle validity. Generated output
existence is not currently fully validated, so changes involving cache
artifacts should address that gap deliberately.

`AssetBuildCoordinator` serializes builds, finds the registered importer,
ensures metadata, evaluates staleness, imports into a staging transaction,
commits outputs, updates metadata, reconciles obsolete generated entries, and
updates the registry. `EditorAssetManager::ImportAsset` persists the resulting
registry. Preserve stable handles and avoid partial registry updates on failed
builds; stale registry data must be repaired from successful metadata/build
results rather than treated as authoritative.

## File watching and events

`AssetFileWatchService` normalizes paths/extensions, filters paths outside the
assets root, ignores hidden/cache/staging components, debounces events, and
converts filewatch notifications into Added/Modified/Removed/Renamed events.
Its callback runs off the main thread. `EditorAssetManager` must hand events
to the application main-thread queue and guard queued callbacks with its
lifetime flag.

Removal must remove the root and associated generated sub-assets from both the
loaded cache and registry. Rename must update registry paths and rebind the
source `.meta` file before rebuilding the new path. Deletion currently does
not automatically remove stale `.meta` or cache files; do not assume cleanup
is complete.

`AssetSourceScanner` recursively scans normalized source extensions and skips
hidden paths, cache/staging directories, and `.meta` files. Keep initial
scanning and watcher filtering based on the same source policy.

## Editor/runtime integration

`EditorAssetManager::GetAsset()` must load through the imported artifact loader
using registry `LibraryPath` values. Native glTF prefab records resolve their
generated mesh handles through registry `RootSourceHandle` and
`SubAssetKey`. It must not call the legacy `AssetImporter` source-loading
switch. `ImportSource`/build operations are the only source ingestion path;
the returned root handle represents the imported prefab or other imported
artifact, not a live source-file object.

`RuntimeAssetManager` owns a registry snapshot and resolves runtime entries
only through their imported `LibraryPath`. It must not retain an editor-manager
pointer, fall back to `Filepath`, invoke source importers, or mutate metadata.
Invalid handles are errors rather than defaulting to an unrelated asset type.
The runtime loader still supports only part of the selected artifact set; add
loaders explicitly and preserve unsupported-asset errors until they exist.
glTF imports now emit native texture artifacts: each referenced image is
imported as a `.ktx2` file under `Textures/` (via `KTX2Encoder`/`KtxConversion`)
and registered as a `Texture2D` sub-asset keyed `texture:<imageIndex>`. Native
material records carry the base color, metallic, roughness, and emissive
factors plus per-slot texture references by image index. `LoadNativeMaterial`
resolves those references through the registry (`RootSourceHandle` +
`SubAssetKey`) and loads the matching textures. Authored YAML material files
still fall back to `MaterialImporter`; that reader remains transitional until
authored materials are fully migrated.

The asset browser is `KerberosEditor/Source/Windows/AssetsPanel.cpp`. It
enumerates the filesystem, looks up handles, previews textures, opens files,
supports drag/drop payloads, and edits materials. Drag/drop payloads use
`AssetDragPayload` and carry both the selected handle and its root source
handle; consumers must decode through `ReadAssetDragPayload` rather than
casting the payload to a bare handle. Refreshing its content list must not
occur while iterating it.

## Validation

Configure/build from the repository root:

```powershell
cmake --preset vs2026
cmake --build --preset vs2026-debug
```

For asset changes, validate initial source scanning and watcher add/modify/
remove/rename behavior, metadata/registry persistence, extension normalization,
and glTF sub-asset handle reuse. Keep this context file updated whenever the
asset architecture or any of these invariants changes.
