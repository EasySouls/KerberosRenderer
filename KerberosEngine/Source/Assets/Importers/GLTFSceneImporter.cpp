#include "Assets/Importers/GLTFSceneImporter.hpp"
#include "Assets/Formats/AnimationFormat.hpp"
#include "Assets/Formats/NativeAssetSerializer.hpp"
#include "Assets/Formats/SkeletonFormat.hpp"
#include "Assets/Asset.hpp"
#include "Assets/Pipeline/ImportPipeline.hpp"
#include "Renderer/Animation/SkeletalMesh.hpp"
#include "Renderer/Vertex.hpp"
#include "Renderer/Textures/KTX2Encoder.hpp"
#include "Renderer/Textures/KTX2FormatSelector.hpp"
#include "Utils/KtxConversion.hpp"
#include "Profiling/Profilers.hpp"

#include <tinygltf/tiny_gltf.h>
#include <ktx.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <map>
#include <ranges>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <limits>

import Kerberos;

namespace Kerberos {

namespace {

uint64_t StableId(const std::string_view source, const std::string_view kind, const size_t index)
{
    uint64_t h = 1469598103934665603ull;
    constexpr uint64_t mul = 1099511628211ull;
    for (const char c : source)
    {
        h ^= static_cast<uint8_t>(c);
        h *= mul;
    }

    h ^= '|'; 
    h *= mul;

    for (const char c : kind) 
    { 
        h ^= static_cast<uint8_t>(c); 
        h *= mul;
    }

    h ^= '|';
    h *= mul;

    for (const char c : std::to_string(index)) 
    { 
        h ^= static_cast<uint8_t>(c); 
        h *= mul; 
    }
    return h;
}

std::string Name(const std::string& n, const char* prefix, const size_t i)
{ 
    return n.empty() ? std::string(prefix) + std::to_string(i) : n;
}

std::string Key(const char* kind, const size_t i, const size_t j = std::numeric_limits<size_t>::max())
{ 
    return j == std::numeric_limits<size_t>::max() 
        ? std::string(kind) + ":" + std::to_string(i) 
        : std::string(kind) + ":" + std::to_string(i) + ":" + std::to_string(j);
}

std::string SanitizeFileName(std::string value)
{
    for (char& c : value)
    {
        if (c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' || c == '|' || c == '?' || c == '*'
            || c == ';' || c == '=' || c == ',')
            c = '_';
    }

    while (!value.empty() && (value.front() == ' ' || value.front() == '.'))
        value.erase(value.begin());
    while (!value.empty() && (value.back() == ' ' || value.back() == '.'))
        value.pop_back();

    return value;
}

std::string MeshArtifactName(const size_t mesh, const size_t primitive, const std::string& meshName)
{
    std::string name = "mesh_" + std::to_string(mesh) + "_" + std::to_string(primitive);
    const std::string sanitized = SanitizeFileName(meshName);
    if (!sanitized.empty())
        name += "_" + sanitized;
    return name;
}

std::string SkinnedMeshArtifactName(const size_t mesh, const size_t primitive, const std::string& meshName)
{
    std::string name = "skinnedmesh_" + std::to_string(mesh) + "_" + std::to_string(primitive);
    const std::string sanitized = SanitizeFileName(meshName);
    if (!sanitized.empty())
        name += "_" + sanitized;
    return name;
}

std::string TextureArtifactName(const size_t imageIndex, const std::string& imageName)
{
    std::string name = "texture_" + std::to_string(imageIndex);
    const std::string sanitized = SanitizeFileName(imageName);
    if (!sanitized.empty())
        name += "_" + sanitized;
    return name;
}

std::string UniqueArtifactName(std::unordered_set<std::string>& used, const std::string& base, const size_t index)
{
    std::string candidate = base;
    if (!used.insert(candidate).second)
    {
        candidate = base + "_" + std::to_string(index);
        used.insert(candidate);
    }
    return candidate;
}

size_t ComponentCount(const int type)
{
    switch (type)
    {
    case TINYGLTF_TYPE_SCALAR: return 1;
    case TINYGLTF_TYPE_VEC2: return 2;
    case TINYGLTF_TYPE_VEC3: return 3;
    case TINYGLTF_TYPE_VEC4: return 4;
    case TINYGLTF_TYPE_MAT4: return 16;
    default: return 0;
    }
}

size_t ComponentSize(const int componentType)
{
    switch (componentType)
    {
    case TINYGLTF_COMPONENT_TYPE_BYTE:
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: return 1;
    case TINYGLTF_COMPONENT_TYPE_SHORT:
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: return 2;
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
    case TINYGLTF_COMPONENT_TYPE_FLOAT: return 4;
    default: return 0;
    }
}

bool AccessorElement(const tinygltf::Model& model, const tinygltf::Accessor& accessor,
    const size_t index, float* destination, const size_t componentCount)
{
    if (accessor.bufferView < 0 || index >= accessor.count)
        return false;

    const size_t availableComponents = ComponentCount(accessor.type);
    const size_t componentSize = ComponentSize(accessor.componentType);
    if (availableComponents < componentCount || componentSize == 0)
        return false;

    const auto& view = model.bufferViews[accessor.bufferView];
    if (view.buffer < 0 || static_cast<size_t>(view.buffer) >= model.buffers.size())
        return false;
    const auto& buffer = model.buffers[view.buffer];

    const size_t elementSize = componentSize * availableComponents;
    const size_t stride = accessor.ByteStride(view) ? static_cast<size_t>(accessor.ByteStride(view)) : elementSize;
    const size_t offset = static_cast<size_t>(view.byteOffset) + static_cast<size_t>(accessor.byteOffset) + index * stride;
    if (offset > buffer.data.size() || elementSize > buffer.data.size() - offset)
        return false;

    const uint8_t* data = buffer.data.data() + offset;
    const double normalization = accessor.normalized
        ? (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE ? 255.0
            : accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT ? 65535.0
            : accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT ? 4294967295.0 : 1.0)
        : 1.0;

    for (size_t component = 0; component < componentCount; ++component)
    {
        const uint8_t* source = data + component * componentSize;
        switch (accessor.componentType)
        {
        case TINYGLTF_COMPONENT_TYPE_FLOAT:
        {
            float value = 0.0f;
            std::memcpy(&value, source, sizeof(value));
            destination[component] = value;
            break;
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
            destination[component] = static_cast<float>(*source) / static_cast<float>(normalization);
            break;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
        {
            uint16_t value = 0;
            std::memcpy(&value, source, sizeof(value));
            destination[component] = static_cast<float>(value) / static_cast<float>(normalization);
            break;
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
        {
            uint32_t value = 0;
            std::memcpy(&value, source, sizeof(value));
            destination[component] = static_cast<float>(value) / static_cast<float>(normalization);
            break;
        }
        default:
            return false;
        }
    }
    return true;
}

bool AccessorUnsignedElement(const tinygltf::Model& model, const tinygltf::Accessor& accessor,
    const size_t index, uint32_t* destination, const size_t componentCount)
{
    if (accessor.bufferView < 0 || index >= accessor.count)
        return false;

    const size_t availableComponents = ComponentCount(accessor.type);
    const size_t componentSize = ComponentSize(accessor.componentType);
    if (availableComponents < componentCount || componentSize == 0)
        return false;

    const auto& view = model.bufferViews[accessor.bufferView];
    if (view.buffer < 0 || static_cast<size_t>(view.buffer) >= model.buffers.size())
        return false;
    const auto& buffer = model.buffers[view.buffer];

    const size_t elementSize = componentSize * availableComponents;
    const size_t stride = accessor.ByteStride(view) ? static_cast<size_t>(accessor.ByteStride(view)) : elementSize;
    const size_t offset = static_cast<size_t>(view.byteOffset) + static_cast<size_t>(accessor.byteOffset) + index * stride;
    if (offset > buffer.data.size() || elementSize > buffer.data.size() - offset)
        return false;

    const uint8_t* data = buffer.data.data() + offset;
    for (size_t component = 0; component < componentCount; ++component)
    {
        const uint8_t* source = data + component * componentSize;
        switch (accessor.componentType)
        {
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
            destination[component] = *source;
            break;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
        {
            uint16_t value = 0;
            std::memcpy(&value, source, sizeof(value));
            destination[component] = value;
            break;
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
        {
            uint32_t value = 0;
            std::memcpy(&value, source, sizeof(value));
            destination[component] = value;
            break;
        }
        default:
            return false;
        }
    }
    return true;
}

bool ReadIndices(const tinygltf::Model& model, const tinygltf::Accessor& accessor, std::vector<uint32_t>& out)
{
    out.clear();
    out.resize(accessor.count);
    for (size_t i = 0; i < accessor.count; ++i)
    {
        uint32_t value = 0;
        if (!AccessorUnsignedElement(model, accessor, i, &value, 1))
            return false;
        out[i] = value;
    }
    return true;
}

glm::mat4 ReadNodeMatrix(const tinygltf::Node& node)
{
    glm::mat4 matrix(1.0f);
    if (node.matrix.size() != 16)
        return matrix;

    // glTF stores matrices column-major.
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            matrix[column][row] = static_cast<float>(node.matrix[static_cast<size_t>(column) * 4 + static_cast<size_t>(row)]);
    return matrix;
}

void ReadNodeTransform(const tinygltf::Node& node, glm::vec3& translation, glm::quat& rotation, glm::vec3& scale)
{
    translation = glm::vec3(0.0f);
    rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    scale = glm::vec3(1.0f);

    if (node.matrix.size() == 16)
    {
        glm::vec3 skew;
        glm::vec4 perspective;
        glm::decompose(ReadNodeMatrix(node), scale, rotation, translation, skew, perspective);
        rotation = glm::normalize(rotation);
        return;
    }

    if (node.translation.size() == 3)
        translation = glm::vec3(node.translation[0], node.translation[1], node.translation[2]);
    if (node.rotation.size() == 4)
        rotation = glm::normalize(glm::quat(
            static_cast<float>(node.rotation[3]),
            static_cast<float>(node.rotation[0]),
            static_cast<float>(node.rotation[1]),
            static_cast<float>(node.rotation[2])));
    if (node.scale.size() == 3)
        scale = glm::vec3(node.scale[0], node.scale[1], node.scale[2]);
}

std::vector<int32_t> BuildNodeParents(const tinygltf::Model& model)
{
    std::vector<int32_t> parents(model.nodes.size(), -1);
    for (size_t parent = 0; parent < model.nodes.size(); ++parent)
    {
        for (const int child : model.nodes[parent].children)
        {
            if (child >= 0 && static_cast<size_t>(child) < parents.size())
                parents[static_cast<size_t>(child)] = static_cast<int32_t>(parent);
        }
    }
    return parents;
}

bool HasSkinAttribute(const tinygltf::Primitive& primitive)
{
    return primitive.attributes.find("JOINTS_0") != primitive.attributes.end()
        && primitive.attributes.find("WEIGHTS_0") != primitive.attributes.end();
}

struct SkeletonImportInfo
{
    bool Valid = false;
    size_t SkinIndex = 0;
    std::string LocalKey;
    int32_t SkeletonNode = -1;
    uint32_t JointCount = 0;
    std::vector<int32_t> JointNodes;                         // final order
    std::unordered_map<uint32_t, uint32_t> OldToNewJoint;    // skin.joints index -> final index
    std::unordered_map<int32_t, uint32_t> NodeToJoint;       // glTF node -> final index
    SkeletonFile File;
};

SkeletonImportInfo BuildSkeleton(const tinygltf::Model& model, const std::vector<int32_t>& nodeParents,
    const size_t skinIndex)
{
    SkeletonImportInfo info;
    info.SkinIndex = skinIndex;
    info.LocalKey = Key("skeleton", skinIndex);

    const auto& skin = model.skins[skinIndex];
    const size_t jointCount = skin.joints.size();
    if (jointCount == 0)
        return info;

    std::unordered_map<int32_t, size_t> nodeToOldIndex;
    nodeToOldIndex.reserve(jointCount);
    for (size_t i = 0; i < jointCount; ++i)
        nodeToOldIndex.emplace(skin.joints[i], i);

    std::vector<int32_t> oldParents(jointCount, -1);
    std::vector<std::vector<size_t>> children(jointCount);
    for (size_t i = 0; i < jointCount; ++i)
    {
        int32_t parentNode = skin.joints[i] >= 0 && static_cast<size_t>(skin.joints[i]) < nodeParents.size()
            ? nodeParents[static_cast<size_t>(skin.joints[i])]
            : -1;
        while (parentNode >= 0 && !nodeToOldIndex.contains(parentNode))
        {
            parentNode = static_cast<size_t>(parentNode) < nodeParents.size()
                ? nodeParents[static_cast<size_t>(parentNode)]
                : -1;
        }
        if (parentNode >= 0)
        {
            const size_t parentIndex = nodeToOldIndex[parentNode];
            oldParents[i] = static_cast<int32_t>(parentIndex);
            children[parentIndex].push_back(i);
        }
    }

    // Stable Kahn topological sort so Skeleton's parent-before-child invariant holds.
    std::vector<size_t> indegree(jointCount, 0);
    for (size_t i = 0; i < jointCount; ++i)
        if (oldParents[i] >= 0)
            indegree[i] = 1;

    std::vector<size_t> queue;
    queue.reserve(jointCount);
    for (size_t i = 0; i < jointCount; ++i)
        if (indegree[i] == 0)
            queue.push_back(i);

    std::vector<size_t> order;
    order.reserve(jointCount);
    for (size_t cursor = 0; cursor < queue.size(); ++cursor)
    {
        const size_t current = queue[cursor];
        order.push_back(current);
        for (const size_t child : children[current])
        {
            if (--indegree[child] == 0)
                queue.push_back(child);
        }
    }
    if (order.size() != jointCount)
    {
        Log::CoreWarn("glTF skin {} has cyclic joint hierarchy; appending remaining joints in source order",
            skinIndex);
        for (size_t i = 0; i < jointCount; ++i)
            if (std::ranges::find(order, i) == order.end())
                order.push_back(i);
    }

    std::vector<size_t> oldToNew(jointCount, 0);
    std::vector<size_t> newToOld(jointCount, 0);
    for (size_t newIndex = 0; newIndex < jointCount; ++newIndex)
    {
        const size_t oldIndex = order[newIndex];
        oldToNew[oldIndex] = newIndex;
        newToOld[newIndex] = oldIndex;
        info.OldToNewJoint[static_cast<uint32_t>(oldIndex)] = static_cast<uint32_t>(newIndex);
    }

    std::vector<glm::mat4> inverseBinds(jointCount, glm::mat4(1.0f));
    if (skin.inverseBindMatrices >= 0)
    {
        const auto& accessor = model.accessors[skin.inverseBindMatrices];
        if (accessor.type == TINYGLTF_TYPE_MAT4 && static_cast<size_t>(accessor.count) >= jointCount)
        {
            for (size_t oldIndex = 0; oldIndex < jointCount; ++oldIndex)
            {
                std::array<float, 16> values{};
                if (!AccessorElement(model, accessor, oldIndex, values.data(), 16))
                {
                    Log::CoreWarn("glTF skin {} could not read inverse bind matrix {}", skinIndex, oldIndex);
                    continue;
                }
                glm::mat4 matrix(1.0f);
                for (int column = 0; column < 4; ++column)
                    for (int row = 0; row < 4; ++row)
                        matrix[column][row] = values[static_cast<size_t>(column) * 4 + static_cast<size_t>(row)];
                inverseBinds[oldIndex] = matrix;
            }
        }
        else
        {
            Log::CoreWarn("glTF skin {} has an invalid inverse bind accessor", skinIndex);
        }
    }

    info.JointCount = static_cast<uint32_t>(jointCount);
    info.JointNodes.reserve(jointCount);
    info.NodeToJoint.reserve(jointCount);
    info.SkeletonNode = skin.skeleton;

    info.File.Joints.reserve(jointCount);
    for (size_t newIndex = 0; newIndex < jointCount; ++newIndex)
    {
        const size_t oldIndex = newToOld[newIndex];
        const int32_t jointNode = skin.joints[oldIndex];
        info.JointNodes.push_back(jointNode);
        info.NodeToJoint[jointNode] = static_cast<uint32_t>(newIndex);

        SkeletonJointFile joint;
        const auto& node = model.nodes[static_cast<size_t>(jointNode)];
        joint.Name = Name(node.name, "Joint_", newIndex);
        joint.SourceNode = jointNode;
        ReadNodeTransform(node, joint.RestTranslation, joint.RestRotation, joint.RestScale);
        joint.InverseBind = inverseBinds[oldIndex];

        const int32_t oldParent = oldParents[oldIndex];
        if (oldParent >= 0)
        {
            const auto parentNew = std::ranges::find(order, static_cast<size_t>(oldParent));
            joint.ParentJoint = parentNew != order.end()
                ? static_cast<int32_t>(std::distance(order.begin(), parentNew))
                : -1;
        }
        info.File.Joints.push_back(std::move(joint));
    }

    info.Valid = true;
    return info;
}

bool WriteRecord(const std::filesystem::path& root, const std::string& folder, const std::string& key,
                 const std::string& kind, const uint32_t index, const std::string& data,
                 const std::string& filename = {})
{
    KBR_TRACY_FUNCTION();

    std::error_code ec;
    const auto outputName = filename.empty()
        ? key.substr(key.find(':') + 1) + ".kbr" + kind
        : filename + ".kbr" + kind;
    const auto path = root / folder / outputName;
    std::filesystem::create_directories(path.parent_path(), ec);
    return !ec && NativeAssetSerializer::SerializeRecord({ .Version = NativeAssetFormatVersion, .Kind = kind, .SourceIndex = index, .LocalKey = key, .Data = data }, path);
}

bool WriteMesh(const std::filesystem::path& root, const size_t mesh, const size_t primitive,
    const tinygltf::Model& model, const tinygltf::Primitive& p)
{
    KBR_TRACY_FUNCTION();

    NativeMeshPayload payload;
    const auto it = p.attributes.find("POSITION");
    if (it == p.attributes.end())
        return false;

    const auto& positionAccessor = model.accessors[it->second];
    if (positionAccessor.bufferView < 0 || positionAccessor.type != TINYGLTF_TYPE_VEC3)
        return false;

    std::vector<Vertex> vertices(positionAccessor.count);
    for (size_t i = 0; i < vertices.size(); ++i)
    {
        if (!AccessorElement(model, positionAccessor, i, &vertices[i].Position.x, 3))
            return false;
        vertices[i].Normal = glm::vec3(0.0f);
        vertices[i].Tangent = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        vertices[i].TexCoord = glm::vec2(0.0f);

        const auto normal = p.attributes.find("NORMAL");
        if (normal != p.attributes.end()
            && !AccessorElement(model, model.accessors[normal->second], i, &vertices[i].Normal.x, 3))
            return false;
        const auto texCoord = p.attributes.find("TEXCOORD_0");
        if (texCoord != p.attributes.end()
            && !AccessorElement(model, model.accessors[texCoord->second], i, &vertices[i].TexCoord.x, 2))
            return false;
        const auto tangent = p.attributes.find("TANGENT");
        if (tangent != p.attributes.end()
            && !AccessorElement(model, model.accessors[tangent->second], i, &vertices[i].Tangent.x, 4))
            return false;
    }

    payload.VertexStride = sizeof(Vertex);
    payload.VertexData.resize(vertices.size() * sizeof(Vertex));
    if (!vertices.empty())
        std::memcpy(payload.VertexData.data(), vertices.data(), payload.VertexData.size());

    if (p.indices >= 0 && !ReadIndices(model, model.accessors[p.indices], payload.Indices))
        return false;

    std::error_code ec;
    const auto path = root / "Meshes" / (MeshArtifactName(mesh, primitive, model.meshes[mesh].name) + ".kbrmesh");
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec)
    {
        Log::CoreError("Failed to create directories for mesh: {}", ec.message());
        return false;
    }

    return NativeAssetSerializer::SerializeMesh(payload, path);
}

bool WriteSkinnedMesh(const std::filesystem::path& root, const size_t mesh, const size_t primitive,
    const tinygltf::Model& model, const tinygltf::Primitive& p, const SkeletonImportInfo& skeleton)
{
    KBR_TRACY_FUNCTION();

    const auto positionIt = p.attributes.find("POSITION");
    const auto jointsIt = p.attributes.find("JOINTS_0");
    const auto weightsIt = p.attributes.find("WEIGHTS_0");
    if (positionIt == p.attributes.end() || jointsIt == p.attributes.end() || weightsIt == p.attributes.end())
        return false;

    const auto& positionAccessor = model.accessors[positionIt->second];
    if (positionAccessor.bufferView < 0 || positionAccessor.type != TINYGLTF_TYPE_VEC3)
        return false;

    const auto& jointsAccessor = model.accessors[jointsIt->second];
    const auto& weightsAccessor = model.accessors[weightsIt->second];
    if (jointsAccessor.count < positionAccessor.count || weightsAccessor.count < positionAccessor.count)
        return false;

    std::vector<SkinVertex> vertices(positionAccessor.count);
    for (size_t i = 0; i < vertices.size(); ++i)
    {
        SkinVertex& vertex = vertices[i];
        vertex.JointIndices = glm::uvec4(0);
        vertex.JointWeights = glm::vec4(0.0f);
        vertex.Tangent = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        vertex.TexCoord = glm::vec2(0.0f);
        vertex.Normal = glm::vec3(0.0f);

        if (!AccessorElement(model, positionAccessor, i, &vertex.Position.x, 3))
            return false;

        const auto normal = p.attributes.find("NORMAL");
        if (normal != p.attributes.end()
            && !AccessorElement(model, model.accessors[normal->second], i, &vertex.Normal.x, 3))
            return false;
        const auto texCoord = p.attributes.find("TEXCOORD_0");
        if (texCoord != p.attributes.end()
            && !AccessorElement(model, model.accessors[texCoord->second], i, &vertex.TexCoord.x, 2))
            return false;
        const auto tangent = p.attributes.find("TANGENT");
        if (tangent != p.attributes.end()
            && !AccessorElement(model, model.accessors[tangent->second], i, &vertex.Tangent.x, 4))
            return false;

        uint32_t joints[4]{};
        float weights[4]{};
        if (!AccessorUnsignedElement(model, jointsAccessor, i, joints, 4)
            || !AccessorElement(model, weightsAccessor, i, weights, 4))
            return false;

        for (int component = 0; component < 4; ++component)
        {
            const size_t componentIndex = static_cast<size_t>(component);
            const auto remapped = skeleton.OldToNewJoint.find(joints[componentIndex]);
            if (remapped != skeleton.OldToNewJoint.end())
            {
                vertex.JointIndices[component] = remapped->second;
                vertex.JointWeights[component] = weights[componentIndex];
            }
            else
            {
                vertex.JointIndices[component] = 0;
                vertex.JointWeights[component] = 0.0f;
            }
        }

        const float weightSum = vertex.JointWeights.x + vertex.JointWeights.y
            + vertex.JointWeights.z + vertex.JointWeights.w;
        if (weightSum > 1e-6f)
            vertex.JointWeights /= weightSum;
        else
            vertex.JointWeights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
    }

    NativeMeshPayload payload;
    payload.VertexStride = sizeof(SkinVertex);
    payload.VertexData.resize(vertices.size() * sizeof(SkinVertex));
    if (!vertices.empty())
        std::memcpy(payload.VertexData.data(), vertices.data(), payload.VertexData.size());

    if (p.indices >= 0 && !ReadIndices(model, model.accessors[p.indices], payload.Indices))
        return false;

    std::error_code ec;
    const auto path = root / "Meshes"
        / (SkinnedMeshArtifactName(mesh, primitive, model.meshes[mesh].name) + ".kbrskinnedmesh");
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec)
    {
        Log::CoreError("Failed to create directories for skinned mesh: {}", ec.message());
        return false;
    }

    return NativeAssetSerializer::SerializeSkeletalMesh(payload, path);
}

bool IsKtxData(const unsigned char* bytes, const int size)
{
    static constexpr unsigned char ktx1Magic[] = {
        0xAB, 0x4B, 0x54, 0x58, 0x20, 0x31, 0x31, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A
    };
    static constexpr unsigned char ktx2Magic[] = {
        0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A
    };

    return bytes && size >= static_cast<int>(sizeof(ktx1Magic))
           && (std::memcmp(bytes, ktx1Magic, sizeof(ktx1Magic)) == 0
               || std::memcmp(bytes, ktx2Magic, sizeof(ktx2Magic)) == 0);
}

bool LoadImageDataWithKtxFallback(
        tinygltf::Image* image,
        const int imageIdx,
        std::string* err,
        std::string* warn,
        const int reqWidth,
        const int reqHeight,
        const unsigned char* bytes,
        const int size,
        const void* userData)
    {
        (void)userData;

        if (IsKtxData(bytes, size))
        {
            ktxTexture* ktxTexture = nullptr;
            const ktxResult result = ktxTexture_CreateFromMemory(bytes, static_cast<ktx_size_t>(size), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &ktxTexture);
            if (result != KTX_SUCCESS || !ktxTexture)
            {
                if (err)
                    (*err) += "Failed to load KTX image data for image[" + std::to_string(imageIdx) + "].\n";
                return false;
            }

            if ((reqWidth > 0 && static_cast<int>(ktxTexture->baseWidth) != reqWidth)
                || (reqHeight > 0 && static_cast<int>(ktxTexture->baseHeight) != reqHeight))
            {
                if (warn)
                {
                    (*warn) += "KTX image dimensions do not match the glTF image request for image[" + std::to_string(imageIdx) + "].\n";
                }
            }

            image->width = static_cast<int>(ktxTexture->baseWidth);
            image->height = static_cast<int>(ktxTexture->baseHeight);
            image->as_is = true;
            image->image.resize(static_cast<size_t>(size));
            std::memcpy(image->image.data(), bytes, static_cast<size_t>(size));

            ktxTexture_Destroy(ktxTexture);
            return true;
        }

        return tinygltf::LoadImageData(image, imageIdx, err, warn, reqWidth, reqHeight, bytes, size, nullptr);
    }

    bool IsKtx2Data(const unsigned char* bytes, const int size)
    {
        static constexpr unsigned char ktx2Magic[] = {
            0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A
        };
        return bytes && size >= static_cast<int>(sizeof(ktx2Magic))
            && std::memcmp(bytes, ktx2Magic, sizeof(ktx2Magic)) == 0;
    }

    std::filesystem::path ResolveTexturePath(const std::filesystem::path& sourcePath, const std::string& uri)
    {
        if (uri.empty())
            return {};

        std::filesystem::path texturePath(uri);
        if (texturePath.is_relative())
            texturePath = sourcePath.parent_path() / texturePath;

        return texturePath.lexically_normal();
    }

    int ImageIndexForTexture(const tinygltf::Model& model, const int textureIndex)
    {
        if (textureIndex < 0 || textureIndex >= static_cast<int>(model.textures.size()))
            return -1;

        const int source = model.textures[textureIndex].source;
        return (source >= 0 && source < static_cast<int>(model.images.size())) ? source : -1;
    }

    bool WriteTextureKtx2(const tinygltf::Image& image, const std::filesystem::path& sourcePath,
                          const std::filesystem::path& destPath, const bool srgb)
    {
        std::error_code ec;
        std::filesystem::create_directories(destPath.parent_path(), ec);

        // Already KTX2 bytes: write directly.
        if (image.as_is && IsKtx2Data(image.image.data(), static_cast<int>(image.image.size())))
        {
            std::ofstream out(destPath, std::ios::binary);
            if (!out)
                return false;
            out.write(reinterpret_cast<const char*>(image.image.data()), static_cast<std::streamsize>(image.image.size()));
            return out.good();
        }

        // KTX1 bytes: convert from the external source file when available.
        if (image.as_is)
        {
            if (image.uri.empty())
            {
                Log::CoreWarn("Embedded KTX1 image '{}' cannot be converted to KTX2; skipping texture.", image.name);
                return false;
            }

            const std::filesystem::path source = ResolveTexturePath(sourcePath, image.uri);
            std::filesystem::path ktx2 = source;
            ktx2.replace_extension(".ktx2");

            if (std::filesystem::exists(ktx2, ec))
                return std::filesystem::copy_file(ktx2, destPath, std::filesystem::copy_options::overwrite_existing, ec);

            const Process::ProcessResult result = KtxConversion::ConvertKtxToKtx2(source, true);
            if (!result.Succeeded)
            {
                Log::CoreError("Failed to convert KTX to KTX2 for '{}': {}", source.string(), result.ErrorMessage);
                return false;
            }
            return std::filesystem::copy_file(ktx2, destPath, std::filesystem::copy_options::overwrite_existing, ec);
        }

        // Decoded pixels: encode to KTX2.
        if (image.image.empty() || image.width <= 0 || image.height <= 0)
            return false;

        TextureSpecification spec;
        spec.Width = static_cast<uint32_t>(image.width);
        spec.Height = static_cast<uint32_t>(image.height);
        ImageFormat format = image.component == 4 ? ImageFormat::RGBA8
            : image.component == 3 ? ImageFormat::RGB8
            : image.component == 2 ? ImageFormat::RG8
            : image.component == 1 ? ImageFormat::R8 : ImageFormat::RGBA8;
        // sRGB color data only applies to 3/4 channel images, single and dual
        // channel images have no sRGB formats and remain linear.
        spec.Format = srgb ? KTX2FormatSelector::ToSrgb(format) : format;

        // glTF stores image rows top-down, but the renderer samples textures with
        // a vertically flipped row order (see TextureImporter::LoadTextureData,
        // which flips standalone images and cubemap faces). Flip the decoded rows
        // so glTF textures match every other image import path.
        const size_t component = static_cast<size_t>(image.component);
        const size_t rowBytes = static_cast<size_t>(image.width) * component;
        const Buffer pixels(static_cast<uint64_t>(image.image.size()));
        for (int y = 0; y < image.height; ++y)
        {
            const size_t sourceRow = static_cast<size_t>(image.height - 1 - y) * rowBytes;
            const size_t destinationRow = static_cast<size_t>(y) * rowBytes;
            std::memcpy(pixels.Data + destinationRow, image.image.data() + sourceRow, rowBytes);
        }
        return KTX2Encoder::Encode(destPath, spec, pixels);
    }

    AnimationClipInterpolation ToInterpolation(const std::string& interpolation)
    {
        if (interpolation == "STEP")
            return AnimationClipInterpolation::Step;
        if (interpolation == "CUBICSPLINE")
            return AnimationClipInterpolation::CubicSpline;
        return AnimationClipInterpolation::Linear;
    }

    bool ReadAnimationTrack(const tinygltf::Model& model, const tinygltf::Animation& animation,
        const tinygltf::AnimationChannel& channel, const SkeletonImportInfo& skeleton,
        AnimationTrackFile& track)
    {
        if (channel.target_node < 0 || channel.sampler < 0
            || static_cast<size_t>(channel.sampler) >= animation.samplers.size())
            return false;

        const auto jointIt = skeleton.NodeToJoint.find(channel.target_node);
        if (jointIt == skeleton.NodeToJoint.end())
            return false;

        AnimTrackPath path;
        if (channel.target_path == "translation")
            path = AnimTrackPath::Translation;
        else if (channel.target_path == "rotation")
            path = AnimTrackPath::Rotation;
        else if (channel.target_path == "scale")
            path = AnimTrackPath::Scale;
        else
            return false; // TODO: Morph target weights and unknown paths are unsupported.

        const auto& sampler = animation.samplers[static_cast<size_t>(channel.sampler)];
        if (sampler.input < 0 || sampler.output < 0)
            return false;

        const auto& inputAccessor = model.accessors[sampler.input];
        if (inputAccessor.type != TINYGLTF_TYPE_SCALAR || inputAccessor.count == 0)
            return false;

        track.TargetNode = static_cast<uint32_t>(channel.target_node);
        track.JointIndex = static_cast<int32_t>(jointIt->second);
        track.Path = path;
        track.Interpolation = ToInterpolation(sampler.interpolation);
        track.Times.resize(inputAccessor.count);
        for (size_t i = 0; i < inputAccessor.count; ++i)
        {
            if (!AccessorElement(model, inputAccessor, i, &track.Times[i], 1))
                return false;
        }

        const auto& outputAccessor = model.accessors[sampler.output];
        const size_t componentCount = path == AnimTrackPath::Rotation ? 4 : 3;
        const bool cubic = track.Interpolation == AnimationClipInterpolation::CubicSpline;
        const size_t keyCount = inputAccessor.count;
        if (outputAccessor.count < keyCount * (cubic ? 3 : 1))
            return false;

        const auto readValue = [&](const size_t valueIndex, glm::vec4& value) -> bool
        {
            std::array<float, 4> components{ 0.0f, 0.0f, 0.0f, 0.0f };
            if (!AccessorElement(model, outputAccessor, valueIndex, components.data(), componentCount))
                return false;
            value = glm::vec4(components[0], components[1], components[2], components[3]);
            return true;
        };

        track.Values.resize(keyCount);
        if (cubic)
        {
            track.InTangents.resize(keyCount);
            track.OutTangents.resize(keyCount);
        }

        for (size_t key = 0; key < keyCount; ++key)
        {
            if (cubic)
            {
                if (!readValue(key * 3 + 0, track.InTangents[key])
                    || !readValue(key * 3 + 1, track.Values[key])
                    || !readValue(key * 3 + 2, track.OutTangents[key]))
                    return false;
            }
            else if (!readValue(key, track.Values[key]))
            {
                return false;
            }
        }

        return true;
    }

    bool WriteAnimation(const std::filesystem::path& root, const size_t animationIndex,
        const tinygltf::Model& model, const SkeletonImportInfo& skeleton, AnimationClipFile& clip,
        const std::string& filename)
    {
        const auto& animation = model.animations[animationIndex];
        clip.Version = 1;
        clip.LocalKey = Key("animation", animationIndex);
        clip.SourceIndex = static_cast<uint32_t>(animationIndex);
        clip.Name = Name(animation.name, "Animation_", animationIndex);
        clip.SkeletonKey = skeleton.Valid ? skeleton.LocalKey : std::string();
        clip.JointCount = skeleton.JointCount;
        clip.DurationSeconds = 0.0f;

        for (const auto& channel : animation.channels)
        {
            AnimationTrackFile track;
            if (!ReadAnimationTrack(model, animation, channel, skeleton, track))
                continue;
            if (!track.Times.empty())
                clip.DurationSeconds = std::max(clip.DurationSeconds, track.Times.back());
            clip.Tracks.push_back(std::move(track));
        }

        std::error_code ec;
        const auto path = root / "Animations" / (filename + ".kbranimation");
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec)
            return false;
        return NativeAssetSerializer::SerializeAnimationClip(clip, path);
    }
}

bool GLTFSceneImporter::Import(const std::filesystem::path& source, const std::filesystem::path& outputDirectory,
                               const std::filesystem::path& namespacePath, GltfSceneManifest* output,
                               const std::map<std::string, bool>* textureSrgbOverrides,
                               std::map<std::string, bool>* textureSrgbOut)
{
    KBR_TRACY_FUNCTION();

    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string error, warning;

    loader.SetImageLoader(LoadImageDataWithKtxFallback, nullptr);

    const auto ext = source.extension().string();
    bool loaded = ext == ".glb" ? loader.LoadBinaryFromFile(&model, &error, &warning, source.string())
                                : ext == ".gltf" ? loader.LoadASCIIFromFile(&model, &error, &warning, source.string()) : false;

    if (!warning.empty()) 
        Log::CoreWarn("glTF scene import warning: {}", warning);

    if (!loaded)
    { 
        if (!error.empty()) 
            Log::CoreError("glTF scene import error: {}", error);

        return false;
    }

    if (model.skins.size() > 1)
        Log::CoreWarn("glTF '{}' has {} skins; only the first skeleton is supported", source.string(), model.skins.size());

    const std::vector<int32_t> nodeParents = BuildNodeParents(model);
    SkeletonImportInfo skeleton;
    if (!model.skins.empty())
        skeleton = BuildSkeleton(model, nodeParents, 0);

    // Determine which skin each primitive belongs to. The same mesh primitive
    // may be referenced by several nodes; the first reference wins.
    struct PrimitiveSkin
    {
        int32_t SkinIndex = -1;
        bool Conflicting = false;
    };
    std::map<std::pair<uint32_t, uint32_t>, PrimitiveSkin> primitiveSkins;
    for (const auto& node : model.nodes)
    {
        if (node.mesh < 0 || static_cast<size_t>(node.mesh) >= model.meshes.size())
            continue;
        const auto& mesh = model.meshes[static_cast<size_t>(node.mesh)];
        for (size_t primitiveIndex = 0; primitiveIndex < mesh.primitives.size(); ++primitiveIndex)
        {
            const auto key = std::make_pair(static_cast<uint32_t>(node.mesh), static_cast<uint32_t>(primitiveIndex));
            auto& entry = primitiveSkins[key];
            if (entry.SkinIndex < 0)
                entry.SkinIndex = node.skin;
            else if (node.skin >= 0 && entry.SkinIndex != node.skin)
                entry.Conflicting = true;
        }
    }
    for (const auto& [key, value] : primitiveSkins)
        if (value.Conflicting)
            Log::CoreWarn("glTF mesh {}:{} is referenced by multiple skins, using the first one", key.first, key.second);

    GltfSceneManifest manifest; 
    manifest.SourcePath = source.lexically_normal().generic_string();

    for (size_t i = 0; i < model.meshes.size(); ++i)
        for (size_t p = 0; p < model.meshes[i].primitives.size(); ++p)
            manifest.Subassets.push_back({ .StableId = StableId(manifest.SourcePath, "mesh", i * 65536ull + p), .Kind = "mesh",
                .Name = Name(model.meshes[i].name, "Mesh_", i) + "_Primitive_" + std::to_string(p), .SourceIndex = static_cast<uint32_t>(i) });
    for (size_t i = 0; i < model.materials.size(); ++i) 
        manifest.Subassets.push_back({ .StableId = StableId(manifest.SourcePath, "material", i), .Kind = "material", .Name = Name(model.materials[i].name, "Material_", i), .SourceIndex = static_cast<uint32_t>(i) });
    for (size_t i = 0; i < model.images.size(); ++i) 
        manifest.Subassets.push_back({ .StableId = StableId(manifest.SourcePath, "texture", i), .Kind = "texture", .Name = Name(model.images[i].name, "Texture_", i), .SourceIndex = static_cast<uint32_t>(i) });
    if (skeleton.Valid)
        manifest.Subassets.push_back({ .StableId = StableId(manifest.SourcePath, "skeleton", 0), .Kind = "skeleton",
            .Name = Name(model.skins[0].name, "Skeleton_", 0), .SourceIndex = 0 });
    for (size_t i = 0; i < model.animations.size(); ++i) 
        manifest.Subassets.push_back({ .StableId = StableId(manifest.SourcePath, "animation", i), .Kind = "animation", .Name = Name(model.animations[i].name, "Animation_", i), .SourceIndex = static_cast<uint32_t>(i) });
    
    manifest.Subassets.push_back({ 
        .StableId = StableId(manifest.SourcePath, "prefab", 0), 
        .Kind = "prefab", 
        .Name = source.stem().string(), 
        .SourceIndex = 0 });

    manifest.Nodes.resize(model.nodes.size());

    for (size_t i = 0; i < model.nodes.size(); ++i) {
        auto& n = manifest.Nodes[i]; n.StableId = StableId(manifest.SourcePath, "node", i);
        n.Name = Name(model.nodes[i].name, "Node_", i); n.SkinIndex = model.nodes[i].skin;
        n.ParentIndex = nodeParents[i];
        ReadNodeTransform(model.nodes[i], n.Translation, n.Rotation, n.Scale);

        if (model.nodes[i].mesh >= 0 && static_cast<size_t>(model.nodes[i].mesh) < model.meshes.size())
        {
            const auto& mesh = model.meshes[model.nodes[i].mesh];
            n.Primitives.reserve(mesh.primitives.size());
            for (size_t primitiveIndex = 0; primitiveIndex < mesh.primitives.size(); ++primitiveIndex)
            {
                const auto skinIt = primitiveSkins.find({ static_cast<uint32_t>(model.nodes[i].mesh), static_cast<uint32_t>(primitiveIndex) });
                const int32_t primitiveSkin = skinIt != primitiveSkins.end() ? skinIt->second.SkinIndex : -1;
                const bool skinned = skeleton.Valid && primitiveSkin == 0
                    && HasSkinAttribute(mesh.primitives[primitiveIndex]);
                n.Primitives.push_back({
                    .MeshIndex = static_cast<uint32_t>(model.nodes[i].mesh),
                    .PrimitiveIndex = static_cast<uint32_t>(primitiveIndex),
                    .MaterialIndex = mesh.primitives[primitiveIndex].material,
                    .Skinned = skinned,
                });
            }
        }
    }

    if (skeleton.Valid)
    {
        NativeSkin nativeSkin;
        nativeSkin.SkeletonNode = skeleton.SkeletonNode;
        nativeSkin.JointNodes = skeleton.JointNodes;
        manifest.Skins.push_back(std::move(nativeSkin));
    }

    if (output) *output = manifest;
    if (outputDirectory.empty()) 
        return true;
    std::error_code ec;
    const std::filesystem::path artifactRoot = namespacePath.empty() ? outputDirectory : outputDirectory / namespacePath;
    std::filesystem::create_directories(artifactRoot, ec);
    if (ec)
        return false;

    // Emit one native record per primitive and source records for all other GLTF domains.
    for (size_t mi = 0; mi < model.meshes.size(); ++mi)
    {
        for (size_t pi = 0; pi < model.meshes[mi].primitives.size(); ++pi)
        {
            const auto& prim = model.meshes[mi].primitives[pi];
            const auto skinIt = primitiveSkins.find({ static_cast<uint32_t>(mi), static_cast<uint32_t>(pi) });
            const int32_t primitiveSkin = skinIt != primitiveSkins.end() ? skinIt->second.SkinIndex : -1;
            if (skeleton.Valid && primitiveSkin == 0 && HasSkinAttribute(prim))
            {
                if (!WriteSkinnedMesh(artifactRoot, mi, pi, model, prim, skeleton))
                    return false;
            }
            else if (!WriteMesh(artifactRoot, mi, pi, model, prim))
            {
                return false;
            }
        }
    }

    // Materials: emit one native record per glTF material, with a readable name
    // and references (by image index) to the imported texture sub-assets.
    std::unordered_set<std::string> usedMaterialNames;
    usedMaterialNames.reserve(model.materials.size());
    for (size_t i = 0; i < model.materials.size(); ++i)
    {
        const auto& m = model.materials[i];
        const auto& pbr = m.pbrMetallicRoughness;
        const std::string materialName = Name(m.name, "Material_", i);
        const std::string sanitized = SanitizeFileName(materialName);
        const std::string fileName = UniqueArtifactName(usedMaterialNames, sanitized, i);

        std::ostringstream d;
        d << "name=" << sanitized
            << ";baseColor=" << pbr.baseColorFactor[0] << "," << pbr.baseColorFactor[1] << "," << pbr.baseColorFactor[2] << "," << pbr.baseColorFactor[3]
            << ";metallic=" << pbr.metallicFactor
            << ";roughness=" << pbr.roughnessFactor;
        if (m.emissiveFactor.size() == 3)
            d << ";emissive=" << m.emissiveFactor[0] << "," << m.emissiveFactor[1] << "," << m.emissiveFactor[2];
        d << ";baseColorTexture=" << ImageIndexForTexture(model, pbr.baseColorTexture.index)
            << ";normalTexture=" << ImageIndexForTexture(model, m.normalTexture.index)
            << ";metallicRoughnessTexture=" << ImageIndexForTexture(model, pbr.metallicRoughnessTexture.index)
            << ";occlusionTexture=" << ImageIndexForTexture(model, m.occlusionTexture.index)
            << ";emissiveTexture=" << ImageIndexForTexture(model, m.emissiveTexture.index);

        if (!WriteRecord(artifactRoot, "Materials", Key("material", i), "material", static_cast<uint32_t>(i), d.str(), fileName))
            return false;
    }

    // Textures: import each referenced image as a .ktx2 artifact in the cache.
    // Color-space choice follows the glTF slot semantics of the referencing
    // material: base color and emissive are sRGB color data; normal, metallic-
    // roughness, and occlusion are linear data. Images referenced by no
    // material slot default to sRGB.
    enum class ImageUsage : uint8_t { Color = 1u << 0, Data = 1u << 1 };
    std::vector<uint8_t> imageUsage(model.images.size(), 0);
    const auto markUsage = [&](const int textureIndex, const ImageUsage usage)
    {
        const int imageIndex = ImageIndexForTexture(model, textureIndex);
        if (imageIndex >= 0)
            imageUsage[imageIndex] |= static_cast<uint8_t>(usage);
    };
    for (const auto& material : model.materials)
    {
        const auto& pbr = material.pbrMetallicRoughness;
        markUsage(pbr.baseColorTexture.index, ImageUsage::Color);
        markUsage(pbr.metallicRoughnessTexture.index, ImageUsage::Data);
        markUsage(material.normalTexture.index, ImageUsage::Data);
        markUsage(material.occlusionTexture.index, ImageUsage::Data);
        markUsage(material.emissiveTexture.index, ImageUsage::Color);
    }

    for (size_t i = 0; i < model.images.size(); ++i)
    {
        const auto& im = model.images[i];
        std::string imageName = im.name;
        if (imageName.empty() && !im.uri.empty())
            imageName = std::filesystem::path(im.uri).stem().string();
        const std::filesystem::path destPath = artifactRoot / "Textures" / (TextureArtifactName(i, imageName) + ".ktx2");

        const bool usedAsColor = (imageUsage[i] & static_cast<uint8_t>(ImageUsage::Color)) != 0;
        const bool usedAsData = (imageUsage[i] & static_cast<uint8_t>(ImageUsage::Data)) != 0;
        if (usedAsColor && usedAsData)
        {
            Log::CoreWarn("glTF image '{}' (index {}) is referenced by both color and data texture slots; encoding it as sRGB, so data slots will receive sRGB-decoded samples.", im.name, i);
        }

        const std::string key = "texture:" + std::to_string(i);
        bool srgb = usedAsColor || !usedAsData;
        if (textureSrgbOverrides)
        {
            const auto overrideIt = textureSrgbOverrides->find(key);
            if (overrideIt != textureSrgbOverrides->end())
                srgb = overrideIt->second;
        }
        if (textureSrgbOut)
            (*textureSrgbOut)[key] = srgb;

        if (!WriteTextureKtx2(im, source, destPath, srgb))
        {
            Log::CoreWarn("Failed to import texture for glTF image '{}' (index {}); material references to it will be left unbound.", im.name, i);
        }
    }

    if (skeleton.Valid)
    {
        SkeletonFile skeletonFile = skeleton.File;
        skeletonFile.Version = 1;
        skeletonFile.LocalKey = skeleton.LocalKey;
        skeletonFile.SourceIndex = 0;
        skeletonFile.Name = Name(model.skins[0].name, "Skeleton_", 0);

        std::error_code skeletonEc;
        const auto path = artifactRoot / "Skeletons"
            / (SanitizeFileName(skeletonFile.Name) + ".kbrskeleton");
        std::filesystem::create_directories(path.parent_path(), skeletonEc);
        if (skeletonEc || !NativeAssetSerializer::SerializeSkeleton(skeletonFile, path))
            return false;
    }

    std::unordered_set<std::string> usedAnimationNames;
    usedAnimationNames.reserve(model.animations.size());
    for (size_t i = 0; i < model.animations.size(); ++i)
    {
        const auto& a = model.animations[i];
        const std::string fileName = UniqueArtifactName(usedAnimationNames,
            SanitizeFileName(Name(a.name, "Animation_", i)), i);
        AnimationClipFile clip;
        if (!WriteAnimation(artifactRoot, i, model, skeleton, clip, fileName))
            return false;
        if (skeleton.Valid && !clip.Tracks.empty())
            manifest.Skins[0].AnimationKeys.push_back(clip.LocalKey);
    }

    std::ostringstream prefab; prefab << "name=" << source.stem().string() << ";nodes=" << model.nodes.size() << ";";
    for (size_t i = 0; i < manifest.Nodes.size(); ++i) {
        prefab << i << ":" << manifest.Nodes[i].Name << ":" << manifest.Nodes[i].ParentIndex << ":" << manifest.Nodes[i].SkinIndex << ";";
    }
    if (!WriteRecord(artifactRoot, "", Key("prefab", 0), "prefab", 0,
        prefab.str(), source.stem().string()))
        return false;

    if (output) *output = manifest;
    return NativeAssetSerializer::SerializeSceneManifest(manifest, artifactRoot / (source.stem().string() + ".kbrscene"));
}

}
