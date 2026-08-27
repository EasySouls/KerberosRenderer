#include "kbrpch.hpp"
#include "Assets/Importers/GltfSceneImporter.hpp"
#include "Assets/Formats/NativeAssetSerializer.hpp"
#include "Assets/Asset.hpp"
#include "Assets/Pipeline/ImportPipeline.hpp"

#include <tinygltf/tiny_gltf.h>
#include <algorithm>
#include <sstream>

namespace Kerberos {

namespace {

uint64_t StableId(const std::string_view source, const std::string_view kind, const size_t index)
{
    uint64_t h = 1469598103934665603ull;
    for (const char c : source)
    {
        h ^= static_cast<uint8_t>(c);
        h *= 1099511628211ull;
    }

    h ^= '|'; 
    h *= 1099511628211ull;

    for (const char c : kind) 
    { 
        h ^= static_cast<uint8_t>(c); 
        h *= 1099511628211ull;
    }

    h ^= '|';
    h *= 1099511628211ull;

    for (const char c : std::to_string(index)) 
    { 
        h ^= static_cast<uint8_t>(c); 
        h *= 1099511628211ull; 
    }
    return h;
}

std::string Name(const std::string& n, const char* prefix, const size_t i)
{ 
    return n.empty() ? std::string(prefix) + std::to_string(i) : n;
}

std::string Key(const char* kind, const size_t i, const size_t j = SIZE_MAX)
{ 
    return j == SIZE_MAX 
        ? std::string(kind) + ":" + std::to_string(i) 
        : std::string(kind) + ":" + std::to_string(i) + ":" + std::to_string(j);
}

AssetHandle Existing(const ImportContext* context, const std::string& key)
{
    if (!context) 
        return AssetHandle::Invalid();

    for (const auto& e : context->Meta.SubAssets)
    {
        if (e.LocalKey == key && e.Handle.IsValid()) 
            return e.Handle;
    }
    return AssetHandle::Invalid();
}

AssetHandle Handle(const ImportContext* context, const std::string& key)
{
    const auto h = Existing(context, key);
    return h.IsValid() ? h : AssetHandle(); 
}

bool WriteRecord(const std::filesystem::path& root, const std::string& folder, const std::string& key,
                 const std::string& kind, const uint32_t index, const std::string& data)
{
    std::error_code ec;
    const auto path = root / folder / (key.substr(key.find(':') + 1) + ".kbr" + kind);
    std::filesystem::create_directories(path.parent_path(), ec);
    return !ec && NativeAssetSerializer::SerializeRecord({ .Version = NativeAssetFormatVersion, .Kind = kind, .SourceIndex = index, .LocalKey = key, .Data = data }, path);
}

bool WriteMesh(const std::filesystem::path& root, const size_t mesh, const size_t primitive, const tinygltf::Model& model, const tinygltf::Primitive& p)
{
    NativeMeshPayload payload;
    const auto it = p.attributes.find("POSITION");
    if (it != p.attributes.end()) {
        const auto& a = model.accessors[it->second]; const auto& view = model.bufferViews[a.bufferView];
        const auto& buffer = model.buffers[view.buffer];
        const size_t stride = a.ByteStride(view) ? a.ByteStride(view) : sizeof(float) * 3;
        payload.VertexStride = sizeof(float) * 3; payload.VertexData.resize(a.count * payload.VertexStride);
        const auto* src = buffer.data.data() + view.byteOffset + a.byteOffset;

        for (size_t i = 0; i < a.count; ++i) 
        {
            std::memcpy(payload.VertexData.data() + i * payload.VertexStride, src + i * stride, sizeof(float) * 3);
        }
    }
    if (p.indices >= 0) {
        const auto& a = model.accessors[p.indices]; 
        const auto& view = model.bufferViews[a.bufferView]; 
        const auto& buffer = model.buffers[view.buffer];

        const auto* src = buffer.data.data() + view.byteOffset + a.byteOffset; 
        payload.Indices.reserve(a.count);

        for (size_t i = 0; i < a.count; ++i) {
            uint32_t value = 0;
            if (a.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) value = src[i];
            else if (a.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) { uint16_t v; std::memcpy(&v, src + i * 2, 2); value = v; }
            else { std::memcpy(&value, src + i * 4, 4); }
            payload.Indices.push_back(value);
        }
    }
    std::error_code ec;
    const auto path = root / "Meshes" / ("mesh_" + std::to_string(mesh) + "_" + std::to_string(primitive) + ".kbrmesh");
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        KBR_CORE_ERROR("Failed to create directories for mesh: {}", ec.message());
        return false;
    }

    const bool success = NativeAssetSerializer::SerializeMesh(payload, path);
    if (!success) {
        KBR_CORE_ERROR("Failed to serialize mesh to path: {}", path.string());
        return false;
    }

    return true;
}

}

bool GltfSceneImporter::Import(const std::filesystem::path& source, const std::filesystem::path& outputDirectory,
                               GltfSceneManifest* output)
{
    tinygltf::Model model; tinygltf::TinyGLTF loader; std::string error, warning;
    const auto ext = source.extension().string();
    bool loaded = ext == ".glb" ? loader.LoadBinaryFromFile(&model, &error, &warning, source.string())
                                : ext == ".gltf" ? loader.LoadASCIIFromFile(&model, &error, &warning, source.string()) : false;

    if (!warning.empty()) 
        KBR_CORE_WARN("glTF scene import warning: {}", warning);

    if (!loaded)
    { 
        if (!error.empty()) 
            KBR_CORE_ERROR("glTF scene import error: {}", error);

        return false;
    }

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
    for (size_t i = 0; i < model.skins.size(); ++i) 
        manifest.Subassets.push_back({ .StableId = StableId(manifest.SourcePath, "skeleton", i), .Kind = "skeleton", .Name = Name(model.skins[i].name, "Skeleton_", i), .SourceIndex = static_cast<uint32_t>(i) });
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
        n.Name = Name(model.nodes[i].name, "Node_", i); n.MeshIndex = model.nodes[i].mesh; n.SkinIndex = model.nodes[i].skin;
        for (size_t p = 0; p < model.nodes.size(); ++p)
        {
            if (std::ranges::find(model.nodes[p].children, static_cast<int>(i)) != model.nodes[p].children.end()) 
            { 
                n.ParentIndex = static_cast<int32_t>(p);
                break; 
            }
        }
    }
    if (output) *output = manifest;
    if (outputDirectory.empty()) 
        return true;
    std::error_code ec;
    std::filesystem::create_directories(outputDirectory, ec);
    if (ec)
        return false;

    // Emit one native record per primitive and source records for all other GLTF domains.
    for (size_t mi = 0; mi < model.meshes.size(); ++mi)
        for (size_t pi = 0; pi < model.meshes[mi].primitives.size(); ++pi) {
            const auto& prim = model.meshes[mi].primitives[pi];
            if (!WriteMesh(outputDirectory, mi, pi, model, prim)) 
                return false;
        }
    for (size_t i = 0; i < model.materials.size(); ++i) {
        const auto& m = model.materials[i]; std::ostringstream d; d << "name=" << m.name << ";baseColor=" << m.pbrMetallicRoughness.baseColorFactor[0] << "," << m.pbrMetallicRoughness.baseColorFactor[1] << "," << m.pbrMetallicRoughness.baseColorFactor[2] << "," << m.pbrMetallicRoughness.baseColorFactor[3] << ";metallic=" << m.pbrMetallicRoughness.metallicFactor << ";roughness=" << m.pbrMetallicRoughness.roughnessFactor << ";baseColorTexture=" << m.pbrMetallicRoughness.baseColorTexture.index;
        if (!WriteRecord(outputDirectory, "Materials", Key("material", i), "material", static_cast<uint32_t>(i), d.str())) 
            return false;
    }
    for (size_t i = 0; i < model.images.size(); ++i) {
        const auto& im = model.images[i]; std::ostringstream d; d << "name=" << im.name << ";uri=" << im.uri << ";mime=" << im.mimeType << ";width=" << im.width << ";height=" << im.height << ";component=" << im.component << ";bits=" << im.bits << ";bytes=" << im.image.size();
        if (!WriteRecord(outputDirectory, "Textures", Key("texture", i), "texture", static_cast<uint32_t>(i), d.str()))
            return false;
    }
    for (size_t i = 0; i < model.skins.size(); ++i) {
        const auto& s = model.skins[i]; std::ostringstream d; d << "name=" << s.name << ";inverseBindAccessor=" << s.inverseBindMatrices << ";skeleton=" << s.skeleton << ";joints="; for (int j : s.joints) d << j << ",";
        if (!WriteRecord(outputDirectory, "Skeletons", Key("skeleton", i), "skeleton", static_cast<uint32_t>(i), d.str()))
            return false;
    }
    for (size_t i = 0; i < model.animations.size(); ++i) {
        const auto& a = model.animations[i]; std::ostringstream d; d << "name=" << a.name << ";channels=" << a.channels.size() << ";samplers=" << a.samplers.size();
        if (!WriteRecord(outputDirectory, "Animations", Key("animation", i), "animation", static_cast<uint32_t>(i), d.str()))
            return false;
    }
    std::ostringstream prefab; prefab << "name=" << source.stem().string() << ";nodes=" << model.nodes.size() << ";";
    for (size_t i = 0; i < manifest.Nodes.size(); ++i) {
        prefab << i << ":" << manifest.Nodes[i].Name << ":" << manifest.Nodes[i].ParentIndex << ":" << manifest.Nodes[i].MeshIndex << ":" << manifest.Nodes[i].SkinIndex << ";";
    }
    if (!WriteRecord(outputDirectory, "Prefabs", Key("prefab", 0), "prefab", 0, prefab.str())) 
        return false;

    return NativeAssetSerializer::SerializeSceneManifest(manifest, outputDirectory / (source.stem().string() + ".kbrscene"));
}

}
