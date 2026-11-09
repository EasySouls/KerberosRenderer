#include "AIStateTreeRegistry.hpp"

#include "AIStateTreeSerializer.hpp"
#include "AINodeFactory.hpp"

#include <algorithm>
#include <optional>
#include <ranges>
#include <string>
#include <unordered_map>
#include <utility>

import Kerberos;

namespace Kerberos::AI {

namespace {

struct RegistryEntry
{
    Ref<AIStateTree> Tree;
    AIStateTreeOrigin Origin = AIStateTreeOrigin::BuiltIn;
    std::optional<AIStateTreeDocument> Document;
};

std::unordered_map<std::string, RegistryEntry>& Trees()
{
    static std::unordered_map<std::string, RegistryEntry> trees;
    return trees;
}

void SetError(std::string* outError, const std::string& error)
{
    if (outError)
        *outError = error;
}

bool Store(std::string name,
           Ref<AIStateTree> tree,
           const AIStateTreeOrigin origin,
           std::optional<AIStateTreeDocument> document)
{
    if (name.empty() || !tree)
    {
        Log::CoreError("Cannot register an AI state tree with an empty name or null tree.");
        return false;
    }

    Trees()[std::move(name)] = RegistryEntry{
        .Tree = std::move(tree),
        .Origin = origin,
        .Document = std::move(document)
    };
    return true;
}

} // namespace

bool AIStateTreeRegistry::Register(std::string name, Ref<AIStateTree> tree, const AIStateTreeOrigin origin)
{
    return Store(std::move(name), std::move(tree), origin, std::nullopt);
}

bool AIStateTreeRegistry::RegisterDocument(std::string name,
                                           Ref<AIStateTree> tree,
                                           const AIStateTreeOrigin origin,
                                           AIStateTreeDocument document)
{
    // Keep the stored document's name aligned with its registry key so editing
    // and saving it back cannot silently rename the tree.
    document.Name = name;
    return Store(std::move(name), std::move(tree), origin, std::move(document));
}

bool AIStateTreeRegistry::RegisterFromDocument(const std::string& name,
                                               const std::string_view json,
                                               const AIStateTreeOrigin origin,
                                               std::string* outError)
{
    RegisterStandardAINodes();

    std::string error;
    if (name.empty())
    {
        error = "State tree name must not be empty.";
        SetError(outError, error);
        Log::CoreError("{}", error);
        return false;
    }

    AIStateTreeDocument document;
    if (!AIStateTreeSerializer::Parse(json, document, &error))
    {
        error = "Failed to parse state tree '" + name + "': " + error;
        SetError(outError, error);
        Log::CoreError("{}", error);
        return false;
    }

    // The registry key is authoritative for C++/C# registration. Keep the
    // document name in sync so saving it back produces the same named tree.
    document.Name = name;

    Ref<AIStateTree> tree = CompileDocument(document, &error);
    if (!tree)
    {
        error = "Failed to compile state tree '" + name + "': " + error;
        SetError(outError, error);
        Log::CoreError("{}", error);
        return false;
    }

    return RegisterDocument(name, std::move(tree), origin, std::move(document));
}

Ref<AIStateTree> AIStateTreeRegistry::CompileDocument(const AIStateTreeDocument& document,
                                                      std::string* outError)
{
    std::string error;
    if (document.Name.empty())
    {
        error = "State tree name must not be empty.";
        SetError(outError, error);
        return nullptr;
    }

    if (document.States.empty())
    {
        error = "State tree '" + document.Name + "' must contain at least one state.";
        SetError(outError, error);
        return nullptr;
    }

    if (document.EntryState.empty())
    {
        error = "State tree '" + document.Name + "' must have a non-empty entry state.";
        SetError(outError, error);
        return nullptr;
    }

    Ref<AIStateTree> tree = CreateRef<AIStateTree>(document.Name);
    std::unordered_map<std::string, StateID> idsByName;
    std::unordered_map<StateID, AIStateDefinition*> definitions;

    for (const AIDocumentState& state : document.States)
    {
        if (state.Name.empty())
        {
            error = "State tree '" + document.Name + "' contains a state with an empty name.";
            SetError(outError, error);
            return nullptr;
        }

        if (idsByName.contains(state.Name))
        {
            error = "Duplicate state '" + state.Name + "'.";
            SetError(outError, error);
            return nullptr;
        }

        const StateID id = HashState(state.Name);
        if (tree->HasState(id))
        {
            error = "State hash collision for '" + state.Name + "'.";
            SetError(outError, error);
            return nullptr;
        }

        AIStateDefinition& definition = tree->AddState(id, state.Name);
        idsByName.emplace(state.Name, id);
        definitions.emplace(id, &definition);
    }

    for (const AIDocumentState& state : document.States)
    {
        const StateID stateID = idsByName.at(state.Name);
        AIStateDefinition& definition = *definitions.at(stateID);

        for (const AINodeDesc& desc : state.Tasks)
        {
            Ref<IAITask> task = CreateTask(desc);
            if (!task)
            {
                error = "State '" + state.Name + "': failed to create task '" + desc.Type + "'.";
                SetError(outError, error);
                return nullptr;
            }
            definition.Tasks.push_back(std::move(task));
        }

        for (const AIDocumentTransition& docTransition : state.Transitions)
        {
            if (!idsByName.contains(docTransition.TargetState))
            {
                error = "State '" + state.Name + "': transition target '" +
                    docTransition.TargetState + "' does not exist.";
                SetError(outError, error);
                return nullptr;
            }

            AITransition transition;
            transition.TargetState = idsByName.at(docTransition.TargetState);
            transition.DebugName = docTransition.Name.empty()
                ? state.Name + " -> " + docTransition.TargetState
                : docTransition.Name;

            for (const AINodeDesc& desc : docTransition.Conditions)
            {
                Ref<IAICondition> condition = CreateCondition(desc);
                if (!condition)
                {
                    error = "State '" + state.Name + "': failed to create condition '" +
                        desc.Type + "'.";
                    SetError(outError, error);
                    return nullptr;
                }
                transition.Conditions.push_back(std::move(condition));
            }

            definition.Transitions.push_back(std::move(transition));
        }
    }

    if (!idsByName.contains(document.EntryState))
    {
        error = "Entry state '" + document.EntryState + "' does not exist.";
        SetError(outError, error);
        return nullptr;
    }

    tree->SetEntryState(idsByName.at(document.EntryState));
    return tree;
}

bool AIStateTreeRegistry::Unregister(const std::string_view name)
{
    return Trees().erase(std::string(name)) > 0;
}

Ref<AIStateTree> AIStateTreeRegistry::Find(const std::string_view name)
{
    const auto it = Trees().find(std::string(name));
    return it != Trees().end() ? it->second.Tree : nullptr;
}

const AIStateTreeDocument* AIStateTreeRegistry::GetDocument(const std::string_view name)
{
    const auto it = Trees().find(std::string(name));
    if (it == Trees().end() || !it->second.Document)
        return nullptr;
    return &*it->second.Document;
}

std::optional<AIStateTreeOrigin> AIStateTreeRegistry::GetOrigin(const std::string_view name)
{
    const auto it = Trees().find(std::string(name));
    if (it == Trees().end())
        return std::nullopt;
    return it->second.Origin;
}

std::vector<std::string> AIStateTreeRegistry::GetNames()
{
    std::vector<std::string> names;
    names.reserve(Trees().size());
    for (const auto& name : Trees() | std::views::keys)
        names.push_back(name);

    std::ranges::sort(names);
    return names;
}

void AIStateTreeRegistry::ClearScriptTrees()
{
    std::erase_if(Trees(), [](const auto& entry) {
        return entry.second.Origin == AIStateTreeOrigin::Script;
    });
}

void AIStateTreeRegistry::ClearAssetTrees()
{
    std::erase_if(Trees(), [](const auto& entry) {
        return entry.second.Origin == AIStateTreeOrigin::Asset;
    });
}

void AIStateTreeRegistry::ClearAll()
{
    Trees().clear();
}

}
