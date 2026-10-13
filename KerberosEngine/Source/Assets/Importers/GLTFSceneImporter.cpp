#include "Assets/Importers/GLTFSceneImporter.hpp"
#include "Assets/Formats/NativeAssetSerializer.hpp"
#include "Assets/Asset.hpp"
#include "Assets/Pipeline/ImportPipeline.hpp"
#include "Renderer/Vertex.hpp"
#include "Renderer/Textures/KTX2Encoder.hpp"
#include "Utils/KtxConversion.hpp"
#include "Profiling/Profilers.hpp"

#include <tinygltf/tiny_gltf.h>
#include <ktx.h>

#include <algorithm>
#include <fstream>
#include <sstream>
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

// Artifact filename stems (no extension). The leading index tokens are stable
// and mirror the sub-asset key, while the trailing name is a readability hint.
std::string MeshArtifactName(const size_t mesh, const size_t primitive, const std::string& meshName)
{
    std::string name = "mesh_" + std::to_string(mesh) + "_" + std::to_string(primitive);
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

bool WriteMesh(const std::filesystem::path& root, const size_t mesh, const size_t primitive, const tinygltf::Model& model, const tinygltf::Primitive& p)
{
    KBR_TRACY_FUNCTION();

    NativeMeshPayload payload;
    const auto it = p.attributes.find("POSITION");
    if (it == p.attributes.end())
        return false;

    const auto& positionAccessor = model.accessors[it->second];
    if (positionAccessor.bufferView < 0 || positionAccessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT
        || positionAccessor.type != TINYGLTF_TYPE_VEC3)
        return false;

    const auto readAttribute = [&model, &p](const char* name, const size_t vertex,
        const size_t componentCount, float* destination) -> bool
    {
        std::fill_n(destination, componentCount, 0.0f);
        const auto attribute = p.attributes.find(name);
        if (attribute == p.attributes.end())
            return true;

        const auto& accessor = model.accessors[attribute->second];
        if (accessor.bufferView < 0 || accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT
            || accessor.count <= vertex)
            return false;
        const auto& view = model.bufferViews[accessor.bufferView];
        if (view.buffer < 0 || static_cast<size_t>(view.buffer) >= model.buffers.size())
            return false;
        const auto& buffer = model.buffers[view.buffer];
        const size_t elementSize = componentCount * sizeof(float);
        const size_t stride = accessor.ByteStride(view) ? accessor.ByteStride(view) : elementSize;
        const size_t offset = view.byteOffset + accessor.byteOffset + vertex * stride;
        if (offset > buffer.data.size() || elementSize > buffer.data.size() - offset)
            return false;
        std::memcpy(destination, buffer.data.data() + offset, elementSize);
        return true;
    };

    std::vector<Vertex> vertices(positionAccessor.count);
    for (size_t i = 0; i < vertices.size(); ++i)
    {
        if (!readAttribute("POSITION", i, 3, &vertices[i].Position.x)
            || !readAttribute("NORMAL", i, 3, &vertices[i].Normal.x)
            || !readAttribute("TEXCOORD_0", i, 2, &vertices[i].TexCoord.x))
            return false;
        vertices[i].Tangent = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        if (!readAttribute("TANGENT", i, 4, &vertices[i].Tangent.x))
            return false;
    }
    payload.VertexStride = sizeof(Vertex);
    payload.VertexData.resize(vertices.size() * sizeof(Vertex));
    if (!vertices.empty())
        std::memcpy(payload.VertexData.data(), vertices.data(), payload.VertexData.size());

    if (p.indices >= 0) {
        const auto& a = model.accessors[p.indices]; 
        if (a.bufferView < 0)
            return false;
        const auto& view = model.bufferViews[a.bufferView]; 
        const auto& buffer = model.buffers[view.buffer];

        const size_t componentSize = a.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE ? 1 :
            a.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT ? 2 :
            a.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT ? 4 : 0;
        const size_t stride = a.ByteStride(view) ? a.ByteStride(view) : componentSize;
        if (stride == 0 || view.buffer < 0 || static_cast<size_t>(view.buffer) >= model.buffers.size())
            return false;
        payload.Indices.reserve(a.count);

        for (size_t i = 0; i < a.count; ++i) {
            uint32_t value = 0;
            const size_t offset = view.byteOffset + a.byteOffset + i * stride;
            if (offset > buffer.data.size() || componentSize > buffer.data.size() - offset)
                return false;
            const auto* index = buffer.data.data() + offset;
            if (a.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
                value = *index;
            else if (a.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
            {
                uint16_t v;
                std::memcpy(&v, index, sizeof(v));
                value = v;
            }
            else if (a.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
                std::memcpy(&value, index, sizeof(value));
            else
                return false;
            payload.Indices.push_back(value);
        }
    }
    std::error_code ec;
    const auto path = root / "Meshes" / (MeshArtifactName(mesh, primitive, model.meshes[mesh].name) + ".kbrmesh");
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        Log::CoreError("Failed to create directories for mesh: {}", ec.message());
        return false;
    }

    const bool success = NativeAssetSerializer::SerializeMesh(payload, path);
    if (!success) {
        Log::CoreError("Failed to serialize mesh to path: {}", path.string());
        return false;
    }

    return true;
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
                          const std::filesystem::path& destPath)
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
        spec.Format = image.component == 4 ? ImageFormat::RGBA8
            : image.component == 3 ? ImageFormat::RGB8
            : image.component == 2 ? ImageFormat::RG8
            : image.component == 1 ? ImageFormat::R8 : ImageFormat::RGBA8;

        const Buffer pixels(image.image);
        return KTX2Encoder::Encode(destPath, spec, pixels);
    }
}

bool GLTFSceneImporter::Import(const std::filesystem::path& source, const std::filesystem::path& outputDirectory,
                               const std::filesystem::path& namespacePath, GltfSceneManifest* output)
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
        n.Name = Name(model.nodes[i].name, "Node_", i); n.SkinIndex = model.nodes[i].skin;
        if (model.nodes[i].mesh >= 0 && static_cast<size_t>(model.nodes[i].mesh) < model.meshes.size())
        {
            const auto& mesh = model.meshes[model.nodes[i].mesh];
            n.Primitives.reserve(mesh.primitives.size());
            for (size_t primitiveIndex = 0; primitiveIndex < mesh.primitives.size(); ++primitiveIndex)
            {
                n.Primitives.push_back({
                    .MeshIndex = static_cast<uint32_t>(model.nodes[i].mesh),
                    .PrimitiveIndex = static_cast<uint32_t>(primitiveIndex),
                    .MaterialIndex = mesh.primitives[primitiveIndex].material
                });
            }
        }
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
    const std::filesystem::path artifactRoot = namespacePath.empty() ? outputDirectory : outputDirectory / namespacePath;
    std::filesystem::create_directories(artifactRoot, ec);
    if (ec)
        return false;

    // Emit one native record per primitive and source records for all other GLTF domains.
    for (size_t mi = 0; mi < model.meshes.size(); ++mi)
        for (size_t pi = 0; pi < model.meshes[mi].primitives.size(); ++pi) {
            const auto& prim = model.meshes[mi].primitives[pi];
            if (!WriteMesh(artifactRoot, mi, pi, model, prim)) 
                return false;
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
    for (size_t i = 0; i < model.images.size(); ++i)
    {
        const auto& im = model.images[i];
        std::string imageName = im.name;
        if (imageName.empty() && !im.uri.empty())
            imageName = std::filesystem::path(im.uri).stem().string();
        const std::filesystem::path destPath = artifactRoot / "Textures" / (TextureArtifactName(i, imageName) + ".ktx2");
        if (!WriteTextureKtx2(im, source, destPath))
        {
            Log::CoreWarn("Failed to import texture for glTF image '{}' (index {}); material references to it will be left unbound.", im.name, i);
        }
    }
    std::unordered_set<std::string> usedSkeletonNames;
    usedSkeletonNames.reserve(model.skins.size());
    for (size_t i = 0; i < model.skins.size(); ++i) {
        const auto& s = model.skins[i]; std::ostringstream d; d << "name=" << s.name << ";inverseBindAccessor=" << s.inverseBindMatrices << ";skeleton=" << s.skeleton << ";joints="; for (int j : s.joints) d << j << ",";
        const std::string fileName = UniqueArtifactName(usedSkeletonNames, SanitizeFileName(Name(s.name, "Skeleton_", i)), i);
        if (!WriteRecord(artifactRoot, "Skeletons", Key("skeleton", i), "skeleton", static_cast<uint32_t>(i), d.str(), fileName))
            return false;
    }
    std::unordered_set<std::string> usedAnimationNames;
    usedAnimationNames.reserve(model.animations.size());
    for (size_t i = 0; i < model.animations.size(); ++i) {
        const auto& a = model.animations[i]; std::ostringstream d; d << "name=" << a.name << ";channels=" << a.channels.size() << ";samplers=" << a.samplers.size();
        const std::string fileName = UniqueArtifactName(usedAnimationNames, SanitizeFileName(Name(a.name, "Animation_", i)), i);
        if (!WriteRecord(artifactRoot, "Animations", Key("animation", i), "animation", static_cast<uint32_t>(i), d.str(), fileName))
            return false;
    }
    std::ostringstream prefab; prefab << "name=" << source.stem().string() << ";nodes=" << model.nodes.size() << ";";
    for (size_t i = 0; i < manifest.Nodes.size(); ++i) {
        prefab << i << ":" << manifest.Nodes[i].Name << ":" << manifest.Nodes[i].ParentIndex << ":" << manifest.Nodes[i].SkinIndex << ";";
    }
    if (!WriteRecord(artifactRoot, "", Key("prefab", 0), "prefab", 0,
        prefab.str(), source.stem().string()))
        return false;

    return NativeAssetSerializer::SerializeSceneManifest(manifest, artifactRoot / (source.stem().string() + ".kbrscene"));
}

}