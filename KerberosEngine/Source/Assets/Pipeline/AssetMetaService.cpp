#include "AssetMetaService.hpp"

#include "kbrpch.hpp"

#include <yaml-cpp/yaml.h>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace Kerberos {

AssetMetaService::AssetMetaService(std::filesystem::path assetsRoot) : m_AssetsRoot(std::move(assetsRoot)) {}

AssetMetaFile AssetMetaService::EnsureMetaForSource(const std::filesystem::path& sourcePath) 
{
    if (!std::filesystem::is_regular_file(sourcePath)) {
        KBR_CORE_ERROR("Cannot create meta for missing source: {0}", sourcePath.string());
        return {};
    }
    const auto metaResult = LoadMeta(sourcePath);
    if (!metaResult.has_value()) {
        KBR_CORE_WARN("Meta file missing or invalid for source: {0}. Creating new meta.", sourcePath.string());

        AssetMetaFile newMeta;
        newMeta.SourceHandle = AssetHandle();
        newMeta.SourceHash = ComputeSourceHash(sourcePath);
        SaveMeta(sourcePath, newMeta);
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

    try {
        YAML::Node config = YAML::LoadFile(metaPath.string());
        if (config["AssetMeta"])
            config = config["AssetMeta"];
        AssetMetaFile meta{};
        if (config["SchemaVersion"])
            meta.SchemaVersion = config["SchemaVersion"].as<uint32_t>();
        if (meta.SchemaVersion > AssetMetaFile::CurrentSchemaVersion)
            return std::nullopt;
        if (config["ImporterVersion"]) meta.ImporterVersion = config["ImporterVersion"].as<uint32_t>();
        if (config["ImporterType"]) meta.ImporterType = static_cast<ImporterType>(config["ImporterType"].as<uint8_t>());
        const YAML::Node sourceHandle = config["SourceHandle"] ? config["SourceHandle"] : config["Handle"];
        if (sourceHandle) meta.SourceHandle = AssetHandle(sourceHandle.as<uint64_t>());
        if (config["SourceHash"]) meta.SourceHash = config["SourceHash"].as<std::string>();
        if (config["SubAssets"]) {
            for (const auto& node : config["SubAssets"]) {
                SubAssetMetaEntry entry;
                if (node["LocalKey"]) entry.LocalKey = node["LocalKey"].as<std::string>();
                if (node["Handle"]) entry.Handle = AssetHandle(node["Handle"].as<uint64_t>());
                if (node["Kind"]) entry.Kind = static_cast<SubAssetKind>(node["Kind"].as<uint8_t>());
                meta.SubAssets.push_back(std::move(entry));
            }
        }
        return meta;
    } catch (const YAML::Exception& e) {
        KBR_CORE_WARN("Invalid meta file {0}: {1}", metaPath.string(), e.what());
        return std::nullopt;
    }
}

bool AssetMetaService::SaveMeta(const std::filesystem::path& sourcePath, const AssetMetaFile& meta) 
{
    YAML::Emitter out;
    out << YAML::BeginMap
        << YAML::Key << "SchemaVersion" << YAML::Value << AssetMetaFile::CurrentSchemaVersion
        << YAML::Key << "SourceHandle" << YAML::Value << static_cast<uint64_t>(meta.SourceHandle)
        << YAML::Key << "ImporterType" << YAML::Value << static_cast<uint8_t>(meta.ImporterType)
        << YAML::Key << "ImporterVersion" << YAML::Value << meta.ImporterVersion
        << YAML::Key << "SourceHash" << YAML::Value << meta.SourceHash
        << YAML::Key << "SubAssets" << YAML::Value << YAML::BeginSeq;
    for (const auto& entry : meta.SubAssets) {
        out << YAML::BeginMap
            << YAML::Key << "LocalKey" << YAML::Value << entry.LocalKey
            << YAML::Key << "Handle" << YAML::Value << static_cast<uint64_t>(entry.Handle)
            << YAML::Key << "Kind" << YAML::Value << static_cast<uint8_t>(entry.Kind)
            << YAML::EndMap;
    }
    out << YAML::EndSeq << YAML::EndMap;
    return WriteAtomic(MetaPathFor(sourcePath), out.c_str());
}

bool AssetMetaService::RebindMetaOnRename(const std::filesystem::path& oldPath, const std::filesystem::path& newPath) 
{
    const auto oldMeta = MetaPathFor(oldPath);
    const auto newMeta = MetaPathFor(newPath);
    std::error_code ec;
    if (!std::filesystem::exists(oldMeta, ec))
        return false;
    std::filesystem::create_directories(newMeta.parent_path(), ec);
    if (ec) return false;
    std::filesystem::rename(oldMeta, newMeta, ec);
    if (ec) {
        auto meta = LoadMeta(oldPath);
        return meta.has_value() && SaveMeta(newPath, *meta) &&
               (std::filesystem::remove(oldMeta, ec), !ec);
    }
    return true;
}

std::filesystem::path AssetMetaService::MetaPathFor(const std::filesystem::path& sourcePath) 
{
    std::filesystem::path metaPath = sourcePath;
    return metaPath.replace_extension(".meta");
}

std::string AssetMetaService::ComputeSourceHash(const std::filesystem::path& sourcePath) 
{
    std::ifstream file(sourcePath, std::ios::binary);
    if (!file.is_open()) return {};
    uint64_t hash = 14695981039346656037ull;
    char buffer[8192];
    while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0) {
        for (std::streamsize i = 0; i < file.gcount(); ++i) {
            hash ^= static_cast<unsigned char>(buffer[i]);
            hash *= 1099511628211ull;
        }
    }
    std::ostringstream result;
    result << std::hex << std::setfill('0') << std::setw(16) << hash;
    return result.str();
}

bool AssetMetaService::WriteAtomic(const std::filesystem::path& filePath, const std::string& content) 
{
    std::scoped_lock _(m_Mutex);
    std::error_code ec;
    std::filesystem::create_directories(filePath.parent_path(), ec);
    if (ec) return false;
    const auto temporaryPath = filePath.string() + ".tmp";
    {
        std::ofstream file(temporaryPath, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) return false;
        file.write(content.data(), static_cast<std::streamsize>(content.size()));
        file.flush();
        if (!file.good()) return false;
    }
    std::filesystem::rename(temporaryPath, filePath, ec);
    if (ec) {
        ec.clear();
        std::filesystem::remove(filePath, ec);
        ec.clear();
        std::filesystem::rename(temporaryPath, filePath, ec);
    }
    if (ec) std::filesystem::remove(temporaryPath, ec);
    return !ec;
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
