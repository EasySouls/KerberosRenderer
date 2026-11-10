#include "AIStateTreeAsset.hpp"

#include "AIStateTreeRegistry.hpp"
#include "AIStateTreeSerializer.hpp"

#include <optional>
#include <utility>

import Kerberos;

namespace Kerberos::AI {

bool AIStateTreeAsset::Recompile(std::string* outError)
{
    if (Document.Name.empty())
        return false;

    std::string error;
    Ref<AIStateTree> compiled = AIStateTreeRegistry::CompileDocument(Document, &error);
    if (!compiled)
    {
        if (outError)
            *outError = std::move(error);
        return false;
    }

    Compiled = std::move(compiled);

    // Keep name-based discovery coherent after editor edits. Asset-handle
    // bindings already read Compiled directly.
    if (!RegisteredName.empty() && RegisteredName != Document.Name)
    {
        if (const std::optional<AIStateTreeOrigin> oldOrigin = AIStateTreeRegistry::GetOrigin(RegisteredName);
            oldOrigin && *oldOrigin == AIStateTreeOrigin::Asset)
        {
            AIStateTreeRegistry::Unregister(RegisteredName);
        }
        RegisteredName.clear();
    }

    const std::optional<AIStateTreeOrigin> existing = AIStateTreeRegistry::GetOrigin(Document.Name);
    if (!existing || *existing == AIStateTreeOrigin::Asset)
    {
        AIStateTreeRegistry::RegisterDocument(Document.Name,
                                              Compiled,
                                              AIStateTreeOrigin::Asset,
                                              Document);
        RegisteredName = Document.Name;
    }

    return true;
}

bool AIStateTreeAsset::SaveSource(const std::filesystem::path& sourcePath, std::string* outError) const
{
    return AIStateTreeSerializer::SaveFile(sourcePath, Document, outError);
}

Ref<AIStateTreeAsset> AIStateTreeAsset::Load(const AssetHandle handle,
                                             const std::filesystem::path& sourcePath,
                                             std::string* outError)
{
    AIStateTreeDocument document;
    if (!AIStateTreeSerializer::LoadFile(sourcePath, document, outError))
        return nullptr;

    if (document.Name.empty())
        document.Name = sourcePath.stem().string();

    std::string error;
    Ref<AIStateTree> compiled = AIStateTreeRegistry::CompileDocument(document, &error);
    if (!compiled)
    {
        if (outError)
            *outError = "Failed to compile AI state tree '" + sourcePath.string() + "': " + error;
        return nullptr;
    }

    auto asset = CreateRef<AIStateTreeAsset>();
    asset->GetHandle() = handle;
    asset->Document = std::move(document);
    asset->Compiled = std::move(compiled);

    // Asset trees can be discovered by name when they do not shadow a native or
    // script tree. Entity components bind directly through TreeAsset.
    const std::optional<AIStateTreeOrigin> existing = AIStateTreeRegistry::GetOrigin(asset->Document.Name);
    if (!existing || *existing == AIStateTreeOrigin::Asset)
    {
        AIStateTreeRegistry::RegisterDocument(asset->Document.Name,
                                              asset->Compiled,
                                              AIStateTreeOrigin::Asset,
                                              asset->Document);
        asset->RegisteredName = asset->Document.Name;
    }
    else
    {
        Log::CoreWarn("AI state tree asset '{}' uses the name '{}', which is already registered by a "
                      "native/script tree. The asset remains usable through its handle.",
                      sourcePath.string(), asset->Document.Name);
    }

    return asset;
}

} // namespace Kerberos::AI
