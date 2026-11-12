#include "AIEditorRuntimeView.hpp"

#include "AIEditorConversions.hpp"

#include <algorithm>

namespace Kerberos {

namespace {

uint64_t FindStateEditorID(const AIEditorDocument& document, const size_t documentStateIndex)
{
    for (const AIEditorStateRef& ref : document.States)
    {
        if (ref.DocumentStateIndex == documentStateIndex)
            return ref.EditorID;
    }
    return 0;
}

} // namespace

AIEditorStateHighlight ResolveStateHighlight(const AIEditorDocument& document,
                                             const AI::AIAgentDebugSnapshot& snapshot,
                                             const uint64_t stateEditorID)
{
    if (!snapshot.Valid || stateEditorID == 0)
        return {};

    const uint64_t current = ResolveStateEditorID(document, snapshot.CurrentState);
    const uint64_t previous = ResolveStateEditorID(document, snapshot.PreviousState);
    const uint64_t pending = ResolveStateEditorID(document, snapshot.PendingState);
    const uint64_t evaluated = ResolveStateEditorID(document, snapshot.EvaluatedState);

    return AIEditorStateHighlight{
        .Current = stateEditorID == current,
        .Previous = stateEditorID == previous,
        .Pending = stateEditorID == pending,
        .Evaluated = stateEditorID == evaluated,
    };
}

AIEditorLinkHighlight ResolveLinkHighlight(const AIEditorDocument& document,
                                           const AI::AIAgentDebugSnapshot& snapshot,
                                           const AIEditorTransitionRef& transitionRef)
{
    AIEditorLinkHighlight result;
    if (!snapshot.Valid)
        return result;

    const uint64_t evaluatedEditorID = ResolveStateEditorID(document, snapshot.EvaluatedState);
    const AIEditorStateRef* evaluatedRef = document.FindState(evaluatedEditorID);
    if (!evaluatedRef || evaluatedRef->DocumentStateIndex != transitionRef.SourceStateIndex)
        return result;

    // Candidates are recorded in transition order and evaluation stops at the
    // first winning transition, so the transition index is the candidate index.
    if (transitionRef.TransitionIndex >= snapshot.Candidates.size())
        return result;

    const AI::AITransitionDebugTrace& candidate =
        snapshot.Candidates[transitionRef.TransitionIndex];

    result.Evaluated = true;
    result.Satisfied = candidate.Satisfied;

    for (size_t i = 0; i < snapshot.Candidates.size(); ++i)
    {
        if (snapshot.Candidates[i].Satisfied)
        {
            result.Winner = i == transitionRef.TransitionIndex;
            break;
        }
    }

    return result;
}

std::vector<AIEditorTransitionHistoryRow> BuildTransitionHistory(
    const AIEditorDocument& document, const std::vector<AI::AITransitionEvent>& events)
{
    std::vector<AIEditorTransitionHistoryRow> rows;
    rows.reserve(events.size());

    for (const AI::AITransitionEvent& event : events)
    {
        AIEditorTransitionHistoryRow row;
        row.Time = event.Time;
        row.Agent = event.Entity;
        row.Reason = event.Reason;
        row.FromState = event.FromStateName;
        row.ToState = event.ToStateName;
        row.TransitionName = event.TransitionName;
        row.TimeInPreviousState = event.TimeInPreviousState;

        const size_t fromIndex = document.FindStateIndexByName(event.FromStateName);
        if (fromIndex < document.Document.States.size())
            row.FromStateEditorID = FindStateEditorID(document, fromIndex);

        const size_t toIndex = document.FindStateIndexByName(event.ToStateName);
        if (toIndex < document.Document.States.size())
            row.ToStateEditorID = FindStateEditorID(document, toIndex);

        rows.push_back(std::move(row));
    }

    std::ranges::reverse(rows);
    return rows;
}

const char* TransitionReasonLabel(const AI::AITransitionReason reason)
{
    switch (reason)
    {
    case AI::AITransitionReason::Initialize:    return "Init";
    case AI::AITransitionReason::Condition:     return "Condition";
    case AI::AITransitionReason::ScriptRequest: return "Script";
    }
    return "Unknown";
}

} // namespace Kerberos
