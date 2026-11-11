#pragma once

#include "AIEditorDocument.hpp"

#include "AI/AIDebug.hpp"
#include "Core/UUID.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace Kerberos {

/// Runtime decoration for one state node. All flags are derived from the
/// selected agent's last AIAgentDebugSnapshot.
struct AIEditorStateHighlight
{
    bool Current = false;
    bool Previous = false;
    bool Pending = false;
    bool Evaluated = false;

    [[nodiscard]] bool Any() const
    {
        return Current || Previous || Pending || Evaluated;
    }
};

/// Runtime decoration for one transition link.
struct AIEditorLinkHighlight
{
    bool Evaluated = false;
    bool Satisfied = false;
    bool Winner = false;
};

/// One row of the transition history table.
struct AIEditorTransitionHistoryRow
{
    double Time = 0.0;
    UUID Agent = UUID::Invalid();
    AI::AITransitionReason Reason = AI::AITransitionReason::Condition;
    std::string FromState;
    std::string ToState;
    std::string TransitionName;
    float TimeInPreviousState = 0.0f;
    uint64_t FromStateEditorID = 0;
    uint64_t ToStateEditorID = 0;
};

/// Resolves the snapshot's state IDs against the document's state names.
[[nodiscard]] AIEditorStateHighlight ResolveStateHighlight(
    const AIEditorDocument& document, const AI::AIAgentDebugSnapshot& snapshot,
    uint64_t stateEditorID);

/// Matches a transition to the evaluated candidate list of the snapshot.
[[nodiscard]] AIEditorLinkHighlight ResolveLinkHighlight(
    const AIEditorDocument& document, const AI::AIAgentDebugSnapshot& snapshot,
    const AIEditorTransitionRef& transitionRef);

/// Converts capture-relative transition events to UI rows, newest first, and
/// resolves state names back to session editor IDs.
[[nodiscard]] std::vector<AIEditorTransitionHistoryRow> BuildTransitionHistory(
    const AIEditorDocument& document, const std::vector<AI::AITransitionEvent>& events);

[[nodiscard]] const char* TransitionReasonLabel(AI::AITransitionReason reason);

} // namespace Kerberos
