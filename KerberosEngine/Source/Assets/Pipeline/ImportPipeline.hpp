#pragma once

#include "Assets/Pipeline/AssetMeta.hpp"

#include <filesystem>
#include <string>

namespace Kerberos {

struct ImportContext
{
    std::filesystem::path SourceAbsolutePath;
    std::filesystem::path AssetRootAbsolutePath;
    std::filesystem::path CacheRootAbsolutePath;
    AssetMetaFile Meta;
};

struct BuiltSubAsset
{
    AssetHandle Handle;
    AssetType Type;
    std::filesystem::path LibraryRelPath;
    std::string SubAssetKey;
};

struct ImportResult
{
    AssetHandle SourceHandle;
    std::vector<BuiltSubAsset> Outputs;
    std::vector<std::string> Warnings;
};

class AssetImportPipeline
{
public:
    ImportResult ImportOrRebuild(const ImportContext& context);
};

}