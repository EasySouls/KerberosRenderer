#pragma once

#include "AIEditorDocument.hpp"

#include <filesystem>
#include <string>

namespace Kerberos {

/// Editor-side adapter around the asset manager and AIStateTreeAsset.
class AIEditorAssetSession
{
public:
    bool OpenAsset(AssetHandle handle, AIEditorDocument& outDocument, std::string* outError = nullptr) const;

    bool SaveAsset(AIEditorDocument& document, std::string* outError = nullptr) const;
    bool RevertAsset(AIEditorDocument& document, std::string* outError = nullptr) const;

    bool CreateAsset(const std::filesystem::path& sourcePath,
                     const AI::AIStateTreeDocument& initialDocument,
                     AssetHandle& outHandle,
                     std::string* outError = nullptr) const;

private:
    static std::filesystem::path ResolveSourcePath(const std::filesystem::path& filepath);
};

} // namespace Kerberos
