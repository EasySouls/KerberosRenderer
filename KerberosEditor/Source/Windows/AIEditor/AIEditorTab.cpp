#include "AIEditorTab.hpp"

namespace Kerberos {

AIEditorTab::~AIEditorTab()
{
    if (GraphContext)
    {
        ax::NodeEditor::DestroyEditor(GraphContext);
        GraphContext = nullptr;
    }
}

void AIEditorTab::ResetGraphSession()
{
    PositionedStateIDs.clear();
    GraphNeedsRefit = true;
    GraphNeedsSelectionClear = true;
    SelectedStateID = 0;
    SelectedTransitionID = 0;
    PendingNodeSelection = 0;
    PendingLinkSelection = 0;
    PendingSelectionReveal = false;
    ValidationDirty = true;
    RuntimeAgent = UUID::Invalid();
}

void AIEditorTab::RecordDocumentEdit(AIEditorDocumentSnapshot before, std::string name)
{
    UndoRedo.Record(CreateOwner<AIEditorDocumentCommand>(
        &Document, std::move(before), CaptureSnapshot(Document), std::move(name)));
    Document.Dirty = true;
    ValidationDirty = true;
}

}