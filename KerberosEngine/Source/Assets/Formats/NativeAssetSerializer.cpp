#include "kbrpch.hpp"
#include "Assets/Formats/NativeAssetSerializer.hpp"

#include "Assets/Formats/AnimationFormat.hpp"
#include "Assets/Formats/SkeletonFormat.hpp"

#include <fstream>
#include <limits>

namespace Kerberos
{
    namespace
    {
        constexpr uint32_t MeshMagic = 0x4D52424B; // KBRM
        constexpr uint32_t SkeletalMeshMagic = 0x534D524B; // KBRM skeletal
        constexpr uint32_t SceneMagic = 0x5347524B; // K R G S
        constexpr uint32_t RecordMagic = 0x5242524B; // KBRR
        constexpr uint32_t SkeletonMagic = 0x4B53424B; // KBSK
        constexpr uint32_t AnimationMagic = 0x414E424B; // KBNA

        constexpr uint16_t MeshType = 1;
        constexpr uint16_t SceneType = 2;
        constexpr uint16_t RecordType = 3;
        constexpr uint16_t SkeletonType = 4;
        constexpr uint16_t AnimationType = 5;

        constexpr uint32_t MaxCollectionCount = 16 * 1024 * 1024;

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

        template<typename T>
        bool WriteVector(std::ostream& stream, const std::vector<T>& values)
        {
            const uint32_t count = static_cast<uint32_t>(values.size());
            if (!Write(stream, count))
                return false;
            if (count == 0)
                return true;
            stream.write(reinterpret_cast<const char*>(values.data()),
                static_cast<std::streamsize>(sizeof(T) * values.size()));
            return stream.good();
        }

        template<typename T>
        bool ReadVector(std::istream& stream, std::vector<T>& values)
        {
            uint32_t count = 0;
            if (!Read(stream, count) || count > MaxCollectionCount)
                return false;
            values.clear();
            values.resize(count);
            if (count == 0)
                return true;
            stream.read(reinterpret_cast<char*>(values.data()),
                static_cast<std::streamsize>(sizeof(T) * values.size()));
            return stream.good();
        }

        bool WriteBool(std::ostream& stream, const bool value)
        {
            const uint8_t stored = value ? 1 : 0;
            return Write(stream, stored);
        }

        bool ReadBool(std::istream& stream, bool& value)
        {
            uint8_t stored = 0;
            if (!Read(stream, stored))
                return false;
            value = stored != 0;
            return true;
        }

        bool ValidHeader(const NativeAssetHeader& header, const uint32_t magic, const uint16_t type)
        {
            return header.Magic == magic
                && header.Version > 0 && header.Version <= NativeAssetFormatVersion
                && header.Type == type
                && header.PayloadSize <= 256 * 1024 * 1024;
        }

        NativeAssetHeader MakeHeader(const uint32_t magic, const uint32_t version, const uint16_t type)
        {
            return NativeAssetHeader{
                .Magic = magic,
                .Version = static_cast<uint16_t>(version),
                .Type = type,
                .PayloadSize = 0,
            };
        }
    }

    bool NativeAssetSerializer::SerializeMesh(const NativeMeshPayload& payload, const std::filesystem::path& path)
    {
        std::ofstream stream(path, std::ios::binary);
        if (!stream)
            return false;

        const NativeAssetHeader header{
            .Magic = MeshMagic,
            .Version = static_cast<uint16_t>(payload.Version),
            .Type = MeshType,
            .PayloadSize = static_cast<uint32_t>(payload.VertexData.size() + payload.Indices.size() * sizeof(uint32_t)),
        };
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
        if (!stream || !Read(stream, header) || !ValidHeader(header, MeshMagic, MeshType)
            || !Read(stream, payload.VertexStride) || !Read(stream, vertexBytes) || !Read(stream, indexCount)
            || vertexBytes > header.PayloadSize || indexCount > (header.PayloadSize / sizeof(uint32_t)))
            return false;

        payload.Version = header.Version;
        payload.VertexData.resize(vertexBytes);
        payload.Indices.resize(indexCount);
        return (vertexBytes == 0 || (stream.read(reinterpret_cast<char*>(payload.VertexData.data()), vertexBytes), stream.good()))
            && (indexCount == 0 || (stream.read(reinterpret_cast<char*>(payload.Indices.data()), indexCount * sizeof(uint32_t)), stream.good()));
    }

    bool NativeAssetSerializer::SerializeSkeletalMesh(const NativeMeshPayload& payload,
        const std::filesystem::path& path)
    {
        std::ofstream stream(path, std::ios::binary);
        if (!stream)
            return false;

        const NativeAssetHeader header{
            .Magic = SkeletalMeshMagic,
            .Version = static_cast<uint16_t>(payload.Version),
            .Type = MeshType,
            .PayloadSize = static_cast<uint32_t>(payload.VertexData.size() + payload.Indices.size() * sizeof(uint32_t)),
        };
        return Write(stream, header)
            && Write(stream, payload.VertexStride)
            && Write(stream, static_cast<uint32_t>(payload.VertexData.size()))
            && Write(stream, static_cast<uint32_t>(payload.Indices.size()))
            && (payload.VertexData.empty() || (stream.write(reinterpret_cast<const char*>(payload.VertexData.data()), payload.VertexData.size()), stream.good()))
            && (payload.Indices.empty() || (stream.write(reinterpret_cast<const char*>(payload.Indices.data()), payload.Indices.size() * sizeof(uint32_t)), stream.good()));
    }

    bool NativeAssetSerializer::DeserializeSkeletalMesh(const std::filesystem::path& path, NativeMeshPayload& payload)
    {
        std::ifstream stream(path, std::ios::binary);
        NativeAssetHeader header;
        uint32_t vertexBytes = 0;
        uint32_t indexCount = 0;
        if (!stream || !Read(stream, header) || !ValidHeader(header, SkeletalMeshMagic, MeshType)
            || !Read(stream, payload.VertexStride) || !Read(stream, vertexBytes) || !Read(stream, indexCount)
            || vertexBytes > header.PayloadSize || indexCount > (header.PayloadSize / sizeof(uint32_t)))
            return false;

        payload.Version = header.Version;
        payload.VertexData.resize(vertexBytes);
        payload.Indices.resize(indexCount);
        return (vertexBytes == 0 || (stream.read(reinterpret_cast<char*>(payload.VertexData.data()), vertexBytes), stream.good()))
            && (indexCount == 0 || (stream.read(reinterpret_cast<char*>(payload.Indices.data()), indexCount * sizeof(uint32_t)), stream.good()));
    }

    bool NativeAssetSerializer::SerializeSkeleton(const SkeletonFile& skeleton, const std::filesystem::path& path)
    {
        std::ofstream stream(path, std::ios::binary);
        if (!stream)
            return false;

        if (!Write(stream, MakeHeader(SkeletonMagic, skeleton.Version, SkeletonType))
            || !WriteString(stream, skeleton.LocalKey)
            || !Write(stream, skeleton.SourceIndex)
            || !WriteString(stream, skeleton.Name)
            || !Write(stream, static_cast<uint32_t>(skeleton.Joints.size())))
            return false;

        for (const auto& joint : skeleton.Joints)
        {
            if (!WriteString(stream, joint.Name)
                || !Write(stream, joint.ParentJoint)
                || !Write(stream, joint.SourceNode)
                || !Write(stream, joint.RestTranslation)
                || !Write(stream, joint.RestRotation)
                || !Write(stream, joint.RestScale)
                || !Write(stream, joint.InverseBind))
                return false;
        }
        return stream.good();
    }

    bool NativeAssetSerializer::DeserializeSkeleton(const std::filesystem::path& path, SkeletonFile& skeleton)
    {
        std::ifstream stream(path, std::ios::binary);
        NativeAssetHeader header;
        uint32_t jointCount = 0;
        if (!stream || !Read(stream, header) || !ValidHeader(header, SkeletonMagic, SkeletonType)
            || !ReadString(stream, skeleton.LocalKey)
            || !Read(stream, skeleton.SourceIndex)
            || !ReadString(stream, skeleton.Name)
            || !Read(stream, jointCount) || jointCount > MaxCollectionCount)
            return false;

        skeleton.Version = header.Version;
        skeleton.Joints.clear();
        skeleton.Joints.resize(jointCount);
        for (auto& joint : skeleton.Joints)
        {
            if (!ReadString(stream, joint.Name)
                || !Read(stream, joint.ParentJoint)
                || !Read(stream, joint.SourceNode)
                || !Read(stream, joint.RestTranslation)
                || !Read(stream, joint.RestRotation)
                || !Read(stream, joint.RestScale)
                || !Read(stream, joint.InverseBind))
                return false;
        }
        return stream.good();
    }

    bool NativeAssetSerializer::SerializeAnimationClip(const AnimationClipFile& clip, const std::filesystem::path& path)
    {
        std::ofstream stream(path, std::ios::binary);
        if (!stream)
            return false;

        if (!Write(stream, MakeHeader(AnimationMagic, clip.Version, AnimationType))
            || !WriteString(stream, clip.LocalKey)
            || !Write(stream, clip.SourceIndex)
            || !WriteString(stream, clip.Name)
            || !WriteString(stream, clip.SkeletonKey)
            || !Write(stream, clip.JointCount)
            || !Write(stream, clip.DurationSeconds)
            || !Write(stream, static_cast<uint32_t>(clip.Tracks.size())))
            return false;

        for (const auto& track : clip.Tracks)
        {
            const uint8_t pathValue = static_cast<uint8_t>(track.Path);
            const uint8_t interpolation = static_cast<uint8_t>(track.Interpolation);
            if (!Write(stream, track.TargetNode)
                || !Write(stream, track.JointIndex)
                || !Write(stream, pathValue)
                || !Write(stream, interpolation)
                || !WriteVector(stream, track.Times)
                || !WriteVector(stream, track.Values)
                || !WriteVector(stream, track.InTangents)
                || !WriteVector(stream, track.OutTangents))
                return false;
        }
        return stream.good();
    }

    bool NativeAssetSerializer::DeserializeAnimationClip(const std::filesystem::path& path, AnimationClipFile& clip)
    {
        std::ifstream stream(path, std::ios::binary);
        NativeAssetHeader header;
        uint32_t trackCount = 0;
        if (!stream || !Read(stream, header) || !ValidHeader(header, AnimationMagic, AnimationType)
            || !ReadString(stream, clip.LocalKey)
            || !Read(stream, clip.SourceIndex)
            || !ReadString(stream, clip.Name)
            || !ReadString(stream, clip.SkeletonKey)
            || !Read(stream, clip.JointCount)
            || !Read(stream, clip.DurationSeconds)
            || !Read(stream, trackCount) || trackCount > MaxCollectionCount)
            return false;

        clip.Version = header.Version;
        clip.Tracks.clear();
        clip.Tracks.resize(trackCount);
        for (auto& track : clip.Tracks)
        {
            uint8_t pathValue = 0;
            uint8_t interpolation = 0;
            if (!Read(stream, track.TargetNode)
                || !Read(stream, track.JointIndex)
                || !Read(stream, pathValue)
                || !Read(stream, interpolation)
                || pathValue > static_cast<uint8_t>(AnimTrackPath::Scale)
                || interpolation > static_cast<uint8_t>(AnimationClipInterpolation::CubicSpline)
                || !ReadVector(stream, track.Times)
                || !ReadVector(stream, track.Values)
                || !ReadVector(stream, track.InTangents)
                || !ReadVector(stream, track.OutTangents))
                return false;

            track.Path = static_cast<AnimTrackPath>(pathValue);
            track.Interpolation = static_cast<AnimationClipInterpolation>(interpolation);
        }
        return stream.good();
    }

    bool NativeAssetSerializer::SerializeSceneManifest(const GltfSceneManifest& manifest, const std::filesystem::path& path)
    {
        std::ofstream stream(path, std::ios::binary);
        if (!stream)
            return false;
        if (!Write(stream, MakeHeader(SceneMagic, manifest.Version, SceneType))
            || !WriteString(stream, manifest.SourcePath)
            || !Write(stream, static_cast<uint32_t>(manifest.Subassets.size())))
            return false;
        for (const auto& asset : manifest.Subassets)
            if (!Write(stream, asset.StableId) || !WriteString(stream, asset.Kind) || !WriteString(stream, asset.Name) || !Write(stream, asset.SourceIndex))
                return false;
        if (!Write(stream, static_cast<uint32_t>(manifest.Nodes.size())))
            return false;
        for (const auto& node : manifest.Nodes)
        {
            if (!Write(stream, node.StableId) || !WriteString(stream, node.Name)
                || !Write(stream, node.ParentIndex) || !Write(stream, node.SkinIndex)
                || !Write(stream, node.Translation) || !Write(stream, node.Rotation) || !Write(stream, node.Scale)
                || !Write(stream, static_cast<uint32_t>(node.Primitives.size())))
                return false;
            for (const auto& primitive : node.Primitives)
                if (!Write(stream, primitive.MeshIndex) || !Write(stream, primitive.PrimitiveIndex)
                    || !Write(stream, primitive.MaterialIndex) || !WriteBool(stream, primitive.Skinned))
                    return false;
        }

        if (!Write(stream, static_cast<uint32_t>(manifest.Skins.size())))
            return false;
        for (const auto& skin : manifest.Skins)
        {
            if (!Write(stream, skin.SkeletonNode)
                || !Write(stream, static_cast<uint32_t>(skin.JointNodes.size())))
                return false;
            if (!skin.JointNodes.empty()
                && (stream.write(reinterpret_cast<const char*>(skin.JointNodes.data()),
                        static_cast<std::streamsize>(sizeof(int32_t) * skin.JointNodes.size())),
                    !stream.good()))
                return false;
            if (!Write(stream, static_cast<uint32_t>(skin.AnimationKeys.size())))
                return false;
            for (const auto& key : skin.AnimationKeys)
                if (!WriteString(stream, key))
                    return false;
        }
        return stream.good();
    }

    bool NativeAssetSerializer::DeserializeSceneManifest(const std::filesystem::path& path, GltfSceneManifest& manifest)
    {
        std::ifstream stream(path, std::ios::binary);
        NativeAssetHeader header;
        uint32_t count = 0;
        if (!stream || !Read(stream, header) || !ValidHeader(header, SceneMagic, SceneType) || !ReadString(stream, manifest.SourcePath) || !Read(stream, count) || count > 1'000'000)
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
        manifest.Skins.clear();
        for (auto& node : manifest.Nodes)
        {
            if (!Read(stream, node.StableId) || !ReadString(stream, node.Name)
                || !Read(stream, node.ParentIndex))
                return false;

            if (header.Version < 3)
            {
                int32_t meshIndex = -1;
                if (!Read(stream, meshIndex) || !Read(stream, node.SkinIndex))
                    return false;
                if (meshIndex >= 0)
                    node.Primitives.push_back({
                        .MeshIndex = static_cast<uint32_t>(meshIndex),
                        .PrimitiveIndex = 0,
                        .MaterialIndex = -1,
                        .Skinned = false,
                    });
            }
            else
            {
                if (!Read(stream, node.SkinIndex))
                    return false;
                if (header.Version >= 4)
                {
                    if (!Read(stream, node.Translation) || !Read(stream, node.Rotation) || !Read(stream, node.Scale))
                        return false;
                }
                uint32_t primitiveCount = 0;
                if (!Read(stream, primitiveCount) || primitiveCount > 1'000'000)
                    return false;
                node.Primitives.resize(primitiveCount);
                for (auto& primitive : node.Primitives)
                {
                    if (!Read(stream, primitive.MeshIndex) || !Read(stream, primitive.PrimitiveIndex)
                        || !Read(stream, primitive.MaterialIndex))
                        return false;
                    if (header.Version >= 4)
                    {
                        if (!ReadBool(stream, primitive.Skinned))
                            return false;
                    }
                }
            }
        }

        if (header.Version >= 4)
        {
            if (!Read(stream, count) || count > 1'000'000)
                return false;
            manifest.Skins.resize(count);
            for (auto& skin : manifest.Skins)
            {
                uint32_t jointCount = 0;
                if (!Read(stream, skin.SkeletonNode) || !Read(stream, jointCount) || jointCount > MaxCollectionCount)
                    return false;
                skin.JointNodes.resize(jointCount);
                if (jointCount > 0
                    && (stream.read(reinterpret_cast<char*>(skin.JointNodes.data()),
                            static_cast<std::streamsize>(sizeof(int32_t) * jointCount)),
                        !stream.good()))
                    return false;
                uint32_t animationCount = 0;
                if (!Read(stream, animationCount) || animationCount > MaxCollectionCount)
                    return false;
                skin.AnimationKeys.clear();
                skin.AnimationKeys.resize(animationCount);
                for (auto& key : skin.AnimationKeys)
                    if (!ReadString(stream, key))
                        return false;
            }
        }
        return stream.good();
    }

    bool NativeAssetSerializer::SerializeRecord(const NativeAssetRecord& record, const std::filesystem::path& path)
    {
        std::ofstream stream(path, std::ios::binary);
        if (!stream) return false;
        const NativeAssetHeader header{ 
            .Magic = RecordMagic, 
            .Version = static_cast<uint16_t>(record.Version), 
            .Type = RecordType,
            .PayloadSize = static_cast<uint32_t>(record.Kind.size() + record.LocalKey.size() + record.Data.size()) 
        };
        return Write(stream, header) && WriteString(stream, record.Kind) &&
            Write(stream, record.SourceIndex) && WriteString(stream, record.LocalKey) &&
            WriteString(stream, record.Data);
    }

    bool NativeAssetSerializer::DeserializeRecord(const std::filesystem::path& path, NativeAssetRecord& record)
    {
        std::ifstream stream(path, std::ios::binary);
        NativeAssetHeader header;
        if (!stream || !Read(stream, header) || !ValidHeader(header, RecordMagic, RecordType) ||
            !ReadString(stream, record.Kind) || !Read(stream, record.SourceIndex) ||
            !ReadString(stream, record.LocalKey) || !ReadString(stream, record.Data))
            return false;
        record.Version = header.Version;
        return true;
    }
}
