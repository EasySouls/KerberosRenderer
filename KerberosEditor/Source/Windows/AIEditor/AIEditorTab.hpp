#pragma once

#include "AIEditorDocument.hpp"
#include "AIEditorValidator.hpp"
#include "Undo/UndoRedoManager.hpp"

#include "imgui_node_editor.h"

#include <unordered_set>

namespace Kerberos {

/// UI/session state for one open AI document. The document model itself stays
/// free of ImGui and node-editor dependencies.
struct AIEditorTab
{
    ~AIEditorTab()
    {
        if (GraphContext)
        {
            ax::NodeEditor::DestroyEditor(GraphContext);
            GraphContext = nullptr;
        }
    }

    AIEditorDocument Document;
    UndoRedoManager UndoRedo;
    ax::NodeEditor::EditorContext* GraphContext = nullptr;

    AIEditorValidationResult Validation;
    bool ValidationDirty = true;

    uint64_t SelectedStateID = 0;
    uint64_t SelectedTransitionID = 0;
    
    // Prevents SetNodePosition from resetting nodes every frame.
    std::unordered_set<uint64_t> PositionedStateIDs;
    // Triggers NavigateToContent once after opening or a structural rebuild.
    bool GraphNeedsRefit = true;
    // Clears node-editor selection inside the next Begin/End canvas frame.
    bool GraphNeedsSelectionClear = false;

    /// Clears session-only graph state after the document identity changes
    /// (undo/redo, revert, or a structural delete). Editor IDs may have been
    /// renumbered, so nodes are repositioned and the view is refit.
    void ResetGraphSession()
    {
        PositionedStateIDs.clear();
        GraphNeedsRefit = true;
        GraphNeedsSelectionClear = true;
        SelectedStateID = 0;
        SelectedTransitionID = 0;
        ValidationDirty = true;
    }
};

} // namespace Kerberos
