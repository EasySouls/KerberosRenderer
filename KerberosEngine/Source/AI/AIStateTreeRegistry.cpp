#include "AIStateTreeRegistry.hpp"

#include "AINodeFactory.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <cstdlib>
#include <ranges>
#include <string>
#include <unordered_map>

import Kerberos;

namespace Kerberos::AI {

namespace {

struct RegistryEntry
{
    Ref<AIStateTree> Tree;
    AIStateTreeOrigin Origin = AIStateTreeOrigin::BuiltIn;
};

std::unordered_map<std::string, RegistryEntry>& Trees()
{
    static std::unordered_map<std::string, RegistryEntry> trees;
    return trees;
}

std::string ToLower(std::string value)
{
    std::ranges::transform(value, value.begin(), [](const unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool TryParseInt(const std::string& text, std::int64_t& out)
{
    if (text.empty())
        return false;

    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto result = std::from_chars(begin, end, out);
    return result.ec == std::errc() && result.ptr == end;
}

bool TryParseFloat(const std::string& text, double& out)
{
    if (text.empty())
        return false;

    errno = 0;
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (end != text.c_str() + text.size() || errno == ERANGE)
        return false;

    out = value;
    return true;
}

bool ParseParam(const YAML::Node& node, AINodeParam& out, std::string& error)
{
    if (node.IsSequence())
    {
        if (node.size() != 3)
        {
            error = "Vector parameters must contain exactly three numbers.";
            return false;
        }

        glm::vec3 vector(0.0f);
        float* components[3] = { &vector.x, &vector.y, &vector.z };
        for (std::size_t i = 0; i < 3; ++i)
        {
            if (!node[i].IsScalar())
            {
                error = "Vector parameters must contain exactly three numbers.";
                return false;
            }

            double value = 0.0;
            if (!TryParseFloat(node[i].as<std::string>(), value))
            {
                error = "Vector parameters must contain exactly three numbers.";
                return false;
            }
            *components[i] = static_cast<float>(value);
        }

        out = vector;
        return true;
    }

    if (!node.IsScalar())
    {
        error = "Parameters must be scalars or three-component vectors.";
        return false;
    }

    const std::string text = node.as<std::string>();
    const std::string lower = ToLower(text);

    if (lower == "true")
    {
        out = true;
        return true;
    }

    if (lower == "false")
    {
        out = false;
        return true;
    }

    std::int64_t intValue = 0;
    if (TryParseInt(text, intValue))
    {
        out = intValue;
        return true;
    }

    double floatValue = 0.0;
    if (TryParseFloat(text, floatValue))
    {
        out = floatValue;
        return true;
    }

    out = text;
    return true;
}

bool ParseNode(const YAML::Node& node, AINodeDesc& out, std::string& error)
{
    if (!node.IsMap())
    {
        error = "Each task/condition must be a map with a 'Type' field.";
        return false;
    }

    const YAML::Node typeNode = node["Type"];
    if (!typeNode || !typeNode.IsScalar())
    {
        error = "Each task/condition must have a scalar 'Type' field.";
        return false;
    }

    out.Type = typeNode.as<std::string>();
    if (out.Type.empty())
    {
        error = "Task/condition 'Type' must not be empty.";
        return false;
    }

    const YAML::Node paramsNode = node["Params"];
    if (paramsNode && paramsNode.IsMap())
    {
        for (const auto& param : paramsNode)
        {
            const std::string key = param.first.as<std::string>();
            AINodeParam value;
            if (!ParseParam(param.second, value, error))
            {
                error = "Parameter '" + key + "': " + error;
                return false;
            }
            out.Params.emplace(key, std::move(value));
        }
    }

    return true;
}

void SetError(std::string* outError, const std::string& error)
{
    if (outError)
        *outError = error;
}

}

bool AIStateTreeRegistry::Register(std::string name, Ref<AIStateTree> tree, const AIStateTreeOrigin origin)
{
    if (name.empty() || !tree)
    {
        Log::CoreError("Cannot register an AI state tree with an empty name or null tree.");
        return false;
    }

    Trees()[std::move(name)] = RegistryEntry{ .Tree = std::move(tree), .Origin = origin };
    return true;
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

    YAML::Node documentNode;
    try
    {
        documentNode = YAML::Load(std::string(json));
    }
    catch (const YAML::Exception& exception)
    {
        error = "Failed to parse state tree '" + name + "': " + exception.what();
        SetError(outError, error);
        Log::CoreError("{}", error);
        return false;
    }
    catch (const std::exception& exception)
    {
        error = "Failed to parse state tree '" + name + "': " + exception.what();
        SetError(outError, error);
        Log::CoreError("{}", error);
        return false;
    }

    try
    {
        if (!documentNode.IsMap())
        {
            error = "State tree '" + name + "' must be a map.";
            SetError(outError, error);
            Log::CoreError("{}", error);
            return false;
        }

        const YAML::Node entryNode = documentNode["Entry"];
        const YAML::Node statesNode = documentNode["States"];

        if (!entryNode || !entryNode.IsScalar())
        {
            error = "State tree '" + name + "' must have a scalar 'Entry' field.";
            SetError(outError, error);
            Log::CoreError("{}", error);
            return false;
        }

        if (!statesNode || !statesNode.IsSequence() || statesNode.size() == 0)
        {
            error = "State tree '" + name + "' must have a non-empty 'States' sequence.";
            SetError(outError, error);
            Log::CoreError("{}", error);
            return false;
        }

        Ref<AIStateTree> tree = CreateRef<AIStateTree>(name);
        std::unordered_map<std::string, StateID> idsByName;
        std::unordered_map<StateID, AIStateDefinition*> definitions;

        for (const auto& stateNode : statesNode)
        {
            if (!stateNode.IsMap())
            {
                error = "Every state must be a map.";
                SetError(outError, error);
                Log::CoreError("State tree '{}': {}", name, error);
                return false;
            }

            const std::string stateName = stateNode["Name"].as<std::string>("");
            if (stateName.empty())
            {
                error = "Every state must have a non-empty 'Name'.";
                SetError(outError, error);
                Log::CoreError("State tree '{}': {}", name, error);
                return false;
            }

            if (idsByName.contains(stateName))
            {
                error = "Duplicate state '" + stateName + "'.";
                SetError(outError, error);
                Log::CoreError("State tree '{}': {}", name, error);
                return false;
            }

            const StateID id = HashState(stateName);
            if (tree->HasState(id))
            {
                error = "State hash collision for '" + stateName + "'.";
                SetError(outError, error);
                Log::CoreError("State tree '{}': {}", name, error);
                return false;
            }

            AIStateDefinition& definition = tree->AddState(id, stateName);
            idsByName.emplace(stateName, id);
            definitions.emplace(id, &definition);
        }

        for (const auto& stateNode : statesNode)
        {
            const std::string stateName = stateNode["Name"].as<std::string>();
            const StateID stateID = idsByName.at(stateName);
            AIStateDefinition& definition = *definitions.at(stateID);

            const YAML::Node tasksNode = stateNode["Tasks"];
            if (tasksNode && tasksNode.IsSequence())
            {
                for (const auto& taskNode : tasksNode)
                {
                    AINodeDesc desc;
                    if (!ParseNode(taskNode, desc, error))
                    {
                        SetError(outError, error);
                        Log::CoreError("State tree '{}', state '{}': {}", name, stateName, error);
                        return false;
                    }

                    Ref<IAITask> task = CreateTask(desc);
                    if (!task)
                    {
                        error = "Failed to create task '" + desc.Type + "'.";
                        SetError(outError, error);
                        Log::CoreError("State tree '{}', state '{}': {}", name, stateName, error);
                        return false;
                    }
                    definition.Tasks.push_back(std::move(task));
                }
            }

            const YAML::Node transitionsNode = stateNode["Transitions"];
            if (transitionsNode && transitionsNode.IsSequence())
            {
                for (const auto& transitionNode : transitionsNode)
                {
                    if (!transitionNode.IsMap())
                    {
                        error = "Every transition must be a map.";
                        SetError(outError, error);
                        Log::CoreError("State tree '{}', state '{}': {}", name, stateName, error);
                        return false;
                    }

                    const std::string targetName = transitionNode["To"].as<std::string>("");
                    if (targetName.empty() || !idsByName.contains(targetName))
                    {
                        error = "Transition target '" + targetName + "' does not exist.";
                        SetError(outError, error);
                        Log::CoreError("State tree '{}', state '{}': {}", name, stateName, error);
                        return false;
                    }

                    AITransition transition;
                    transition.TargetState = idsByName.at(targetName);

                    const YAML::Node nameNode = transitionNode["Name"];
                    transition.DebugName = nameNode && nameNode.IsScalar()
                        ? nameNode.as<std::string>()
                        : stateName + " -> " + targetName;

                    const YAML::Node whenNode = transitionNode["When"];
                    if (whenNode && whenNode.IsSequence())
                    {
                        for (const auto& conditionNode : whenNode)
                        {
                            AINodeDesc desc;
                            if (!ParseNode(conditionNode, desc, error))
                            {
                                SetError(outError, error);
                                Log::CoreError("State tree '{}', state '{}': {}", name, stateName, error);
                                return false;
                            }

                            Ref<IAICondition> condition = CreateCondition(desc);
                            if (!condition)
                            {
                                error = "Failed to create condition '" + desc.Type + "'.";
                                SetError(outError, error);
                                Log::CoreError("State tree '{}', state '{}': {}", name, stateName, error);
                                return false;
                            }
                            transition.Conditions.push_back(std::move(condition));
                        }
                    }

                    definition.Transitions.push_back(std::move(transition));
                }
            }
        }

        const std::string entryName = entryNode.as<std::string>();
        if (!idsByName.contains(entryName))
        {
            error = "Entry state '" + entryName + "' does not exist.";
            SetError(outError, error);
            Log::CoreError("State tree '{}': {}", name, error);
            return false;
        }

        tree->SetEntryState(idsByName.at(entryName));
        return Register(std::move(name), std::move(tree), origin);
    }
    catch (const YAML::Exception& exception)
    {
        error = "Failed to parse state tree '" + name + "': " + exception.what();
        SetError(outError, error);
        Log::CoreError("{}", error);
        return false;
    }
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

void AIStateTreeRegistry::ClearAll()
{
    Trees().clear();
}

}
