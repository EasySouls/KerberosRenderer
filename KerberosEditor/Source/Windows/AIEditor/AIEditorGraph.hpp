#pragma once

#include "AIEditorTab.hpp"

#include "AI/AIDebug.hpp"

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace Kerberos {

/**
 * The UI adapter between AIEditorDocument and `imgui-node-editor`
 */
class AIEditorGraph
{
public:
    void Draw(AIEditorTab& tab, bool canEdit,
              const AI::AIAgentDebugSnapshot* runtimeSnapshot = nullptr);
    
private:
    enum class PinRoleKind : std::uint8_t
    {
        StateInput,
        TransitionOutput,
        NewTransition,
    };
    
    struct PinRole
    {
        PinRoleKind Kind = PinRoleKind::StateInput;
        uint64_t StateEditorID = 0;
        uint64_t TransitionEditorID = 0;
    };
    void DrawStateNode(AIEditorTab& tab, AIEditorDocument& document,
                       const AIEditorStateRef& stateRef, bool canEdit,
                       const AI::AIAgentDebugSnapshot* runtimeSnapshot);
    void DrawLinks(const AIEditorDocument& document,
                   const AI::AIAgentDebugSnapshot* runtimeSnapshot);
    void HandleCreate(AIEditorTab& tab, AIEditorDocument& document, bool canEdit);
    void HandleNewLink(AIEditorTab& tab, AIEditorDocument& document, bool canEdit);
    void HandleDelete(AIEditorTab& tab, AIEditorDocument& document, bool canEdit);
    void HandleSelection(AIEditorTab& tab);
    void HandleContextMenus(AIEditorTab& tab, AIEditorDocument& document, bool canEdit);

    static void ApplyPendingSelection(AIEditorTab& tab);
    void RecordPositionEditIfFinished(AIEditorTab& tab);

    static const AIEditorStateRef* FindStateRefByIndex(const AIEditorDocument& document, size_t index);
    
private:
    std::unordered_map<uintptr_t, uint64_t> m_NodeToState;
    std::unordered_map<uintptr_t, PinRole> m_PinRoles;
    std::unordered_map<uintptr_t, uint64_t> m_LinkToTransition;

    std::unordered_set<uint64_t> m_ErrorStateIDs;
    std::unordered_set<uint64_t> m_WarningStateIDs;
    std::unordered_set<uint64_t> m_ErrorTransitionIDs;
    std::unordered_set<uint64_t> m_WarningTransitionIDs;

    std::optional<AIEditorDocumentSnapshot> m_PendingPositionEdit;

    uintptr_t m_ContextNode = 0;
    uintptr_t m_ContextLink = 0;
    ImVec2 m_ContextCanvasPosition{ 0.0f, 0.0f };

    uint64_t m_RenameStateID = 0;
    char m_RenameBuffer[128]{};
    bool m_OpenRenamePopup = false;
};

}
