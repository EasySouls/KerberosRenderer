#include "AIEditorValidator.hpp"

#include "AI/AIIdentifier.hpp"
#include "AI/AINodeFactory.hpp"
#include "AI/AIStateTreeRegistry.hpp"

#include <algorithm>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

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

uint64_t FindTransitionEditorID(const AIEditorDocument& document,
                                const size_t sourceStateIndex,
                                const size_t transitionIndex)
{
    for (const AIEditorTransitionRef& ref : document.Transitions)
    {
        if (ref.SourceStateIndex == sourceStateIndex && ref.TransitionIndex == transitionIndex)
            return ref.EditorID;
    }
    return 0;
}

void AddIssue(AIEditorValidationResult& result,
              const AIEditorIssueSeverity severity,
              std::string message,
              std::string stateName = {},
              const int32_t transitionIndex = -1,
              const uint64_t stateEditorID = 0,
              const uint64_t transitionEditorID = 0)
{
    result.Issues.push_back(AIEditorIssue{
        .Severity = severity,
        .Message = std::move(message),
        .StateName = std::move(stateName),
        .TransitionIndex = transitionIndex,
        .StateEditorID = stateEditorID,
        .TransitionEditorID = transitionEditorID
    });
}

const AI::AINodeParamDef* FindParamDef(const AI::AINodeTypeInfo& info, const std::string_view name)
{
    const auto it = std::ranges::find(info.Params, name, &AI::AINodeParamDef::Name);
    return it != info.Params.end() ? &*it : nullptr;
}

bool ParamMatchesType(const AI::AINodeParam& param, const AI::AINodeParamType type)
{
    switch (type)
    {
    case AI::AINodeParamType::Bool:
        return std::holds_alternative<bool>(param);
    case AI::AINodeParamType::Int:
        return std::holds_alternative<std::int64_t>(param);
    case AI::AINodeParamType::Float:
        return std::holds_alternative<double>(param);
    case AI::AINodeParamType::String:
        return std::holds_alternative<std::string>(param);
    case AI::AINodeParamType::Vec3:
        return std::holds_alternative<glm::vec3>(param);
    }
    return false;
}

void ValidateNode(AIEditorValidationResult& result,
                  const AI::AINodeDesc& node,
                  const AI::AINodeKind kind,
                  const std::string& stateName,
                  const int32_t transitionIndex,
                  const uint64_t stateEditorID,
                  const uint64_t transitionEditorID,
                  const char* nodeLabel)
{
    if (node.Type.empty())
    {
        AddIssue(result, AIEditorIssueSeverity::Error,
                 std::string(nodeLabel) + " has an empty type.",
                 stateName, transitionIndex, stateEditorID, transitionEditorID);
        return;
    }

    const std::optional<AI::AINodeTypeInfo> info = AI::FindAINodeType(node.Type, kind);
    if (!info)
    {
        AddIssue(result, AIEditorIssueSeverity::Error,
                 std::string("Unknown ") + nodeLabel + " type '" + node.Type + "'.",
                 stateName, transitionIndex, stateEditorID, transitionEditorID);
        return;
    }

    for (const auto& [parameterName, parameterValue] : node.Params)
    {
        const AI::AINodeParamDef* definition = FindParamDef(*info, parameterName);
        if (!definition)
        {
            AddIssue(result, AIEditorIssueSeverity::Warning,
                     "Unknown parameter '" + parameterName + "' on " + node.Type + ".",
                     stateName, transitionIndex, stateEditorID, transitionEditorID);
            continue;
        }

        if (!ParamMatchesType(parameterValue, definition->Type))
        {
            AddIssue(result, AIEditorIssueSeverity::Error,
                     "Parameter '" + parameterName + "' on " + node.Type +
                         " has the wrong value type.",
                     stateName, transitionIndex, stateEditorID, transitionEditorID);
        }
    }
}

} // namespace

bool AIEditorValidationResult::HasErrors() const
{
    return std::ranges::any_of(Issues, [](const AIEditorIssue& issue) {
        return issue.Severity == AIEditorIssueSeverity::Error;
    });
}

AIEditorValidationResult AIEditorValidator::Validate(const AIEditorDocument& document)
{
    AIEditorValidationResult result;

    if (document.Document.Name.empty())
    {
        AddIssue(result, AIEditorIssueSeverity::Error,
                 "The state tree must have a non-empty name.");
    }

    if (document.Document.States.empty())
    {
        AddIssue(result, AIEditorIssueSeverity::Error,
                 "The state tree must contain at least one state.");
        return result;
    }

    std::unordered_map<std::string, size_t> stateIndices;
    stateIndices.reserve(document.Document.States.size());

    std::unordered_map<AI::StateID, std::string> stateIDOwners;
    stateIDOwners.reserve(document.Document.States.size());

    // Collect every state name before validating transitions so forward and
    // backward edges are both resolved correctly.
    for (size_t stateIndex = 0; stateIndex < document.Document.States.size(); ++stateIndex)
    {
        const AI::AIDocumentState& state = document.Document.States[stateIndex];
        const uint64_t stateEditorID = FindStateEditorID(document, stateIndex);

        if (state.Name.empty())
        {
            AddIssue(result, AIEditorIssueSeverity::Error,
                     "Every state must have a non-empty name.",
                     {}, -1, stateEditorID);
            continue;
        }

        if (!stateIndices.emplace(state.Name, stateIndex).second)
        {
            AddIssue(result, AIEditorIssueSeverity::Error,
                     "Duplicate state name '" + state.Name + "'.",
                     state.Name, -1, stateEditorID);
        }

        const AI::StateID stateID = AI::HashState(state.Name);
        if (const auto [it, inserted] = stateIDOwners.emplace(stateID, state.Name); !inserted &&
            it->second != state.Name)
        {
            AddIssue(result, AIEditorIssueSeverity::Error,
                     "State hash collision between '" + it->second + "' and '" + state.Name + "'.",
                     state.Name, -1, stateEditorID);
        }
    }

    for (size_t stateIndex = 0; stateIndex < document.Document.States.size(); ++stateIndex)
    {
        const AI::AIDocumentState& state = document.Document.States[stateIndex];
        const uint64_t stateEditorID = FindStateEditorID(document, stateIndex);

        if (state.Name.empty())
            continue;

        if (state.Tasks.empty())
        {
            AddIssue(result, AIEditorIssueSeverity::Warning,
                     "State '" + state.Name + "' has no tasks.",
                     state.Name, -1, stateEditorID);
        }

        for (const AI::AINodeDesc& task : state.Tasks)
        {
            ValidateNode(result, task, AI::AINodeKind::Task, state.Name, -1,
                         stateEditorID, 0, "task");
        }

        std::unordered_set<std::string> transitionNames;
        for (size_t transitionIndex = 0; transitionIndex < state.Transitions.size(); ++transitionIndex)
        {
            const AI::AIDocumentTransition& transition = state.Transitions[transitionIndex];
            const uint64_t transitionEditorID =
                FindTransitionEditorID(document, stateIndex, transitionIndex);
            const int32_t transitionIndexInt = static_cast<int32_t>(transitionIndex);

            if (transition.TargetState.empty())
            {
                AddIssue(result, AIEditorIssueSeverity::Error,
                         "A transition from '" + state.Name + "' has no target state.",
                         state.Name, transitionIndexInt, stateEditorID, transitionEditorID);
            }
            else if (!stateIndices.contains(transition.TargetState))
            {
                AddIssue(result, AIEditorIssueSeverity::Error,
                         "Transition target '" + transition.TargetState + "' does not exist.",
                         state.Name, transitionIndexInt, stateEditorID, transitionEditorID);
            }

            if (!transition.Name.empty() && !transitionNames.emplace(transition.Name).second)
            {
                AddIssue(result, AIEditorIssueSeverity::Warning,
                         "Duplicate transition name '" + transition.Name + "' on state '" +
                             state.Name + "'.",
                         state.Name, transitionIndexInt, stateEditorID, transitionEditorID);
            }

            for (const AI::AINodeDesc& condition : transition.Conditions)
            {
                ValidateNode(result, condition, AI::AINodeKind::Condition, state.Name,
                             transitionIndexInt, stateEditorID, transitionEditorID, "condition");
            }
        }
    }

    if (document.Document.EntryState.empty())
    {
        AddIssue(result, AIEditorIssueSeverity::Error,
                 "The state tree must have an entry state.");
    }
    else if (!stateIndices.contains(document.Document.EntryState))
    {
        AddIssue(result, AIEditorIssueSeverity::Error,
                 "Entry state '" + document.Document.EntryState + "' does not exist.",
                 document.Document.EntryState);
    }

    // Runtime parity check. Structural issues above give graph locations. 
    // This catches anything the compiler rejects that the editor did not model.
    std::string compileError;
    if (!AI::AIStateTreeRegistry::CompileDocument(document.Document, &compileError))
    {
        AddIssue(result, AIEditorIssueSeverity::Error,
                 "Runtime compile failed: " + compileError);
    }

    // Reachability from the entry state. This is a warning, disconnected states
    // are still valid documents.
    if (!document.Document.EntryState.empty() && stateIndices.contains(document.Document.EntryState))
    {
        std::unordered_set<std::string> visited;
        std::vector<std::string> pending{document.Document.EntryState};
        visited.insert(document.Document.EntryState);

        while (!pending.empty())
        {
            const std::string current = std::move(pending.back());
            pending.pop_back();

            const size_t stateIndex = stateIndices.at(current);
            for (const AI::AIDocumentTransition& transition : document.Document.States[stateIndex].Transitions)
            {
                if (!transition.TargetState.empty() &&
                    stateIndices.contains(transition.TargetState) &&
                    visited.insert(transition.TargetState).second)
                {
                    pending.push_back(transition.TargetState);
                }
            }
        }

        for (const auto& [stateName, stateIndex] : stateIndices)
        {
            if (!visited.contains(stateName))
            {
                AddIssue(result, AIEditorIssueSeverity::Warning,
                         "State '" + stateName + "' is unreachable from the entry state.",
                         stateName, -1, FindStateEditorID(document, stateIndex));
            }
        }
    }

    return result;
}

} // namespace Kerberos
