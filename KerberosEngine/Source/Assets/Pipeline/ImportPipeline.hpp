#pragma once

#include "Assets/Pipeline/AssetMeta.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace Kerberos {

/**
 * Immutable inputs supplied to a source importer for one build.
 *
 * SourceAbsolutePath identifies the authoring file being imported. The asset
 * and cache roots provide stable locations for resolving source-relative data
 * and writing staged engine-owned artifacts. Meta contains the persistent
 * source handle and prior subasset keys so importers can preserve identity
 * across rebuilds.
 */
struct ImportContext
{
    /** Absolute path to the source asset. Never use this as a runtime artifact path. */
    std::filesystem::path SourceAbsolutePath;
    /** Absolute root of the project's authoring assets. */
    std::filesystem::path AssetRootAbsolutePath;
    /** Absolute staging/cache root where this build writes imported artifacts. */
    std::filesystem::path CacheRootAbsolutePath;
    /** Existing or newly created metadata for the source asset. */
    AssetMetaFile Meta;
};

/**
 * One engine-owned artifact produced by importing a source asset.
 *
 * A BuiltSubAsset normally represents a mesh, material, texture, skin,
 * animation, or prefab child asset. Its handle is reused from Meta when the
 * SubAssetKey already existed, otherwise the importer/build coordinator
 * assigns a new stable handle. LibraryRelPath is relative to the cache/library
 * root and is the only path runtime loading should consume.
 */
struct BuiltSubAsset
{
    /** Persistent identity of this generated artifact. */
    AssetHandle Handle;
    /** Runtime type of the generated artifact, not the source file's type. */
    AssetType Type;
    /** Cache/library-relative artifact path recorded in the registry. */
    std::filesystem::path LibraryRelPath;
    /** Stable importer-defined key used to preserve identity across rebuilds. */
    std::string SubAssetKey;
    /** Other generated or already-imported artifacts required by this asset. */
    std::vector<AssetHandle> Dependencies;
};

/**
 * Complete result returned by a source importer.
 *
 * SourceHandle identifies the imported root asset and normally matches
 * Context::Meta.SourceHandle. SourceLibraryPath points to the root artifact
 * exposed to the runtime, such as a .kbrprefab or .kbrscene. Outputs contains
 * the registry entries for generated subassets. Warnings are non-fatal
 * diagnostics that should be surfaced by the editor.
 */
struct ImportResult
{
    /** Persistent handle of the imported root/source relationship. */
    AssetHandle SourceHandle;
    /** Cache/library-relative path of the root imported artifact. */
    std::filesystem::path SourceLibraryPath;
    /** Generated subasset artifacts and their dependency edges. */
    std::vector<BuiltSubAsset> Outputs;
    /** Non-fatal importer diagnostics returned to the build coordinator. */
    std::vector<std::string> Warnings;
};

/**
 * Boundary for source-to-artifact conversion.
 *
 * Implementations read only the source and context, write engine-owned
 * artifacts into the staging cache, and return registry metadata. They must
 * not mutate editor state directly or make runtime code depend on source
 * formats.
 */
class AssetImportPipeline
{
public:
    /** Import or rebuild one source using the supplied context. */
    ImportResult ImportOrRebuild(const ImportContext& context);
};

}