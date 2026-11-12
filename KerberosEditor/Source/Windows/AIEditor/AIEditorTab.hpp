#pragma once

#include "AIEditorDocument.hpp"
#include "AIEditorDocumentCommand.hpp"
#include "AIEditorValidator.hpp"
#include "Core/UUID.hpp"
#include "Undo/UndoRedoManager.hpp"

#include "imgui_node_editor.h"

#include <string>
#include <unordered_set>

namespace Kerberos {

/// UI/session state for one open AI document. The document model itself stays
/// free of ImGui and node-editor dependencies.
struct AIEditorTab
{
    ~AIEditorTab();

    AIEditorDocument Document;
    UndoRedoManager UndoRedo;
    ax::NodeEditor::EditorContext* GraphContext = nullptr;

    AIEditorValidationResult Validation;
    bool ValidationDirty = true;

    uint64_t SelectedStateID = 0;
    uint64_t SelectedTransitionID = 0;
    
    // Session-only graph selection request. Consumed inside AIEditorGraph::Draw
    // after nodes and links are submitted.
    uint64_t PendingNodeSelection = 0;    // State editor ID
    uint64_t PendingLinkSelection = 0;    // Transition editor ID
    bool PendingSelectionReveal = false;  // NavigateToSelection
    
    // Prevents SetNodePosition from resetting nodes every frame.
    std::unordered_set<uint64_t> PositionedStateIDs;
    // Triggers NavigateToContent once after opening or a structural rebuild.
    bool GraphNeedsRefit = true;
    // Clears node-editor selection inside the next Begin/End canvas frame.
    bool GraphNeedsSelectionClear = false;
    
    // Used for the agent picker in AIEditorInspector
    UUID RuntimeAgent = UUID::Invalid();

    /// Clears session-only graph state after the document identity changes
    /// (undo/redo, revert, or a structural delete). Editor IDs may have been
    /// renumbered, so nodes are repositioned and the view is refit.
    void ResetGraphSession();
    
    /// Records an already-applied document edit and marks the tab dirty and
    /// structurally changed.
    void RecordDocumentEdit(AIEditorDocumentSnapshot before, std::string name);
};

} // namespace Kerberos
