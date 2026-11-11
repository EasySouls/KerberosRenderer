#include "AIEditorConversions.hpp"

#include "AI/AIIdentifier.hpp"
#include "AI/AIStateTree.hpp"

#include <algorithm>
#include <ranges>
#include <vector>

namespace Kerberos {

namespace {

std::string StateNameForID(const AI::AIStateTree& tree, const AI::StateID stateID)
{
    if (const AI::AIStateDefinition* state = tree.FindState(stateID))
        return state->Name;
    return std::to_string(stateID);
}

} // namespace

AI::AIStateTreeDocument ProjectCompiledTree(const AI::AIStateTree& tree,
                                            const std::string_view fallbackName)
{
    AI::AIStateTreeDocument document;
    document.Name = !tree.GetName().empty() ? tree.GetName() : std::string(fallbackName);

    std::vector<const AI::AIStateDefinition*> definitions;
    definitions.reserve(tree.GetStates().size());
    for (const auto& definition : tree.GetStates() | std::views::values)
        definitions.push_back(&definition);

    std::ranges::sort(definitions, {}, &AI::AIStateDefinition::Name);

    const AI::AIStateDefinition* entry = tree.FindState(tree.GetEntryState());
    document.EntryState = entry ? entry->Name : std::string();

    for (size_t index = 0; index < definitions.size(); ++index)
    {
        constexpr int columns = 4;
        constexpr float rowHeight = 180.0f;
        constexpr float columnWidth = 320.0f;
        
        const AI::AIStateDefinition& definition = *definitions[index];

        AI::AIDocumentState state;
        state.Name = definition.Name;
        state.EditorPosition = glm::vec2(
            static_cast<float>(index % columns) * columnWidth,
            static_cast<float>(index / columns) * rowHeight);

        for (const Ref<AI::IAITask>& task : definition.Tasks)
        {
            AI::AINodeDesc desc;
            desc.Type = task ? task->GetDebugType() : std::string("<null>");
            state.Tasks.push_back(std::move(desc));
        }

        for (const AI::AITransition& transition : definition.Transitions)
        {
            AI::AIDocumentTransition documentTransition;
            documentTransition.TargetState = StateNameForID(tree, transition.TargetState);
            documentTransition.Name = transition.DebugName;

            for (const Ref<AI::IAICondition>& condition : transition.Conditions)
            {
                AI::AINodeDesc desc;
                desc.Type = condition ? condition->GetDebugType() : std::string("<null>");
                documentTransition.Conditions.push_back(std::move(desc));
            }

            state.Transitions.push_back(std::move(documentTransition));
        }

        document.States.push_back(std::move(state));
    }

    return document;
}

std::string ResolveStateName(const AIEditorDocument& document, const uint64_t stateEditorID)
{
    const AIEditorStateRef* ref = document.FindState(stateEditorID);
    if (!ref)
        return {};

    const AI::AIDocumentState* state = document.ResolveState(*ref);
    return state ? state->Name : std::string();
}

uint64_t ResolveStateEditorID(const AIEditorDocument& document, const AI::StateID stateID)
{
    for (const AIEditorStateRef& ref : document.States)
    {
        const AI::AIDocumentState* state = document.ResolveState(ref);
        if (state && AI::HashState(state->Name) == stateID)
            return ref.EditorID;
    }
    return 0;
}

} // namespace Kerberos
