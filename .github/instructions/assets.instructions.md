---
applyTo: "KerberosEngine/Source/Assets/**/*,KerberosEngine/Source/Project/**/*,KerberosEditor/Source/Windows/AssetsPanel.cpp,KerberosEditor/Source/AssetConstants.hpp,KerberosEditor/Assets/**/*"
---

# Asset system context

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

## Root assets versus generated sub-assets

Do not confuse a source asset with its generated library outputs.

- Root source metadata uses a source-relative `Filepath`.
- Generated entries use a cache-relative `LibraryPath`, point to the root with
  `RootSourceHandle`/`ParentHandle`, and have a stable `SubAssetKey`.
- glTF/GLB sources can generate meshes, materials, textures, skeletons,
  animations, and prefabs.
- Stable local keys are required to reuse sub-asset handles across rebuilds.
- Generated cache/staging outputs must not recursively become watched sources.

The active in-memory registry is `std::map<AssetHandle, AssetMetadata>`.
`AssetRegistryTypes.hpp` contains a newer serialized-entry model that is not
the active in-memory representation; do not assume both models are equivalent.
Path lookups are linear and case-normalized.

## Source formats and import paths

`EditorAssetManager.cpp` currently defines source extensions for textures
(`.png`, `.jpg`, `.jpeg`, `.ktx`, `.ktx2`), cubemaps, FBX/OBJ/glTF/GLB models,
Kerberos scenes, WAV audio, materials, prefabs, and animations.

There are two import concepts:

- `AssetImporter` is the legacy/direct object-loading switch used by
  `GetAsset()` and `ImportAsset()`.
- `ImporterRegistry`/`IAssetImporter` are build-pipeline importers used by
  `AssetBuildCoordinator`.

`GLTFPipelineImporter` performs real conversion and emits derived outputs.
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
commits outputs, updates metadata, and updates the registry. Preserve stable
handles and avoid partial registry updates on failed builds.

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

`EditorAssetManager::GetAsset()` lazily loads through the legacy
`AssetImporter`, then assigns the persistent handle and caches the object.
`ImportAsset()` is a separate direct-import path that derives type from the
extension, loads immediately, inserts into the registry, and serializes.
Changes should reconcile these paths rather than adding another parallel
system.

`RuntimeAssetManager` holds a non-owning registry pointer and currently loads
only the native Mesh and Model paths through `RuntimeAssetLoader`. Verify
registry ownership and library-path resolution before changing project manager
transitions or runtime packaging.

The asset browser is `KerberosEditor/Source/Windows/AssetsPanel.cpp`. It
enumerates the filesystem, looks up handles, previews textures, opens files,
supports drag/drop payloads, and edits materials. Refreshing its content list
must not occur while iterating it.

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
