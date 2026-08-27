#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Kerberos
{
    inline constexpr uint32_t NativeAssetFormatVersion = 2;

    struct NativeAssetHeader
    {
        uint32_t Magic = 0;
        uint16_t Version = static_cast<uint16_t>(NativeAssetFormatVersion);
        uint16_t Type = 0;
        uint32_t PayloadSize = 0;
    };

    struct NativeMeshPayload
    {
        uint32_t Version = NativeAssetFormatVersion;
        uint32_t VertexStride = 0;
        std::vector<uint8_t> VertexData;
        std::vector<uint32_t> Indices;
    };

    struct NativeSubasset
    {
        uint64_t StableId = 0;
        std::string Kind;
        std::string Name;
        uint32_t SourceIndex = 0;
    };

    struct NativeSceneNode
    {
        uint64_t StableId = 0;
        std::string Name;
        int32_t ParentIndex = -1;
        int32_t MeshIndex = -1;
        int32_t SkinIndex = -1;
    };

    struct GltfSceneManifest
    {
        uint32_t Version = NativeAssetFormatVersion;
        std::string SourcePath;
        std::vector<NativeSubasset> Subassets;
        std::vector<NativeSceneNode> Nodes;
    };

    struct NativeAssetRecord
    {
        uint32_t Version = NativeAssetFormatVersion;
        std::string Kind;
        uint32_t SourceIndex = 0;
        std::string LocalKey;
        std::string Data;
    };
}
