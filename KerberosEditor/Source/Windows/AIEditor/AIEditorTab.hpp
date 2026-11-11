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
    bool GraphNeedsRefit = false;
};

} // namespace Kerberos
