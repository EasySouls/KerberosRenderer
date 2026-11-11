#pragma once

#include "AIEditorTab.hpp"

#include "Core/Core.hpp"
#include "Core/UUID.hpp"

namespace Kerberos {

class Scene;

/// Draws the shared runtime agent picker for the open tree and stores the
/// selection in the tab. Returns the selected agent, or an invalid UUID.
UUID DrawAIAgentPicker(AIEditorTab& tab, const AIEditorDocument& document,
                       const Ref<Scene>& scene);

/// Draws the capture-relative transition history table. Clicking a row selects
/// the matching state or transition and the agent that produced the event.
void DrawAITransitionHistory(AIEditorTab& tab, const AIEditorDocument& document,
                             const Ref<Scene>& scene, bool isRunning);

} // namespace Kerberos
