#include "GLTFPipelineImporter.hpp"

#include "GLTFSceneImporter.hpp"
#include "Assets/Formats/NativeAssetFormat.hpp"
#include "Assets/Formats/NativeAssetSerializer.hpp"

#include "Profiling/Profilers.hpp"

#include <string_view>

namespace Kerberos {

namespace {

// Mesh artifacts are named "mesh_<m>_<p>[_<name>].kbrmesh"; recover the stable
// sub-asset key "mesh:<m>_<p>" from the leading index tokens.
std::string MeshKeyFromStem(const std::string_view stem)
{
    constexpr std::string_view prefix = "mesh_";
    if (!stem.starts_with(prefix))
        return {};

    const std::string_view rest = stem.substr(prefix.size());
    const size_t first = rest.find('_');
    if (first == std::string_view::npos)
        return std::string("mesh:") + std::string(rest);
    const size_t second = rest.find('_', first + 1);
    const std::string_view primitive = second == std::string_view::npos
        ? rest.substr(first + 1)
        : rest.substr(first + 1, second - first - 1);
    return std::string("mesh:") + std::string(rest.substr(0, first)) + "_" + std::string(primitive);
}

// Texture artifacts are named "texture_<i>[_<name>].ktx2"; recover the stable
// sub-asset key "texture:<i>" from the leading index token.
std::string TextureKeyFromStem(const std::string_view stem)
{
    constexpr std::string_view prefix = "texture_";
    if (!stem.starts_with(prefix))
        return {};

    const std::string_view rest = stem.substr(prefix.size());
    const size_t end = rest.find('_');
    return std::string("texture:") + std::string(end == std::string_view::npos ? rest : rest.substr(0, end));
}

}

bool GLTFPipelineImporter::SupportsExtension(const std::string_view extension) const
{
    return extension == ".gltf" || extension == ".glb";
}

ImportResult GLTFPipelineImporter::Import(const ImportContext &context)
{
    KBR_TRACY_FUNCTION();

    // Source-unique cache namespace derived from the source path relative to the
    // assets root. This keeps one source's artifacts from overwriting another's.
    std::error_code namespaceError;
    std::filesystem::path namespacePath =
        std::filesystem::relative(context.SourceAbsolutePath, context.AssetRootAbsolutePath, namespaceError);
    if (namespaceError || namespacePath.empty())
        namespacePath = context.SourceAbsolutePath.stem();
    namespacePath.replace_extension();

    ImportResult result;
    GltfSceneManifest manifest;
    if (!GLTFSceneImporter::Import(context.SourceAbsolutePath, context.CacheRootAbsolutePath, namespacePath, &manifest,
                                   &context.Meta.TextureSrgb, &result.TextureSrgb))
        throw std::runtime_error("Failed to build glTF scene");
    result.SourceHandle = context.Meta.SourceHandle.IsValid() ? context.Meta.SourceHandle : AssetHandle();
    if (!result.SourceHandle.IsValid())
    {
        result.SourceHandle = AssetHandle();
    }

    result.SourceLibraryPath = namespacePath / (context.SourceAbsolutePath.stem().string() + ".kbrprefab");

    for (std::error_code ec;
         const auto& entry : std::filesystem::recursive_directory_iterator(context.CacheRootAbsolutePath, ec))
    {
        const auto extension = entry.path().extension();
        const bool isOutput = extension == ".kbrmesh" || extension == ".kbrmaterial" ||
            extension == ".ktx2" || extension == ".kbrskeleton" ||
            extension == ".kbranimation" || extension == ".kbrprefab";
        if (ec || !entry.is_regular_file() || extension == ".kbrscene" || !isOutput)
            continue;

        NativeAssetRecord record;
        AssetType type = AssetType::Prefab;
        if (entry.path().extension() == ".kbrmesh") {
            type = AssetType::Mesh;
            record.LocalKey = MeshKeyFromStem(entry.path().stem().string());
        } else if (entry.path().extension() == ".ktx2") {
            // Textures are imported as KTX2 artifacts named texture_<imageIndex>[<name>].ktx2.
            type = AssetType::Texture2D;
            record.LocalKey = TextureKeyFromStem(entry.path().stem().string());
        } else {
            if (!NativeAssetSerializer::DeserializeRecord(entry.path(), record))
                continue;

            if (record.Kind == "material") type = AssetType::Material;
            else if (record.Kind == "skeleton") type = AssetType::Skin;
            else if (record.Kind == "animation") type = AssetType::Animation;
        }

        AssetHandle handle = AssetHandle::Invalid();
        for (const auto& old : context.Meta.SubAssets) {
            if (old.LocalKey == record.LocalKey) {
                handle = old.Handle; break;
            }
        }

        if (!handle.IsValid())
            handle = AssetHandle();

        result.Outputs.push_back({
            .Handle = handle,
            .Type = type,
            .LibraryRelPath = std::filesystem::relative(entry.path(), context.CacheRootAbsolutePath, ec),
            .SubAssetKey = record.LocalKey,
            .Dependencies = {}});
    }
    for (auto& output : result.Outputs)
    {
        if (output.Type == AssetType::Material)
        {
            for (const auto& candidate : result.Outputs)
                if (candidate.Type == AssetType::Texture2D && candidate.Handle.IsValid())
                    output.Dependencies.push_back(candidate.Handle);
        }
        else if (output.Type == AssetType::Prefab)
        {
            for (const auto& candidate : result.Outputs)
                if (candidate.Type == AssetType::Mesh || candidate.Type == AssetType::Material || candidate.Type == AssetType::Skin || candidate.Type == AssetType::Animation)
                    if (candidate.Handle.IsValid()) output.Dependencies.push_back(candidate.Handle);
        }
    }
    return result;
}

ImporterType GLTFPipelineImporter::Type() const
{
    return ImporterType::GLTFScene;
}

uint32_t GLTFPipelineImporter::Version() const
{
    return 6;
}

AssetType GLTFPipelineImporter::SourceAssetType() const
{
    return AssetType::Prefab;
}

}
