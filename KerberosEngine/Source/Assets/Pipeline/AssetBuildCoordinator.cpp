#include "kbrpch.hpp"
#include "AssetBuildCoordinator.hpp"

#include "Assets/Pipeline/ImportPipeline.hpp"

#include <unordered_set>

namespace Kerberos {

AssetBuildCoordinator::AssetBuildCoordinator(std::filesystem::path assetsRoot,
    std::filesystem::path cacheRoot, AssetMetaService& metaService,
    ImporterRegistry& importers, AssetRegistry* registry)
    : m_AssetsRoot(std::move(assetsRoot)), m_CacheRoot(std::move(cacheRoot)),
      m_MetaService(metaService), m_Importers(importers), m_Registry(registry)
{}

AssetBuildReport AssetBuildCoordinator::Build(const std::filesystem::path& source, const bool force)
{
    KBR_TRACY_FUNCTION();

    std::scoped_lock lock(m_BuildMutex);

    AssetBuildReport report;
    report.Source = source;

    std::error_code ec;
    if (!std::filesystem::is_regular_file(source, ec)) 
    { 
        report.Errors.emplace_back("Source is not a regular file");
        return report; 
    }
    auto importer = m_Importers.Find(source.extension().string());
    if (!importer)
    { 
        report.Errors.emplace_back("No importer registered for extension " + source.extension().string()); 
        return report;
    }

    auto meta = m_MetaService.EnsureMetaForSource(source);
    bool registryComplete = !m_Registry || (meta.SourceHandle.IsValid() && m_Registry->Contains(meta.SourceHandle));
    if (registryComplete && m_Registry)
        for (const auto& subAsset : meta.SubAssets)
            registryComplete = registryComplete && subAsset.Handle.IsValid() && m_Registry->Contains(subAsset.Handle);

    report.Reason = AssetStalenessEvaluator::Evaluate(source, m_CacheRoot, meta, registryComplete, force);
    if (report.Reason == AssetStaleReason::None &&
        (meta.ImporterType != importer->Type() || meta.ImporterVersion != importer->Version())) 
    {
        report.Reason = AssetStaleReason::SettingsChanged;
    }

    // Metadata can be current while the persisted registry is stale (for example
    // after a pipeline migration). Artifact-producing importers must have a valid
    // root LibraryPath and an existing output, otherwise runtime loading would
    // silently fail for an asset the build previously considered up to date.
    if (report.Reason == AssetStaleReason::None && m_Registry && importer->Type() != ImporterType::Standalone)
    {
        if (!meta.SourceHandle.IsValid() || !m_Registry->Contains(meta.SourceHandle))
        {
            report.Reason = AssetStaleReason::RegistryMissing;
        }
        else
        {
            const auto& metadata = m_Registry->Get(meta.SourceHandle);
            if (metadata.LibraryPath.empty())
            {
                report.Reason = AssetStaleReason::RegistryMissing;
            }
            else
            {
                const auto artifactPath = metadata.LibraryPath.is_absolute()
                    ? metadata.LibraryPath
                    : m_CacheRoot / metadata.LibraryPath;
                std::error_code artifactError;
                if (!std::filesystem::is_regular_file(artifactPath, artifactError) || artifactError)
                    report.Reason = AssetStaleReason::OutputMissing;
            }
        }
    }

    if (report.Reason == AssetStaleReason::None) 
    { 
        report.Built = true;
        return report;
    }

    AssetBuildTransaction transaction(m_CacheRoot, source.stem().string());
    if (transaction.StagingRoot().empty()) 
    { 
        report.Errors.emplace_back("Unable to create build staging directory"); 
        return report; 
    }

    ImportContext context{ 
        .SourceAbsolutePath = std::filesystem::absolute(source), 
        .AssetRootAbsolutePath = std::filesystem::absolute(m_AssetsRoot), 
        .CacheRootAbsolutePath = std::filesystem::absolute(transaction.StagingRoot()), 
        .Meta = meta 
    };
    ImportResult result;
    try 
    { 
        result = importer->Import(context); 
    }
    catch (const std::exception& exception) 
    { 
        report.Errors.emplace_back(exception.what()); 
        return report; 
    }
    report.Warnings = result.Warnings;
    std::string commitError;
    if (!transaction.Commit(&commitError)) 
    { 
        report.Errors.push_back(commitError.empty() ? "Failed to commit imported outputs" : commitError); 
        return report; 
    }

    if (result.SourceHandle.IsValid()) 
        meta.SourceHandle = result.SourceHandle;
    if (!meta.SourceHandle.IsValid()) 
        meta.SourceHandle = AssetHandle();

    meta.ImporterType = importer->Type();
    meta.ImporterVersion = importer->Version();
    meta.SourceHash = AssetMetaService::ComputeSourceHash(source);
    meta.TextureSrgb = result.TextureSrgb;
    meta.SubAssets.clear();
    for (const auto& output : result.Outputs)
    {
        const SubAssetKind kind = output.Type == AssetType::Mesh ? SubAssetKind::Mesh :
            output.Type == AssetType::Material ? SubAssetKind::Material :
            output.Type == AssetType::Texture2D ? SubAssetKind::Texture :
            output.Type == AssetType::Animation ? SubAssetKind::AnimationClip :
            output.Type == AssetType::Skeleton ? SubAssetKind::Skeleton : 
            output.Type == AssetType::SkinnedMesh ? SubAssetKind::SkinnedMesh 
            : SubAssetKind::Prefab;

        meta.SubAssets.push_back({ .LocalKey = output.SubAssetKey, .Handle = output.Handle, .Kind = kind });
    }
    if (!m_MetaService.SaveMeta(source, meta)) 
    { 
        report.Errors.emplace_back("Failed to save asset meta"); 
        return report; 
    }
    if (m_Registry)
    {
        std::unordered_set<AssetHandle> currentHandles;
        currentHandles.reserve(result.Outputs.size());
        for (const auto& output : result.Outputs)
            currentHandles.insert(output.Handle);

        std::vector<AssetHandle> staleHandles;
        for (const auto& [assetHandle, assetMetadata] : *m_Registry)
        {
            const bool isGeneratedForSource = assetMetadata.RootSourceHandle == meta.SourceHandle
                && assetHandle != meta.SourceHandle;
            if (isGeneratedForSource && !currentHandles.contains(assetHandle))
                staleHandles.push_back(assetHandle);
        }
        for (const auto assetHandle : staleHandles)
            m_Registry->Remove(assetHandle);

        AssetMetadata root;
        root.Filepath = std::filesystem::relative(source, m_AssetsRoot, ec);
        if (root.Filepath.empty() || ec) 
            root.Filepath = source;

        root.Type = importer->SourceAssetType();
        root.LibraryPath = result.SourceLibraryPath;

        root.RootSourceHandle = meta.SourceHandle;

        m_Registry->Add(meta.SourceHandle, root);
        
        for (const auto& [Handle, Type, LibraryRelPath, SubAssetKey, Dependencies] : result.Outputs)
        {
            AssetMetadata data; data.Type = Type; data.Filepath = LibraryRelPath;
            data.LibraryPath = LibraryRelPath;
            data.RootSourceHandle = meta.SourceHandle; data.ParentHandle = meta.SourceHandle; data.SubAssetKey = SubAssetKey;
            data.Dependencies = Dependencies;
            m_Registry->Add(Handle, data);
        }
    }
    report.Built = true;
    return report;
}

std::future<AssetBuildReport> AssetBuildCoordinator::BuildAsync(std::filesystem::path source, const bool force)
{
    KBR_TRACY_FUNCTION();

    return std::async(std::launch::async, [this, source = std::move(source), force] { 
        return Build(source, force); 
    });
}

std::vector<AssetBuildReport> AssetBuildCoordinator::BuildAll(const std::vector<std::filesystem::path>& sources, const bool force)
{
    KBR_TRACY_FUNCTION();

    std::vector<AssetBuildReport> reports;
    reports.reserve(sources.size());
    for (const auto& source : sources)
    {
        reports.push_back(Build(source, force));
    }
    return reports;
}

void AssetBuildCoordinator::Shutdown() 
{}

}
