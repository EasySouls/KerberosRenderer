#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Kerberos
{
    inline constexpr uint32_t NativeAssetFormatVersion = 4;

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
        int32_t SkinIndex = -1;

        glm::vec3 Translation = glm::vec3(0.0f);
        glm::quat Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        glm::vec3 Scale = glm::vec3(1.0f);

        struct Primitive
        {
            uint32_t MeshIndex = 0;
            uint32_t PrimitiveIndex = 0;
            int32_t MaterialIndex = -1;
            bool Skinned = false;
        };
        std::vector<Primitive> Primitives;
    };

    struct NativeSkin
    {
        int32_t SkeletonNode = -1;
        std::vector<int32_t> JointNodes; // glTF node indices in final joint order
        std::vector<std::string> AnimationKeys; // "animation:<i>" clips for this skeleton
    };

    struct GltfSceneManifest
    {
        uint32_t Version = NativeAssetFormatVersion;
        std::string SourcePath;
        std::vector<NativeSubasset> Subassets;
        std::vector<NativeSceneNode> Nodes;
        std::vector<NativeSkin> Skins;
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
