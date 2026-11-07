#pragma once

#include "Assets/Formats/NativeAssetFormat.hpp"

#include <filesystem>

namespace Kerberos
{
    struct SkeletonFile;
    struct AnimationClipFile;

    class NativeAssetSerializer
    {
    public:
        static bool SerializeMesh(const NativeMeshPayload& payload, const std::filesystem::path& path);
        static bool DeserializeMesh(const std::filesystem::path& path, NativeMeshPayload& payload);

        static bool SerializeSkeletalMesh(const NativeMeshPayload& payload, const std::filesystem::path& path);
        static bool DeserializeSkeletalMesh(const std::filesystem::path& path, NativeMeshPayload& payload);

        static bool SerializeSkeleton(const SkeletonFile& skeleton, const std::filesystem::path& path);
        static bool DeserializeSkeleton(const std::filesystem::path& path, SkeletonFile& skeleton);

        static bool SerializeAnimationClip(const AnimationClipFile& clip, const std::filesystem::path& path);
        static bool DeserializeAnimationClip(const std::filesystem::path& path, AnimationClipFile& clip);

        static bool SerializeSceneManifest(const GltfSceneManifest& manifest, const std::filesystem::path& path);
        static bool DeserializeSceneManifest(const std::filesystem::path& path, GltfSceneManifest& manifest);
        static bool SerializeRecord(const NativeAssetRecord& record, const std::filesystem::path& path);
        static bool DeserializeRecord(const std::filesystem::path& path, NativeAssetRecord& record);
    };
}
