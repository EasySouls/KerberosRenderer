#include "AIEditorAssetSession.hpp"

#include "AI/AIStateTreeAsset.hpp"
#include "AI/AIStateTreeSerializer.hpp"
#include "Assets/AssetManager.hpp"
#include "Assets/EditorAssetManager.hpp"
#include "Project/Project.hpp"

#include <utility>

namespace Kerberos {

namespace {

void SetError(std::string* outError, std::string message)
{
    if (outError)
        *outError = std::move(message);
}

Ref<EditorAssetManager> GetEditorAssetManager()
{
    const Ref<Project> project = Project::GetActive();
    return project ? project->GetEditorAssetManager() : nullptr;
}

} // namespace

std::filesystem::path AIEditorAssetSession::ResolveSourcePath(const std::filesystem::path& filepath)
{
    if (filepath.is_absolute())
        return filepath;
    return Project::GetAssetDirectory() / filepath;
}

bool AIEditorAssetSession::OpenAsset(const AssetHandle handle,
                                     AIEditorDocument& outDocument,
                                     std::string* outError) const
{
    outDocument = {};

    const Ref<EditorAssetManager> manager = GetEditorAssetManager();
    if (!manager)
    {
        SetError(outError, "No editor asset manager is active.");
        return false;
    }

    if (!handle.IsValid() || !manager->IsAssetHandleValid(handle))
    {
        SetError(outError, "The selected AI state tree asset is not imported.");
        return false;
    }

    const AssetMetadata& metadata = manager->GetMetadata(handle);
    if (metadata.Type != AssetType::AIStateTree)
    {
        SetError(outError, "The selected asset is not an AI state tree.");
        return false;
    }

    if (metadata.SourceRoot == AssetSourceRoot::Engine)
    {
        SetError(outError, "Engine AI state trees are read-only.");
        return false;
    }

    const Ref<AI::AIStateTreeAsset> asset = AssetManager::AsAsset<AI::AIStateTreeAsset>(handle);
    if (!asset)
    {
        SetError(outError, "Failed to load the AI state tree asset.");
        return false;
    }

    outDocument.Source = AIEditorDocument::SourceKind::Asset;
    outDocument.Asset = handle;
    outDocument.RegistryName.clear();
    outDocument.SourcePath = ResolveSourcePath(metadata.Filepath);
    outDocument.Document = asset->Document;
    outDocument.SavedDocument = asset->Document;
    outDocument.CompiledPreview = asset->Compiled;
    outDocument.ViewOnly = false;
    outDocument.Dirty = false;
    outDocument.SourceError.clear();
    outDocument.RebuildGraphReferences();

    return true;
}

bool AIEditorAssetSession::SaveAsset(AIEditorDocument& document, std::string* outError) const
{
    if (document.ViewOnly || !document.Asset.IsValid())
    {
        SetError(outError, "This document is not an editable AI state tree asset.");
        return false;
    }

    if (document.SourcePath.empty())
    {
        SetError(outError, "The AI state tree source path is missing.");
        return false;
    }

    const Ref<EditorAssetManager> manager = GetEditorAssetManager();
    if (!manager)
    {
        SetError(outError, "No editor asset manager is active.");
        return false;
    }

    Ref<AI::AIStateTreeAsset> asset = AssetManager::AsAsset<AI::AIStateTreeAsset>(document.Asset);
    if (!asset)
    {
        SetError(outError, "Failed to load the AI state tree asset for saving.");
        return false;
    }

    std::string error;
    asset->Document = document.Document;
    if (!asset->Recompile(&error))
    {
        SetError(outError, "AI state tree validation failed: " + error);
        return false;
    }

    if (!asset->SaveSource(document.SourcePath, &error))
    {
        SetError(outError, error);
        return false;
    }

    if (!manager->ReimportAsset(document.SourcePath).IsValid())
    {
        SetError(outError, "The AI state tree source was saved, but reimport failed.");
        return false;
    }

    const Ref<AI::AIStateTreeAsset> reloaded = AssetManager::AsAsset<AI::AIStateTreeAsset>(document.Asset);
    if (!reloaded)
    {
        SetError(outError, "The AI state tree was saved, but the imported asset could not be reloaded.");
        return false;
    }

    document.Document = reloaded->Document;
    document.SavedDocument = reloaded->Document;
    document.CompiledPreview = reloaded->Compiled;
    document.Dirty = false;
    document.SourceError.clear();
    document.RebuildGraphReferences();
    return true;
}

bool AIEditorAssetSession::RevertAsset(AIEditorDocument& document, std::string* outError) const
{
    if (document.ViewOnly || document.SourcePath.empty())
    {
        SetError(outError, "This document has no editable source to revert.");
        return false;
    }

    std::string error;
    const Ref<AI::AIStateTreeAsset> asset =
        AI::AIStateTreeAsset::Load(document.Asset, document.SourcePath, &error);
    if (!asset)
    {
        SetError(outError, "Failed to reload the AI state tree source: " + error);
        return false;
    }

    document.Document = asset->Document;
    document.SavedDocument = asset->Document;
    document.CompiledPreview = asset->Compiled;
    document.Dirty = false;
    document.SourceError.clear();
    document.RebuildGraphReferences();
    return true;
}

bool AIEditorAssetSession::CreateAsset(const std::filesystem::path& sourcePath,
                                       const AI::AIStateTreeDocument& initialDocument,
                                       AssetHandle& outHandle,
                                       std::string* outError) const
{
    outHandle = AssetHandle::Invalid();

    if (sourcePath.extension() != ".kbrai")
    {
        SetError(outError, "AI state tree assets must use the .kbrai extension.");
        return false;
    }

    const Ref<EditorAssetManager> manager = GetEditorAssetManager();
    if (!manager)
    {
        SetError(outError, "No editor asset manager is active.");
        return false;
    }

    std::string error;
    if (!AI::AIStateTreeSerializer::SaveFile(sourcePath, initialDocument, &error))
    {
        SetError(outError, error);
        return false;
    }

    outHandle = manager->ImportAsset(sourcePath);
    if (!outHandle.IsValid())
    {
        SetError(outError, "The AI state tree source was written, but import failed.");
        return false;
    }

    return true;
}

} // namespace Kerberos
