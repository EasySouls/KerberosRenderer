#include "AIEditorDocument.hpp"

#include <algorithm>
#include <utility>

namespace Kerberos {

void AIEditorDocument::RebuildGraphReferences()
{
    States.clear();
    Transitions.clear();
    NextEditorID = 1;

    States.reserve(Document.States.size());
    for (size_t stateIndex = 0; stateIndex < Document.States.size(); ++stateIndex)
    {
        States.push_back(AIEditorStateRef{
            .EditorID = NextEditorID++,
            .DocumentStateIndex = stateIndex
        });
    }

    for (size_t stateIndex = 0; stateIndex < Document.States.size(); ++stateIndex)
    {
        const AI::AIDocumentState& state = Document.States[stateIndex];
        for (size_t transitionIndex = 0; transitionIndex < state.Transitions.size(); ++transitionIndex)
        {
            Transitions.push_back(AIEditorTransitionRef{
                .EditorID = NextEditorID++,
                .SourceStateIndex = stateIndex,
                .TransitionIndex = transitionIndex
            });
        }
    }
}

AIEditorStateRef* AIEditorDocument::FindState(const uint64_t editorID)
{
    const auto it = std::ranges::find(States, editorID, &AIEditorStateRef::EditorID);
    return it != States.end() ? &*it : nullptr;
}

const AIEditorStateRef* AIEditorDocument::FindState(const uint64_t editorID) const
{
    const auto it = std::ranges::find(States, editorID, &AIEditorStateRef::EditorID);
    return it != States.end() ? &*it : nullptr;
}

AIEditorTransitionRef* AIEditorDocument::FindTransition(const uint64_t editorID)
{
    const auto it = std::ranges::find(Transitions, editorID, &AIEditorTransitionRef::EditorID);
    return it != Transitions.end() ? &*it : nullptr;
}

const AIEditorTransitionRef* AIEditorDocument::FindTransition(const uint64_t editorID) const
{
    const auto it = std::ranges::find(Transitions, editorID, &AIEditorTransitionRef::EditorID);
    return it != Transitions.end() ? &*it : nullptr;
}

AI::AIDocumentState* AIEditorDocument::ResolveState(const AIEditorStateRef& ref)
{
    if (ref.DocumentStateIndex >= Document.States.size())
        return nullptr;
    return &Document.States[ref.DocumentStateIndex];
}

const AI::AIDocumentState* AIEditorDocument::ResolveState(const AIEditorStateRef& ref) const
{
    if (ref.DocumentStateIndex >= Document.States.size())
        return nullptr;
    return &Document.States[ref.DocumentStateIndex];
}

AI::AIDocumentTransition* AIEditorDocument::ResolveTransition(const AIEditorTransitionRef& ref)
{
    if (ref.SourceStateIndex >= Document.States.size())
        return nullptr;

    AI::AIDocumentState& state = Document.States[ref.SourceStateIndex];
    if (ref.TransitionIndex >= state.Transitions.size())
        return nullptr;

    return &state.Transitions[ref.TransitionIndex];
}

const AI::AIDocumentTransition* AIEditorDocument::ResolveTransition(const AIEditorTransitionRef& ref) const
{
    if (ref.SourceStateIndex >= Document.States.size())
        return nullptr;

    const AI::AIDocumentState& state = Document.States[ref.SourceStateIndex];
    if (ref.TransitionIndex >= state.Transitions.size())
        return nullptr;

    return &state.Transitions[ref.TransitionIndex];
}

uint64_t AIEditorDocument::AddState(std::string name, const glm::vec2 position)
{
    if (name.empty())
        name = "New State";

    if (Document.EntryState.empty())
        Document.EntryState = name;

    AI::AIDocumentState state;
    state.Name = std::move(name);
    state.EditorPosition = position;
    Document.States.push_back(std::move(state));

    const uint64_t editorID = NextEditorID++;
    States.push_back(AIEditorStateRef{
        .EditorID = editorID,
        .DocumentStateIndex = Document.States.size() - 1
    });
    return editorID;
}

uint64_t AIEditorDocument::AddTransition(const size_t sourceStateIndex,
                                         std::string targetState,
                                         std::string name)
{
    if (sourceStateIndex >= Document.States.size())
        return 0;

    AI::AIDocumentTransition transition;
    transition.TargetState = std::move(targetState);
    transition.Name = std::move(name);

    AI::AIDocumentState& sourceState = Document.States[sourceStateIndex];
    sourceState.Transitions.push_back(std::move(transition));

    const uint64_t editorID = NextEditorID++;
    Transitions.push_back(AIEditorTransitionRef{
        .EditorID = editorID,
        .SourceStateIndex = sourceStateIndex,
        .TransitionIndex = sourceState.Transitions.size() - 1
    });
    return editorID;
}

void AIEditorDocument::RemoveState(const uint64_t editorID)
{
    const AIEditorStateRef* stateRef = FindState(editorID);
    if (!stateRef || stateRef->DocumentStateIndex >= Document.States.size())
        return;

    const size_t removedIndex = stateRef->DocumentStateIndex;
    const std::string removedName = Document.States[removedIndex].Name;

    Document.States.erase(Document.States.begin() + static_cast<std::ptrdiff_t>(removedIndex));

    // Remove incoming transitions from every remaining state.
    if (!removedName.empty())
    {
        for (AI::AIDocumentState& state : Document.States)
        {
            std::erase_if(state.Transitions, [&removedName](const AI::AIDocumentTransition& transition) {
                return transition.TargetState == removedName;
            });
        }
    }

    RebuildGraphReferences();
}

void AIEditorDocument::RemoveTransition(const uint64_t editorID)
{
    const AIEditorTransitionRef* transitionRef = FindTransition(editorID);
    if (!transitionRef || transitionRef->SourceStateIndex >= Document.States.size())
        return;

    AI::AIDocumentState& state = Document.States[transitionRef->SourceStateIndex];
    if (transitionRef->TransitionIndex >= state.Transitions.size())
        return;

    state.Transitions.erase(state.Transitions.begin() +
                            static_cast<std::ptrdiff_t>(transitionRef->TransitionIndex));
    RebuildGraphReferences();
}

void AIEditorDocument::MoveTransition(const uint64_t editorID, size_t newIndex)
{
    const AIEditorTransitionRef* transitionRef = FindTransition(editorID);
    if (!transitionRef || transitionRef->SourceStateIndex >= Document.States.size())
        return;

    AI::AIDocumentState& state = Document.States[transitionRef->SourceStateIndex];
    if (state.Transitions.empty() || transitionRef->TransitionIndex >= state.Transitions.size())
        return;

    newIndex = std::min(newIndex, state.Transitions.size() - 1);
    if (newIndex == transitionRef->TransitionIndex)
        return;

    AI::AIDocumentTransition transition = std::move(state.Transitions[transitionRef->TransitionIndex]);
    state.Transitions.erase(state.Transitions.begin() +
                            static_cast<std::ptrdiff_t>(transitionRef->TransitionIndex));
    state.Transitions.insert(state.Transitions.begin() + static_cast<std::ptrdiff_t>(newIndex),
                             std::move(transition));

    RebuildGraphReferences();
}

void AIEditorDocument::RenameState(const uint64_t editorID, std::string newName)
{
    AIEditorStateRef* stateRef = FindState(editorID);
    if (!stateRef || newName.empty())
        return;

    AI::AIDocumentState* state = ResolveState(*stateRef);
    if (!state)
        return;

    const std::string oldName = state->Name;
    if (oldName == newName)
        return;

    state->Name = std::move(newName);
    if (Document.EntryState == oldName)
        Document.EntryState = state->Name;

    for (AI::AIDocumentState& otherState : Document.States)
    {
        for (AI::AIDocumentTransition& transition : otherState.Transitions)
        {
            if (transition.TargetState == oldName)
                transition.TargetState = state->Name;
        }
    }
}

size_t AIEditorDocument::FindStateIndexByName(const std::string_view name) const
{
    const auto it = std::ranges::find(Document.States, name, &AI::AIDocumentState::Name);
    return it != Document.States.end()
        ? static_cast<size_t>(std::distance(Document.States.begin(), it))
        : Document.States.size();
}

bool AIEditorDocument::IsValid() const
{
    return !States.empty();
}

} // namespace Kerberos
