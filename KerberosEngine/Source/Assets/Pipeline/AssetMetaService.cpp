#include "AssetMetaService.hpp"
#include "Core/Core.hpp"
#include "Profiling/Profilers.hpp"

#include <yaml-cpp/yaml.h>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>

import Kerberos;

namespace Kerberos {

namespace {

AnimationEventParameterTypeFile ParseAnimationEventParameterType(const std::string& value)
{
    if (value == "Bool") return AnimationEventParameterTypeFile::Bool;
    if (value == "Int") return AnimationEventParameterTypeFile::Int;
    if (value == "Float") return AnimationEventParameterTypeFile::Float;
    if (value == "Vector3") return AnimationEventParameterTypeFile::Vector3;
    if (value == "String") return AnimationEventParameterTypeFile::String;
    if (value == "Asset") return AnimationEventParameterTypeFile::Asset;

    // Accept legacy/numeric values as well.
    try
    {
        const int type = std::stoi(value);
        if (type >= 0 && type <= static_cast<int>(AnimationEventParameterTypeFile::Asset))
            return static_cast<AnimationEventParameterTypeFile>(type);
    }
    catch (const std::exception&)
    {
    }

    return AnimationEventParameterTypeFile::Float;
}

const char* AnimationEventParameterTypeToString(const AnimationEventParameterTypeFile type)
{
    switch (type)
    {
    case AnimationEventParameterTypeFile::Bool: return "Bool";
    case AnimationEventParameterTypeFile::Int: return "Int";
    case AnimationEventParameterTypeFile::Float: return "Float";
    case AnimationEventParameterTypeFile::Vector3: return "Vector3";
    case AnimationEventParameterTypeFile::String: return "String";
    case AnimationEventParameterTypeFile::Asset: return "Asset";
    }
    return "Float";
}

}

AssetMetaService::AssetMetaService(std::filesystem::path assetsRoot) 
: m_AssetsRoot(std::move(assetsRoot)) 
{}

AssetMetaFile AssetMetaService::EnsureMetaForSource(const std::filesystem::path& sourcePath) const
{
    KBR_TRACY_FUNCTION();

    if (!std::filesystem::is_regular_file(sourcePath)) {
        Log::CoreError("Cannot create meta for missing source: {0}", sourcePath.string());
        return {};
    }
    const auto metaResult = LoadMeta(sourcePath);
    if (!metaResult.has_value()) {
        Log::CoreWarn("Meta file missing or invalid for source: {0}. Creating new meta.", sourcePath.string());

        AssetMetaFile newMeta;
        newMeta.SourceHandle = AssetHandle();
        newMeta.SourceHash = ComputeSourceHash(sourcePath);
        if (!SaveMeta(sourcePath, newMeta))
        {
            Log::CoreError("Failed to save asset metadata for {0}", sourcePath.string());
        }
        return newMeta;
    }

    KBRAssert(metaResult.has_value(), "Meta file should exist for source: {0}", sourcePath.string());

    return metaResult.value();
}

std::optional<AssetMetaFile> AssetMetaService::LoadMeta(const std::filesystem::path& sourcePath) const 
{
    KBR_TRACY_FUNCTION();

    const auto metaPath = MetaPathFor(sourcePath);
    if (!std::filesystem::exists(metaPath)) {
        Log::CoreWarn("Meta file does not exist for source: {0}", sourcePath.string());
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
        if (config["ImporterVersion"]) 
            meta.ImporterVersion = config["ImporterVersion"].as<uint32_t>();
        if (config["ImporterType"]) 
            meta.ImporterType = static_cast<ImporterType>(config["ImporterType"].as<uint8_t>());

        const YAML::Node sourceHandle = config["SourceHandle"] 
            ? config["SourceHandle"] 
            : config["Handle"];

        if (sourceHandle) 
            meta.SourceHandle = AssetHandle(sourceHandle.as<uint64_t>());
        if (config["SourceHash"]) 
            meta.SourceHash = config["SourceHash"].as<std::string>();

        if (config["SubAssets"])
        {
            for (const auto& node : config["SubAssets"]) 
            {
                SubAssetMetaEntry entry;
                if (node["LocalKey"]) entry.LocalKey = node["LocalKey"].as<std::string>();
                if (node["Handle"]) entry.Handle = AssetHandle(node["Handle"].as<uint64_t>());
                if (node["Kind"]) entry.Kind = static_cast<SubAssetKind>(node["Kind"].as<uint8_t>());
                meta.SubAssets.push_back(std::move(entry));
            }
        }
        if (config["TextureSrgb"]) 
        {
            for (const auto& entry : config["TextureSrgb"]) 
            {
                meta.TextureSrgb[entry.first.as<std::string>()] = entry.second.as<bool>();
            }
        }

        if (config["AnimationClips"])
        {
            for (const auto& clipNode : config["AnimationClips"])
            {
                const std::string clipKey = clipNode.first.as<std::string>();
                AnimationClipMetaEntry clipMeta{};

                if (const YAML::Node markersNode = clipNode.second["Markers"])
                {
                    for (const auto& markerNode : markersNode)
                    {
                        AnimationMarkerFile marker{};
                        marker.Time = markerNode["Time"].as<float>(0.0f);
                        marker.Name = markerNode["Name"].as<std::string>("");
                        clipMeta.Markers.push_back(std::move(marker));
                    }
                }

                if (const YAML::Node eventsNode = clipNode.second["Events"])
                {
                    for (const auto& eventNode : eventsNode)
                    {
                        AnimationEventFile event{};
                        event.Time = eventNode["Time"].as<float>(0.0f);
                        event.Duration = eventNode["Duration"].as<float>(0.0f);
                        event.Name = eventNode["Name"].as<std::string>("");
                        event.Payload = eventNode["Payload"].as<std::string>("");
                        event.FireInEditor = eventNode["FireInEditor"].as<bool>(false);
                        event.MinTriggerWeight = eventNode["MinTriggerWeight"].as<float>(0.0f);
                        event.Flags = eventNode["Flags"].as<uint32_t>(0);

                        if (const YAML::Node parametersNode = eventNode["Parameters"])
                        {
                            for (const auto& parameterNode : parametersNode)
                            {
                                AnimationEventParameterFile parameter{};
                                parameter.Name = parameterNode["Name"].as<std::string>("");
                                parameter.Type = ParseAnimationEventParameterType(
                                    parameterNode["Type"].as<std::string>("Float"));
                                parameter.BoolValue = parameterNode["BoolValue"].as<bool>(false);
                                parameter.IntValue = parameterNode["IntValue"].as<int32_t>(0);
                                parameter.FloatValue = parameterNode["FloatValue"].as<float>(0.0f);
                                if (const YAML::Node vectorNode = parameterNode["Vector3"];
                                    vectorNode && vectorNode.IsSequence() && vectorNode.size() == 3)
                                {
                                    parameter.VectorValue = glm::vec3(
                                        vectorNode[0].as<float>(0.0f),
                                        vectorNode[1].as<float>(0.0f),
                                        vectorNode[2].as<float>(0.0f));
                                }
                                parameter.StringValue = parameterNode["StringValue"].as<std::string>("");
                                parameter.AssetValue = parameterNode["AssetValue"].as<uint64_t>(0);
                                event.Parameters.push_back(std::move(parameter));
                            }
                        }

                        clipMeta.Events.push_back(std::move(event));
                    }
                }

                meta.AnimationClips[clipKey] = std::move(clipMeta);
            }
        }

        return meta;
    } 
    catch (const YAML::Exception& e) 
    {
        Log::CoreWarn("Invalid meta file {0}: {1}", metaPath.string(), e.what());
        return std::nullopt;
    }
}

bool AssetMetaService::SaveMeta(const std::filesystem::path& sourcePath, const AssetMetaFile& meta) const
{
    KBR_TRACY_FUNCTION();

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
    out << YAML::EndSeq;
    if (!meta.TextureSrgb.empty()) {
        out << YAML::Key << "TextureSrgb" << YAML::Value << YAML::BeginMap;
        for (const auto& [key, value] : meta.TextureSrgb)
            out << YAML::Key << key << YAML::Value << value;
        out << YAML::EndMap;
    }
    if (!meta.AnimationClips.empty()) {
        out << YAML::Key << "AnimationClips" << YAML::Value << YAML::BeginMap;
        for (const auto& [clipKey, clipMeta] : meta.AnimationClips) {
            out << YAML::Key << clipKey << YAML::Value << YAML::BeginMap;

            auto markers = clipMeta.Markers;
            std::ranges::stable_sort(markers, {}, &AnimationMarkerFile::Time);
            if (!markers.empty()) {
                out << YAML::Key << "Markers" << YAML::Value << YAML::BeginSeq;
                for (const auto& marker : markers) {
                    out << YAML::BeginMap
                        << YAML::Key << "Time" << YAML::Value << marker.Time
                        << YAML::Key << "Name" << YAML::Value << marker.Name
                        << YAML::EndMap;
                }
                out << YAML::EndSeq;
            }

            auto events = clipMeta.Events;
            std::ranges::stable_sort(events, {}, &AnimationEventFile::Time);
            if (!events.empty()) {
                out << YAML::Key << "Events" << YAML::Value << YAML::BeginSeq;
                for (const auto& event : events) {
                    out << YAML::BeginMap
                        << YAML::Key << "Time" << YAML::Value << event.Time
                        << YAML::Key << "Duration" << YAML::Value << event.Duration
                        << YAML::Key << "Name" << YAML::Value << event.Name
                        << YAML::Key << "Payload" << YAML::Value << event.Payload
                        << YAML::Key << "FireInEditor" << YAML::Value << event.FireInEditor
                        << YAML::Key << "MinTriggerWeight" << YAML::Value << event.MinTriggerWeight
                        << YAML::Key << "Flags" << YAML::Value << event.Flags;

                    if (!event.Parameters.empty())
                    {
                        out << YAML::Key << "Parameters" << YAML::Value << YAML::BeginSeq;
                        for (const auto& parameter : event.Parameters)
                        {
                            out << YAML::BeginMap
                                << YAML::Key << "Name" << YAML::Value << parameter.Name
                                << YAML::Key << "Type" << YAML::Value
                                << AnimationEventParameterTypeToString(parameter.Type);

                            switch (parameter.Type)
                            {
                            case AnimationEventParameterTypeFile::Bool:
                                out << YAML::Key << "BoolValue" << YAML::Value << parameter.BoolValue;
                                break;
                            case AnimationEventParameterTypeFile::Int:
                                out << YAML::Key << "IntValue" << YAML::Value << parameter.IntValue;
                                break;
                            case AnimationEventParameterTypeFile::Float:
                                out << YAML::Key << "FloatValue" << YAML::Value << parameter.FloatValue;
                                break;
                            case AnimationEventParameterTypeFile::Vector3:
                                out << YAML::Key << "Vector3" << YAML::Value
                                    << YAML::Flow << YAML::BeginSeq
                                    << parameter.VectorValue.x
                                    << parameter.VectorValue.y
                                    << parameter.VectorValue.z
                                    << YAML::EndSeq;
                                break;
                            case AnimationEventParameterTypeFile::String:
                                out << YAML::Key << "StringValue" << YAML::Value << parameter.StringValue;
                                break;
                            case AnimationEventParameterTypeFile::Asset:
                                out << YAML::Key << "AssetValue" << YAML::Value << parameter.AssetValue;
                                break;
                            }

                            out << YAML::EndMap;
                        }
                        out << YAML::EndSeq;
                    }

                    out << YAML::EndMap;
                }
                out << YAML::EndSeq;
            }

            out << YAML::EndMap;
        }
        out << YAML::EndMap;
    }
    out << YAML::EndMap;
    return WriteAtomic(MetaPathFor(sourcePath), out.c_str());
}

bool AssetMetaService::RebindMetaOnRename(const std::filesystem::path& oldPath, const std::filesystem::path& newPath) const
{
    KBR_TRACY_FUNCTION();

    const auto oldMeta = MetaPathFor(oldPath);
    const auto newMeta = MetaPathFor(newPath);
    std::error_code ec;
    if (!std::filesystem::exists(oldMeta, ec))
        return false;
    std::filesystem::create_directories(newMeta.parent_path(), ec);
    if (ec) return false;
    std::filesystem::rename(oldMeta, newMeta, ec);
    if (ec) {
        const auto meta = LoadMeta(oldPath);
        return meta.has_value() && SaveMeta(newPath, *meta) &&
               (std::filesystem::remove(oldMeta, ec), !ec);
    }
    return true;
}

std::filesystem::path AssetMetaService::MetaPathFor(const std::filesystem::path& sourcePath) 
{
    KBR_TRACY_FUNCTION();

    // Append .meta to the full filename so every source file gets a unique meta
    // (e.g. foo.png.meta and foo.ktx.meta do not collide on the same stem).
    std::filesystem::path metaPath = sourcePath;
    metaPath += ".meta";
    return metaPath;
}

std::string AssetMetaService::ComputeSourceHash(const std::filesystem::path& sourcePath) 
{
    KBR_TRACY_FUNCTION();

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

bool AssetMetaService::WriteAtomic(const std::filesystem::path& filePath, const std::string& content) const
{
    KBR_TRACY_FUNCTION();

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
    KBR_TRACY_FUNCTION();

    std::scoped_lock _(m_Mutex);

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        Log::CoreError("Failed to open file for reading: {0}", path.string());
        return std::nullopt;
    }
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return content;
}

}