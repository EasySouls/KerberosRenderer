#pragma once

#include "Assets/AssetMetadata.hpp"
#include "Assets/Pipeline/AssetMeta.hpp"

#include <filesystem>
#include <optional>
#include <shared_mutex>
#include <string>

namespace Kerberos {

class AssetMetaService
{
public:
    explicit AssetMetaService(std::filesystem::path assetsRoot);

    /// Ensure a .meta file exists for `sourcePath`. Returns the meta (new or existing).
    AssetMetaFile EnsureMetaForSource(const std::filesystem::path& sourcePath);

    /// Load meta; returns nullopt if not present or invalid.
    std::optional<AssetMetaFile> LoadMeta(const std::filesystem::path& sourcePath) const;

    /// Persist meta atomically (write temp + rename).
    /// @returns success.
    bool SaveMeta(const std::filesystem::path& sourcePath, const AssetMetaFile& meta);

    /// Move/rename handling: attach meta to new path and update internal indices.
    bool RebindMetaOnRename(const std::filesystem::path& oldPath, const std::filesystem::path& newPath);

    /// Compute a canonical meta path for a source.
    static std::filesystem::path MetaPathFor(const std::filesystem::path& sourcePath);

    /// Compute content hash (file bytes) - used by staleness checks.
    static std::string ComputeSourceHash(const std::filesystem::path& sourcePath);

private:
    bool WriteAtomic(const std::filesystem::path& filePath, const std::string& content) const;
    std::optional<std::string> ReadFileText(const std::filesystem::path& path) const;

private:
    const std::filesystem::path m_AssetsRoot;
    mutable std::shared_mutex m_Mutex;
};

}