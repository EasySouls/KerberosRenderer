#pragma once

#include "AIEditorTab.hpp"

#include <cstdint>
#include <unordered_map>

namespace Kerberos {

/**
 * The UI adapter between AIEditorDocument and `imgui-node-editor`
 */
class AIEditorGraph
{
public:
    void Draw(AIEditorTab& tab, bool canEdit);
    
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
                       const AIEditorStateRef& stateRef, bool canEdit);
    void DrawLinks(AIEditorTab& tab, const AIEditorDocument& document);
    void HandleCreate(AIEditorTab& tab, AIEditorDocument& document, bool canEdit);
    void HandleNewLink(AIEditorTab& tab, AIEditorDocument& document, bool canEdit);
    void HandleDelete(AIEditorTab& tab, AIEditorDocument& document, bool canEdit);
    void HandleSelection(AIEditorTab& tab);
    void HandleContextMenus(AIEditorTab& tab, AIEditorDocument& document, bool canEdit);

    static const AIEditorStateRef* FindStateRefByIndex(const AIEditorDocument& document, size_t index);
    
private:
    std::unordered_map<uintptr_t, uint64_t> m_NodeToState;
    std::unordered_map<uintptr_t, PinRole> m_PinRoles;
    std::unordered_map<uintptr_t, uint64_t> m_LinkToTransition;

    uintptr_t m_ContextNode = 0;
    uintptr_t m_ContextLink = 0;
    ImVec2 m_ContextCanvasPosition{ 0.0f, 0.0f };
};

}
