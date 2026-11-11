#pragma once

#include "AI/AIStateTreeDocument.hpp"
#include "Assets/Asset.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace Kerberos {

struct AIEditorStateRef
{
    uint64_t EditorID = 0;          // Session-only graph node ID
    size_t DocumentStateIndex = 0;  // Index into AIStateTreeDocument::States
};

struct AIEditorTransitionRef
{
    uint64_t EditorID = 0;          // Session-only graph link ID
    size_t SourceStateIndex = 0;    // Index into AIStateTreeDocument::States
    size_t TransitionIndex = 0;     // Index into AIDocumentState::Transitions
};

/// Editor-only working copy of an AI state tree. This type deliberately contains
/// no ImGui or node-editor state. AIEditorTab owns the graph/UI session.
struct AIEditorDocument
{
    enum class SourceKind : std::uint8_t
    {
        Asset,
        RegisteredDocument,
        RegisteredCompiled
    };

    SourceKind Source = SourceKind::Asset;
    AssetHandle Asset = AssetHandle::Invalid();
    std::string RegistryName;
    std::filesystem::path SourcePath;

    AI::AIStateTreeDocument Document;               // Engine-authored working copy
    AI::AIStateTreeDocument SavedDocument;          // Dirty baseline
    Ref<AI::AIStateTree> CompiledPreview = nullptr; // Validation/preview

    bool ViewOnly = false;
    bool Dirty = false;
    std::string SourceError;

    std::vector<AIEditorStateRef> States;
    std::vector<AIEditorTransitionRef> Transitions;
    uint64_t NextEditorID = 1;

    /// Rebuilds session graph references in document order. Structural edits
    /// that shift document indices call this, rename/add operations preserve IDs.
    void RebuildGraphReferences();

    [[nodiscard]] AIEditorStateRef* FindState(uint64_t editorID);
    [[nodiscard]] const AIEditorStateRef* FindState(uint64_t editorID) const;
    [[nodiscard]] AIEditorTransitionRef* FindTransition(uint64_t editorID);
    [[nodiscard]] const AIEditorTransitionRef* FindTransition(uint64_t editorID) const;

    [[nodiscard]] AI::AIDocumentState* ResolveState(const AIEditorStateRef& ref);
    [[nodiscard]] const AI::AIDocumentState* ResolveState(const AIEditorStateRef& ref) const;
    [[nodiscard]] AI::AIDocumentTransition* ResolveTransition(const AIEditorTransitionRef& ref);
    [[nodiscard]] const AI::AIDocumentTransition* ResolveTransition(const AIEditorTransitionRef& ref) const;

    /// Adds a state and returns its session editor ID.
    uint64_t AddState(std::string name, glm::vec2 position = glm::vec2(0.0f));

    /// Adds a transition on the given source state and returns its session editor ID.
    uint64_t AddTransition(size_t sourceStateIndex, std::string targetState, std::string name = {});

    /// Structural edits that invalidate indices rebuild session references.
    void RemoveState(uint64_t editorID);
    void RemoveTransition(uint64_t editorID);
    void MoveTransition(uint64_t editorID, size_t newIndex);

    /// Renames a state and rewrites incoming transition targets. The state keeps
    /// its EditorID.
    void RenameState(uint64_t editorID, std::string newName);

    [[nodiscard]] size_t FindStateIndexByName(std::string_view name) const;
    [[nodiscard]] bool IsValid() const;
};

} // namespace Kerberos
