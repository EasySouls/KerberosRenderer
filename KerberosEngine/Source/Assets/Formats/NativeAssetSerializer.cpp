#include "kbrpch.hpp"
#include "Assets/Formats/NativeAssetSerializer.hpp"

#include <fstream>
#include <limits>

namespace Kerberos
{
    namespace
    {
        constexpr uint32_t MeshMagic = 0x4D52424B; // KBRM
        constexpr uint32_t SceneMagic = 0x5347524B; // K R G S
        constexpr uint32_t RecordMagic = 0x5242524B; // KBRR

        template<typename T>
        bool Write(std::ostream& stream, const T& value)
        {
            stream.write(reinterpret_cast<const char*>(&value), sizeof(T));
            return stream.good();
        }

        template<typename T>
        bool Read(std::istream& stream, T& value)
        {
            stream.read(reinterpret_cast<char*>(&value), sizeof(T));
            return stream.good();
        }

        bool WriteString(std::ostream& stream, const std::string& value)
        {
            const uint32_t size = static_cast<uint32_t>(value.size());
            return Write(stream, size) && (size == 0 || (stream.write(value.data(), size), stream.good()));
        }

        bool ReadString(std::istream& stream, std::string& value)
        {
            uint32_t size = 0;
            if (!Read(stream, size) || size > 16 * 1024 * 1024)
                return false;
            value.resize(size);
            return size == 0 || (stream.read(value.data(), size), stream.good());
        }

        bool ValidHeader(const NativeAssetHeader& header, uint32_t magic, uint16_t type)
        {
            return header.Magic == magic
                && header.Version > 0 && header.Version <= NativeAssetFormatVersion
                && header.Type == type
                && header.PayloadSize <= 256 * 1024 * 1024;
        }
    }

    bool NativeAssetSerializer::SerializeMesh(const NativeMeshPayload& payload, const std::filesystem::path& path)
    {
        std::ofstream stream(path, std::ios::binary);
        if (!stream)
            return false;

        NativeAssetHeader header{ MeshMagic, static_cast<uint16_t>(payload.Version), 1,
            static_cast<uint32_t>(payload.VertexData.size() + payload.Indices.size() * sizeof(uint32_t)) };
        return Write(stream, header)
            && Write(stream, payload.VertexStride)
            && Write(stream, static_cast<uint32_t>(payload.VertexData.size()))
            && Write(stream, static_cast<uint32_t>(payload.Indices.size()))
            && (payload.VertexData.empty() || (stream.write(reinterpret_cast<const char*>(payload.VertexData.data()), payload.VertexData.size()), stream.good()))
            && (payload.Indices.empty() || (stream.write(reinterpret_cast<const char*>(payload.Indices.data()), payload.Indices.size() * sizeof(uint32_t)), stream.good()));
    }

    bool NativeAssetSerializer::DeserializeMesh(const std::filesystem::path& path, NativeMeshPayload& payload)
    {
        std::ifstream stream(path, std::ios::binary);
        NativeAssetHeader header;
        uint32_t vertexBytes = 0;
        uint32_t indexCount = 0;
        if (!stream || !Read(stream, header) || !ValidHeader(header, MeshMagic, 1)
            || !Read(stream, payload.VertexStride) || !Read(stream, vertexBytes) || !Read(stream, indexCount)
            || vertexBytes > header.PayloadSize || indexCount > (header.PayloadSize / sizeof(uint32_t)))
            return false;

        payload.Version = header.Version;
        payload.VertexData.resize(vertexBytes);
        payload.Indices.resize(indexCount);
        return (vertexBytes == 0 || (stream.read(reinterpret_cast<char*>(payload.VertexData.data()), vertexBytes), stream.good()))
            && (indexCount == 0 || (stream.read(reinterpret_cast<char*>(payload.Indices.data()), indexCount * sizeof(uint32_t)), stream.good()));
    }

    bool NativeAssetSerializer::SerializeSceneManifest(const GltfSceneManifest& manifest, const std::filesystem::path& path)
    {
        std::ofstream stream(path, std::ios::binary);
        if (!stream)
            return false;
        NativeAssetHeader header{ SceneMagic, static_cast<uint16_t>(manifest.Version), 2, 0 };
        if (!Write(stream, header) || !WriteString(stream, manifest.SourcePath)
            || !Write(stream, static_cast<uint32_t>(manifest.Subassets.size())))
            return false;
        for (const auto& asset : manifest.Subassets)
            if (!Write(stream, asset.StableId) || !WriteString(stream, asset.Kind) || !WriteString(stream, asset.Name) || !Write(stream, asset.SourceIndex))
                return false;
        if (!Write(stream, static_cast<uint32_t>(manifest.Nodes.size())))
            return false;
        for (const auto& node : manifest.Nodes)
            if (!Write(stream, node.StableId) || !WriteString(stream, node.Name) || !Write(stream, node.ParentIndex)
                || !Write(stream, node.MeshIndex) || !Write(stream, node.SkinIndex))
                return false;
        return stream.good();
    }

    bool NativeAssetSerializer::DeserializeSceneManifest(const std::filesystem::path& path, GltfSceneManifest& manifest)
    {
        std::ifstream stream(path, std::ios::binary);
        NativeAssetHeader header;
        uint32_t count = 0;
        if (!stream || !Read(stream, header) || !ValidHeader(header, SceneMagic, 2) || !ReadString(stream, manifest.SourcePath) || !Read(stream, count) || count > 1'000'000)
            return false;
        manifest.Version = header.Version;
        manifest.Subassets.clear();
        manifest.Subassets.resize(count);
        for (auto& asset : manifest.Subassets)
            if (!Read(stream, asset.StableId) || !ReadString(stream, asset.Kind) || !ReadString(stream, asset.Name) || !Read(stream, asset.SourceIndex))
                return false;
        if (!Read(stream, count) || count > 1'000'000)
            return false;
        manifest.Nodes.clear();
        manifest.Nodes.resize(count);
        for (auto& node : manifest.Nodes)
            if (!Read(stream, node.StableId) || !ReadString(stream, node.Name) || !Read(stream, node.ParentIndex)
                || !Read(stream, node.MeshIndex) || !Read(stream, node.SkinIndex))
                return false;
        return stream.good();
    }

    bool NativeAssetSerializer::SerializeRecord(const NativeAssetRecord& record, const std::filesystem::path& path)
    {
        std::ofstream stream(path, std::ios::binary);
        if (!stream) return false;
        NativeAssetHeader header{ RecordMagic, static_cast<uint16_t>(record.Version), 3,
            static_cast<uint32_t>(record.Kind.size() + record.LocalKey.size() + record.Data.size()) };
        return Write(stream, header) && WriteString(stream, record.Kind) &&
            Write(stream, record.SourceIndex) && WriteString(stream, record.LocalKey) &&
            WriteString(stream, record.Data);
    }

    bool NativeAssetSerializer::DeserializeRecord(const std::filesystem::path& path, NativeAssetRecord& record)
    {
        std::ifstream stream(path, std::ios::binary);
        NativeAssetHeader header;
        if (!stream || !Read(stream, header) || !ValidHeader(header, RecordMagic, 3) ||
            !ReadString(stream, record.Kind) || !Read(stream, record.SourceIndex) ||
            !ReadString(stream, record.LocalKey) || !ReadString(stream, record.Data))
            return false;
        record.Version = header.Version;
        return true;
    }
}
