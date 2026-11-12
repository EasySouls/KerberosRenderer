#pragma once

#include "AIEditorDocumentCommand.hpp"
#include "AIEditorTab.hpp"

#include "Core/Core.hpp"
#include "Core/UUID.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Kerberos {

class Scene;

class AIEditorInspector
{
public:
    void Draw(AIEditorTab& tab, bool canEdit, const Ref<Scene>& scene, bool isRunning);

private:
    void DrawStateInspector(AIEditorTab& tab, AIEditorDocument& document,
                            AIEditorStateRef& stateRef, bool canEdit,
                            const Ref<Scene>& activeScene, bool isRunning);
    void DrawTransitionInspector(AIEditorTab& tab, AIEditorDocument& document,
                                 AIEditorTransitionRef& transitionRef, bool canEdit,
                                 const Ref<Scene>& activeScene, bool isRunning);
    void DrawNodeList(AIEditorTab& tab, AIEditorDocument& document,
                      std::vector<AI::AINodeDesc>& nodes, AI::AINodeKind kind,
                      const char* label, bool canEdit);
    void DrawNodeParams(AIEditorTab& tab, const AIEditorDocument& document,
                        AI::AINodeDesc& node, const AI::AINodeTypeInfo& info,
                        bool canEdit);

    void DrawStateRuntime(AIEditorTab& tab, const AIEditorDocument& document,
                          const AIEditorStateRef& stateRef,
                          const Ref<Scene>& activeScene);
    void DrawTransitionRuntime(AIEditorTab& tab, const AIEditorDocument& document,
                               const AIEditorTransitionRef& transitionRef,
                               const Ref<Scene>& activeScene);

    void TrackEdit(AIEditorTab& tab, const AIEditorDocument& document, const char* label);

private:
    std::optional<AIEditorDocumentSnapshot> m_PendingEdit;
    std::optional<AIEditorDocumentSnapshot> m_ComboPending;
    unsigned int m_ComboPendingID = 0;
    char m_StateNameBuffer[128]{};
    char m_TransitionNameBuffer[128]{};
};

} // namespace Kerberos
