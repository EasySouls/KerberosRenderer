#include "AIEditorDocument.hpp"

#include "Profiling/TracyInstrumentation.hpp"

#include <algorithm>
#include <functional>
#include <limits>
#include <queue>
#include <unordered_map>
#include <utility>

import Kerberos;

namespace Kerberos {

void AIEditorDocument::RebuildGraphReferences()
{
    KBR_TRACY_FUNCTION();
    
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

    const size_t index = stateRef->DocumentStateIndex;
    RemoveStates(std::span<const size_t>(&index, 1));
}

void AIEditorDocument::RemoveStates(const std::span<const size_t> documentStateIndices)
{
    if (documentStateIndices.empty())
        return;

    std::vector<size_t> indices(documentStateIndices.begin(), documentStateIndices.end());
    std::ranges::sort(indices, std::greater<>());
    indices.erase(std::ranges::unique(indices).begin(), indices.end());

    std::vector<std::string> removedNames;
    removedNames.reserve(indices.size());
    for (const size_t index : indices)
    {
        if (index >= Document.States.size())
            continue;

        removedNames.push_back(Document.States[index].Name);
        Document.States.erase(Document.States.begin() + static_cast<std::ptrdiff_t>(index));
    }

    if (removedNames.empty())
        return;

    // Remove incoming transitions from every remaining state.
    for (AI::AIDocumentState& state : Document.States)
    {
        std::erase_if(state.Transitions, [&removedNames](const AI::AIDocumentTransition& transition) {
            return std::ranges::find(removedNames, transition.TargetState) != removedNames.end();
        });
    }

    if (std::ranges::find(removedNames, Document.EntryState) != removedNames.end())
    {
        Document.EntryState = Document.States.empty()
            ? std::string{}
            : Document.States.front().Name;
    }

    RebuildGraphReferences();
}

void AIEditorDocument::RemoveTransition(const uint64_t editorID)
{
    const AIEditorTransitionRef* transitionRef = FindTransition(editorID);
    if (!transitionRef || transitionRef->SourceStateIndex >= Document.States.size())
        return;

    const std::pair indices{ transitionRef->SourceStateIndex, transitionRef->TransitionIndex };
    RemoveTransitions(std::span<const std::pair<size_t, size_t>>(&indices, 1));
}

void AIEditorDocument::RemoveTransitions(
    const std::span<const std::pair<size_t, size_t>> transitions)
{
    if (transitions.empty())
        return;

    std::vector<std::pair<size_t, size_t>> items(transitions.begin(), transitions.end());
    std::ranges::sort(items, std::greater<>());
    items.erase(std::ranges::unique(items).begin(), items.end());

    bool removed = false;
    for (const auto& [stateIndex, transitionIndex] : items)
    {
        if (stateIndex >= Document.States.size())
            continue;

        AI::AIDocumentState& state = Document.States[stateIndex];
        if (transitionIndex >= state.Transitions.size())
            continue;

        state.Transitions.erase(state.Transitions.begin() +
                                static_cast<std::ptrdiff_t>(transitionIndex));
        removed = true;
    }

    if (removed)
        RebuildGraphReferences();
}

void AIEditorDocument::MoveTransition(const uint64_t editorID, const size_t newIndex)
{
    const AIEditorTransitionRef* transitionRef = FindTransition(editorID);
    if (!transitionRef)
        return;

    MoveTransitionByIndex(transitionRef->SourceStateIndex, transitionRef->TransitionIndex, newIndex);
}

uint64_t AIEditorDocument::MoveTransitionByIndex(const size_t sourceStateIndex,
                                                 const size_t fromIndex,
                                                 const size_t toIndex)
{
    if (sourceStateIndex >= Document.States.size())
        return 0;

    AI::AIDocumentState& state = Document.States[sourceStateIndex];
    if (fromIndex >= state.Transitions.size())
        return 0;

    const size_t targetIndex = std::min(toIndex, state.Transitions.size() - 1);
    if (fromIndex == targetIndex)
    {
        for (const AIEditorTransitionRef& ref : Transitions)
        {
            if (ref.SourceStateIndex == sourceStateIndex && ref.TransitionIndex == fromIndex)
                return ref.EditorID;
        }
        return 0;
    }

    AI::AIDocumentTransition transition = std::move(state.Transitions[fromIndex]);
    state.Transitions.erase(state.Transitions.begin() +
                            static_cast<std::ptrdiff_t>(fromIndex));
    state.Transitions.insert(state.Transitions.begin() +
                             static_cast<std::ptrdiff_t>(targetIndex),
                             std::move(transition));

    RebuildGraphReferences();

    for (const AIEditorTransitionRef& ref : Transitions)
    {
        if (ref.SourceStateIndex == sourceStateIndex && ref.TransitionIndex == targetIndex)
            return ref.EditorID;
    }
    return 0;
}

uint64_t AIEditorDocument::DuplicateState(const uint64_t editorID, const glm::vec2 offset)
{
    const AIEditorStateRef* stateRef = FindState(editorID);
    if (!stateRef)
        return 0;

    const AI::AIDocumentState* source = ResolveState(*stateRef);
    if (!source)
        return 0;

    AI::AIDocumentState copy = *source;
    copy.Name = GenerateUniqueStateName(source->Name + " Copy");
    copy.EditorPosition += offset;

    const size_t newStateIndex = Document.States.size();
    Document.States.push_back(std::move(copy));

    const uint64_t newEditorID = NextEditorID++;
    States.push_back(AIEditorStateRef{
        .EditorID = newEditorID,
        .DocumentStateIndex = newStateIndex
    });

    const AI::AIDocumentState& inserted = Document.States[newStateIndex];
    for (size_t transitionIndex = 0; transitionIndex < inserted.Transitions.size(); ++transitionIndex)
    {
        Transitions.push_back(AIEditorTransitionRef{
            .EditorID = NextEditorID++,
            .SourceStateIndex = newStateIndex,
            .TransitionIndex = transitionIndex
        });
    }

    return newEditorID;
}

void AIEditorDocument::AutoLayoutStates(const glm::vec2 origin,
                                        const float columnSpacing,
                                        const float rowSpacing)
{
    const size_t stateCount = Document.States.size();
    if (stateCount == 0)
        return;

    std::unordered_map<std::string, size_t> indexByName;
    indexByName.reserve(stateCount);
    for (size_t i = 0; i < stateCount; ++i)
        indexByName.emplace(Document.States[i].Name, i);

    constexpr size_t unvisited = std::numeric_limits<size_t>::max();
    std::vector<size_t> depths(stateCount, unvisited);

    size_t entryIndex = FindStateIndexByName(Document.EntryState);
    if (entryIndex >= stateCount)
        entryIndex = 0;

    std::queue<size_t> pending;
    depths[entryIndex] = 0;
    pending.push(entryIndex);

    size_t maxDepth = 0;
    while (!pending.empty())
    {
        const size_t stateIndex = pending.front();
        pending.pop();

        for (const AI::AIDocumentTransition& transition : Document.States[stateIndex].Transitions)
        {
            const auto target = indexByName.find(transition.TargetState);
            if (target == indexByName.end() || depths[target->second] != unvisited)
                continue;

            depths[target->second] = depths[stateIndex] + 1;
            maxDepth = std::max(maxDepth, depths[target->second]);
            pending.push(target->second);
        }
    }

    const size_t orphanDepth = maxDepth + 1;
    for (size_t& depth : depths)
    {
        if (depth == unvisited)
            depth = orphanDepth;
    }

    std::vector<size_t> rows(orphanDepth + 1, 0);
    for (size_t i = 0; i < stateCount; ++i)
    {
        const size_t depth = depths[i];
        Document.States[i].EditorPosition = origin +
            glm::vec2(static_cast<float>(depth) * columnSpacing,
                      static_cast<float>(rows[depth]++) * rowSpacing);
    }
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

std::string AIEditorDocument::GenerateUniqueStateName(const std::string_view base) const
{
    const std::string prefix = base.empty() ? "New State" : std::string(base);
    if (FindStateIndexByName(prefix) == Document.States.size())
        return prefix;

    for (size_t suffix = 2;; ++suffix)
    {
        std::string candidate = prefix + " " + std::to_string(suffix);
        if (FindStateIndexByName(candidate) == Document.States.size())
            return candidate;
    }
}

std::string AIEditorDocument::GenerateUniqueTransitionName(
    const AI::AIDocumentState& state, const std::string_view base) const
{
    const std::string prefix = base.empty() ? "Transition" : std::string(base);
    const auto isUsed = [&state](const std::string& name) {
        return std::ranges::any_of(state.Transitions,
            [&name](const AI::AIDocumentTransition& transition) {
                return transition.Name == name;
            });
    };

    if (!isUsed(prefix))
        return prefix;

    for (size_t suffix = 2;; ++suffix)
    {
        std::string candidate = prefix + " " + std::to_string(suffix);
        if (!isUsed(candidate))
            return candidate;
    }
}

bool AIEditorDocument::IsValid() const
{
    return !States.empty();
}

} // namespace Kerberos
