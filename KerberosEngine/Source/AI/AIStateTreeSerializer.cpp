#include "AIStateTreeSerializer.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <ranges>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Kerberos::AI {

namespace {

void SetError(std::string* outError, const std::string& error)
{
    if (outError)
        *outError = error;
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
    if (!typeNode || !typeNode.IsScalar() || typeNode.IsNull())
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
        for (const auto param : paramsNode)
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

void NormalizeParam(AINodeParam& param, const AINodeParamType type)
{
    switch (type)
    {
    case AINodeParamType::Bool:
        if (const auto* intValue = std::get_if<std::int64_t>(&param))
            param = (*intValue != 0);
        else if (const auto* floatValue = std::get_if<double>(&param))
            param = (*floatValue != 0.0);
        break;

    case AINodeParamType::Int:
        if (const auto* floatValue = std::get_if<double>(&param))
            param = static_cast<std::int64_t>(*floatValue);
        else if (const auto* boolValue = std::get_if<bool>(&param))
            param = *boolValue ? std::int64_t{ 1 } : std::int64_t{ 0 };
        break;

    case AINodeParamType::Float:
        if (const auto* intValue = std::get_if<std::int64_t>(&param))
            param = static_cast<double>(*intValue);
        else if (const auto* boolValue = std::get_if<bool>(&param))
            param = *boolValue ? 1.0 : 0.0;
        break;

    case AINodeParamType::String:
    case AINodeParamType::Vec3:
        break;
    }
}

void NormalizeNodeParams(AINodeDesc& desc, const AINodeKind kind)
{
    const std::optional<AINodeTypeInfo> info = FindAINodeType(desc.Type, kind);
    if (!info)
        return;

    for (const AINodeParamDef& def : info->Params)
    {
        const auto it = desc.Params.find(def.Name);
        if (it != desc.Params.end())
            NormalizeParam(it->second, def.Type);
    }
}

bool ParseEditorPosition(const YAML::Node& node, glm::vec2& out)
{
    if (!node || !node.IsSequence() || node.size() != 2 ||
        !node[0].IsScalar() || !node[1].IsScalar())
    {
        return false;
    }

    double x = 0.0;
    double y = 0.0;
    if (!TryParseFloat(node[0].as<std::string>(), x) ||
        !TryParseFloat(node[1].as<std::string>(), y))
    {
        return false;
    }

    out = glm::vec2(static_cast<float>(x), static_cast<float>(y));
    return true;
}

bool ParseState(const YAML::Node& node, AIDocumentState& out, std::string& error)
{
    if (!node.IsMap())
    {
        error = "Every state must be a map.";
        return false;
    }

    const YAML::Node nameNode = node["Name"];
    if (!nameNode || !nameNode.IsScalar() || nameNode.IsNull())
    {
        error = "Every state must have a non-empty 'Name'.";
        return false;
    }

    out.Name = nameNode.as<std::string>();
    if (out.Name.empty())
    {
        error = "Every state must have a non-empty 'Name'.";
        return false;
    }

    // Layout is editor-only metadata. Invalid values are ignored rather than
    // rejecting an otherwise valid runtime document.
    ParseEditorPosition(node["EditorPosition"], out.EditorPosition);

    if (const YAML::Node tasksNode = node["Tasks"]; tasksNode && tasksNode.IsSequence())
    {
        out.Tasks.reserve(tasksNode.size());
        for (const auto taskNode : tasksNode)
        {
            AINodeDesc desc;
            if (!ParseNode(taskNode, desc, error))
                return false;
            NormalizeNodeParams(desc, AINodeKind::Task);
            out.Tasks.push_back(std::move(desc));
        }
    }

    if (const YAML::Node transitionsNode = node["Transitions"];
        transitionsNode && transitionsNode.IsSequence())
    {
        out.Transitions.reserve(transitionsNode.size());
        for (const auto transitionNode : transitionsNode)
        {
            if (!transitionNode.IsMap())
            {
                error = "Every transition must be a map.";
                return false;
            }

            AIDocumentTransition transition;
            const YAML::Node targetNode = transitionNode["To"];
            if (!targetNode || !targetNode.IsScalar() || targetNode.IsNull())
            {
                error = "Every transition must have a non-empty 'To' target.";
                return false;
            }

            transition.TargetState = targetNode.as<std::string>();
            if (transition.TargetState.empty())
            {
                error = "Every transition must have a non-empty 'To' target.";
                return false;
            }

            const YAML::Node transitionNameNode = transitionNode["Name"];
            if (transitionNameNode && transitionNameNode.IsScalar() && !transitionNameNode.IsNull())
                transition.Name = transitionNameNode.as<std::string>();

            if (const YAML::Node whenNode = transitionNode["When"];
                whenNode && whenNode.IsSequence())
            {
                transition.Conditions.reserve(whenNode.size());
                for (const auto conditionNode : whenNode)
                {
                    AINodeDesc desc;
                    if (!ParseNode(conditionNode, desc, error))
                        return false;
                    NormalizeNodeParams(desc, AINodeKind::Condition);
                    transition.Conditions.push_back(std::move(desc));
                }
            }

            out.Transitions.push_back(std::move(transition));
        }
    }

    return true;
}

void WriteParam(YAML::Emitter& out, const std::string& key, const AINodeParam& param)
{
    out << YAML::Key << key << YAML::Value;

    if (const auto* boolValue = std::get_if<bool>(&param))
        out << *boolValue;
    else if (const auto* intValue = std::get_if<std::int64_t>(&param))
        out << static_cast<long long>(*intValue);
    else if (const auto* doubleValue = std::get_if<double>(&param))
        out << *doubleValue;
    else if (const auto* stringValue = std::get_if<std::string>(&param))
        out << *stringValue;
    else if (const auto* vectorValue = std::get_if<glm::vec3>(&param))
    {
        out << YAML::Flow
            << YAML::BeginSeq << vectorValue->x << vectorValue->y << vectorValue->z << YAML::EndSeq
            << YAML::Block;
    }
}

void WriteNode(YAML::Emitter& out, const AINodeDesc& desc)
{
    out << YAML::BeginMap;
    out << YAML::Key << "Type" << YAML::Value << desc.Type;

    if (!desc.Params.empty())
    {
        std::vector<std::string> keys;
        keys.reserve(desc.Params.size());
        for (const auto& key : desc.Params | std::views::keys)
            keys.push_back(key);
        std::ranges::sort(keys);

        out << YAML::Key << "Params" << YAML::Value << YAML::BeginMap;
        for (const std::string& key : keys)
            WriteParam(out, key, desc.Params.at(key));
        out << YAML::EndMap;
    }

    out << YAML::EndMap;
}

} // namespace

bool AIStateTreeSerializer::Parse(const std::string_view document,
                                  AIStateTreeDocument& out,
                                  std::string* outError)
{
    out = {};

    YAML::Node root;
    try
    {
        root = YAML::Load(std::string(document));
    }
    catch (const YAML::Exception& exception)
    {
        SetError(outError, std::string("Failed to parse state tree document: ") + exception.what());
        return false;
    }
    catch (const std::exception& exception)
    {
        SetError(outError, std::string("Failed to parse state tree document: ") + exception.what());
        return false;
    }

    if (!root.IsMap())
    {
        SetError(outError, "State tree document must be a map.");
        return false;
    }

    const YAML::Node nameNode = root["Name"];
    if (nameNode && nameNode.IsScalar() && !nameNode.IsNull())
        out.Name = nameNode.as<std::string>();

    const YAML::Node entryNode = root["Entry"];
    if (!entryNode || !entryNode.IsScalar() || entryNode.IsNull())
    {
        SetError(outError, "State tree must have a scalar 'Entry' field.");
        return false;
    }

    out.EntryState = entryNode.as<std::string>();
    if (out.EntryState.empty())
    {
        SetError(outError, "State tree 'Entry' must not be empty.");
        return false;
    }

    const YAML::Node statesNode = root["States"];
    if (!statesNode || !statesNode.IsSequence() || statesNode.size() == 0)
    {
        SetError(outError, "State tree must have a non-empty 'States' sequence.");
        return false;
    }

    std::unordered_set<std::string> stateNames;
    stateNames.reserve(statesNode.size());
    out.States.reserve(statesNode.size());

    for (const auto stateNode : statesNode)
    {
        AIDocumentState state;
        std::string error;
        if (!ParseState(stateNode, state, error))
        {
            SetError(outError, error);
            return false;
        }

        if (!stateNames.emplace(state.Name).second)
        {
            SetError(outError, "Duplicate state '" + state.Name + "'.");
            return false;
        }

        out.States.push_back(std::move(state));
    }

    return true;
}

bool AIStateTreeSerializer::Serialize(const AIStateTreeDocument& document,
                                      std::string& outYaml,
                                      std::string* outError)
{
    outYaml.clear();

    YAML::Emitter out;
    out.SetIndent(2);

    out << YAML::BeginMap;
    out << YAML::Key << "Name" << YAML::Value << document.Name;
    out << YAML::Key << "Entry" << YAML::Value << document.EntryState;
    out << YAML::Key << "States" << YAML::Value << YAML::BeginSeq;

    for (const AIDocumentState& state : document.States)
    {
        out << YAML::BeginMap;
        out << YAML::Key << "Name" << YAML::Value << state.Name;

        if (state.EditorPosition.x != 0.0f || state.EditorPosition.y != 0.0f)
        {
            out << YAML::Key << "EditorPosition" << YAML::Value
                << YAML::Flow
                << YAML::BeginSeq << state.EditorPosition.x << state.EditorPosition.y << YAML::EndSeq
                << YAML::Block;
        }

        if (!state.Tasks.empty())
        {
            out << YAML::Key << "Tasks" << YAML::Value << YAML::BeginSeq;
            for (const AINodeDesc& task : state.Tasks)
                WriteNode(out, task);
            out << YAML::EndSeq;
        }

        if (!state.Transitions.empty())
        {
            out << YAML::Key << "Transitions" << YAML::Value << YAML::BeginSeq;
            for (const AIDocumentTransition& transition : state.Transitions)
            {
                out << YAML::BeginMap;
                out << YAML::Key << "To" << YAML::Value << transition.TargetState;
                if (!transition.Name.empty())
                    out << YAML::Key << "Name" << YAML::Value << transition.Name;

                if (!transition.Conditions.empty())
                {
                    out << YAML::Key << "When" << YAML::Value << YAML::BeginSeq;
                    for (const AINodeDesc& condition : transition.Conditions)
                        WriteNode(out, condition);
                    out << YAML::EndSeq;
                }

                out << YAML::EndMap;
            }
            out << YAML::EndSeq;
        }

        out << YAML::EndMap;
    }

    out << YAML::EndSeq;
    out << YAML::EndMap;

    if (!out.good())
    {
        SetError(outError, std::string("Failed to serialize state tree document: ") + out.GetLastError());
        return false;
    }

    outYaml = out.c_str();
    return true;
}

bool AIStateTreeSerializer::LoadFile(const std::filesystem::path& path,
                                     AIStateTreeDocument& out,
                                     std::string* outError)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        SetError(outError, "Failed to open state tree document: " + path.string());
        return false;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    if (!file.good() && !file.eof())
    {
        SetError(outError, "Failed to read state tree document: " + path.string());
        return false;
    }

    return Parse(buffer.str(), out, outError);
}

bool AIStateTreeSerializer::SaveFile(const std::filesystem::path& path,
                                     const AIStateTreeDocument& document,
                                     std::string* outError)
{
    std::string yaml;
    if (!Serialize(document, yaml, outError))
        return false;

    std::error_code ec;
    if (!path.parent_path().empty())
        std::filesystem::create_directories(path.parent_path(), ec);
    if (ec)
    {
        SetError(outError, "Failed to create state tree directory '" +
                               path.parent_path().string() + "': " + ec.message());
        return false;
    }

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file)
    {
        SetError(outError, "Failed to open state tree document for writing: " + path.string());
        return false;
    }

    file.write(yaml.data(), static_cast<std::streamsize>(yaml.size()));
    if (!file.good())
    {
        SetError(outError, "Failed to write state tree document: " + path.string());
        return false;
    }

    return true;
}

} // namespace Kerberos::AI
