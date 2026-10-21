#pragma once

#include "Assets/Formats/NativeAssetFormat.hpp"

#include <filesystem>

namespace Kerberos
{
    class NativeAssetSerializer
    {
    public:
        static bool SerializeMesh(const NativeMeshPayload& payload, const std::filesystem::path& path);
        static bool DeserializeMesh(const std::filesystem::path& path, NativeMeshPayload& payload);

        static bool SerializeSceneManifest(const GltfSceneManifest& manifest, const std::filesystem::path& path);
        static bool DeserializeSceneManifest(const std::filesystem::path& path, GltfSceneManifest& manifest);
        static bool SerializeRecord(const NativeAssetRecord& record, const std::filesystem::path& path);
        static bool DeserializeRecord(const std::filesystem::path& path, NativeAssetRecord& record);
    };
}
