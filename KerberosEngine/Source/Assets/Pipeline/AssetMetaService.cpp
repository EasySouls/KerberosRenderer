#include "AssetMetaService.hpp"

#include "kbrpch.hpp"

#include <yaml-cpp/yaml.h>

namespace Kerberos {

AssetMetaService::AssetMetaService(std::filesystem::path assetsRoot) : m_AssetsRoot(std::move(assetsRoot)) {}

AssetMetaFile AssetMetaService::EnsureMetaForSource(const std::filesystem::path& sourcePath) 
{
    const auto metaResult = LoadMeta(sourcePath);
    if (!metaResult.has_value()) {
        KBR_CORE_ERROR("Meta file missing or invalid for source: {0}. Creating new meta.", sourcePath.string());

        AssetMetaFile newMeta;
        // TODO: Create meta
        return newMeta;
    }

    KBR_CORE_ASSERT(metaResult.has_value(), "Meta file should exist for source: {0}", sourcePath.string());

    return metaResult.value();
}

std::optional<AssetMetaFile> AssetMetaService::LoadMeta(const std::filesystem::path& sourcePath) const 
{
    const auto metaPath = MetaPathFor(sourcePath);
    if (!std::filesystem::exists(metaPath)) {
        KBR_CORE_WARN("Meta file does not exist for source: {0}", sourcePath.string());
        return std::nullopt;
    }

    YAML::Node config = YAML::LoadFile(metaPath.string());
}

bool AssetMetaService::SaveMeta(const std::filesystem::path& sourcePath, const AssetMetaFile& meta) 
{
    return false;
}

bool AssetMetaService::RebindMetaOnRename(const std::filesystem::path& oldPath, const std::filesystem::path& newPath) 
{
    return false;
}

std::filesystem::path AssetMetaService::MetaPathFor(const std::filesystem::path& sourcePath) 
{
    std::filesystem::path metaPath = sourcePath;
    return metaPath.replace_extension(".meta");
}

std::string AssetMetaService::ComputeSourceHash(const std::filesystem::path& sourcePath) 
{
    return "";
}

bool AssetMetaService::WriteAtomic(const std::filesystem::path& filePath, const std::string& content) 
{
    return false;
}

std::optional<std::string> AssetMetaService::ReadFileText(const std::filesystem::path& path) const 
{
    std::scoped_lock _(m_Mutex);

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        KBR_CORE_ERROR("Failed to open file for reading: {0}", path.string());
        return std::nullopt;
    }
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return content;
}

}
