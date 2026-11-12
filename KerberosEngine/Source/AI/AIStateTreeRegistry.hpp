#pragma once

#include "AIStateTree.hpp"
#include "AIStateTreeDocument.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Kerberos::AI {

enum class AIStateTreeOrigin : std::uint8_t
{
    BuiltIn,
    Script,
    Asset,
};

class AIStateTreeRegistry
{
public:
    static bool Register(std::string name, Ref<AIStateTree>, AIStateTreeOrigin = AIStateTreeOrigin::BuiltIn);
    static bool RegisterDocument(std::string name,
                                 Ref<AIStateTree>,
                                 AIStateTreeOrigin,
                                 AIStateTreeDocument document);
    static bool RegisterFromDocument(const std::string& name,
                                     std::string_view json,
                                     AIStateTreeOrigin,
                                     std::string* outError = nullptr);
    static bool Unregister(std::string_view name);

    /// Compiles an authored document into a runtime tree. Returns nullptr and
    /// reports a validation error when the document is malformed.
    static Ref<AIStateTree> CompileDocument(const AIStateTreeDocument& document,
                                            std::string* outError = nullptr);

    static Ref<AIStateTree> Find(std::string_view name);

    /// Returns the authored document for a tree registered from a document, or
    /// nullptr for native trees registered only as compiled objects.
    static const AIStateTreeDocument* GetDocument(std::string_view name);

    /// Returns the registration origin, or nullopt when the name is unknown.
    static std::optional<AIStateTreeOrigin> GetOrigin(std::string_view name);

    static std::vector<std::string> GetNames();
    static void ClearScriptTrees();
    static void ClearAssetTrees();
    static void ClearAll();
};

}
