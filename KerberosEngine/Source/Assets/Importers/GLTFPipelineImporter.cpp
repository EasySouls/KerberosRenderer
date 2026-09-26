#include "GLTFPipelineImporter.hpp"

#include "GLTFSceneImporter.hpp"
#include "Assets/Formats/NativeAssetFormat.hpp"
#include "Assets/Formats/NativeAssetSerializer.hpp"

#include "Profiling/Profilers.hpp"

namespace Kerberos {

    bool GLTFPipelineImporter::SupportsExtension(const std::string_view extension) const
    {
        return extension == ".gltf" || extension == ".glb";
    }

    ImportResult GLTFPipelineImporter::Import(const ImportContext &context)
    {
        KBR_TRACY_FUNCTION();

        GltfSceneManifest manifest;
        if (!GLTFSceneImporter::Import(context.SourceAbsolutePath, context.CacheRootAbsolutePath, &manifest))
            throw std::runtime_error("Failed to build glTF scene");
        ImportResult result;
        result.SourceHandle = context.Meta.SourceHandle.IsValid() ? context.Meta.SourceHandle : AssetHandle();
        if (!result.SourceHandle.IsValid())
            result.SourceHandle = AssetHandle();
        result.SourceLibraryPath = std::filesystem::path(context.SourceAbsolutePath.stem().string() + ".kbrscene");

        for (std::error_code ec;
             const auto& entry : std::filesystem::recursive_directory_iterator(context.CacheRootAbsolutePath, ec))
        {
            const auto extension = entry.path().extension();
            const bool isOutput = extension == ".kbrmesh" || extension == ".kbrmaterial" ||
                extension == ".kbrtexture" || extension == ".kbrskeleton" ||
                extension == ".kbranimation" || extension == ".kbrprefab";
            if (ec || !entry.is_regular_file() || extension == ".kbrscene" || !isOutput)
                continue;

            NativeAssetRecord record;
            AssetType type = AssetType::Prefab;
            if (entry.path().extension() == ".kbrmesh") {
                type = AssetType::Mesh;
                record.LocalKey = "mesh:" + entry.path().stem().string().substr(5);
            } else {
                if (!NativeAssetSerializer::DeserializeRecord(entry.path(), record))
                    continue;

                if (record.Kind == "material") type = AssetType::Material;
                else if (record.Kind == "texture") type = AssetType::Texture2D;
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

    AssetType GLTFPipelineImporter::SourceAssetType() const
    {
        return AssetType::Model;
    }

}
