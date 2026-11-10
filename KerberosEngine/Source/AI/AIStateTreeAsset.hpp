#pragma once

#include "AIStateTree.hpp"
#include "AIStateTreeDocument.hpp"
#include "Assets/Asset.hpp"

#include <filesystem>
#include <string>

namespace Kerberos::AI {

/**
 * Persistent AI state tree asset. The document is the editable source form;
 * Compiled is the runtime form used by AIStateMachineComponent.
 *
 * Assets are loaded from .kbrai sources through the asset pipeline. C++/C#
 * trees remain registry-only and are intentionally not writable through this
 * asset.
 */
class AIStateTreeAsset final : public Asset
{
public:
    static constexpr AssetType Type = AssetType::AIStateTree;

    AIStateTreeDocument Document;
    Ref<AIStateTree> Compiled = nullptr;
    /// Name currently registered with AIStateTreeRegistry (transient).
    std::string RegisteredName;

    [[nodiscard]] AssetType GetType() override { return Type; }

    /// Recompiles Document into Compiled, reporting validation errors.
    bool Recompile(std::string* outError = nullptr);

    /// Writes the authored document back to its source path.
    bool SaveSource(const std::filesystem::path& sourcePath, std::string* outError = nullptr) const;

    /// Loads and compiles a .kbrai source. The returned asset is also registered
    /// with AIStateTreeRegistry as an Asset-origin tree when its name does not
    /// collide with a native/script tree.
    static Ref<AIStateTreeAsset> Load(AssetHandle handle,
                                      const std::filesystem::path& sourcePath,
                                      std::string* outError = nullptr);
};

} // namespace Kerberos::AI
