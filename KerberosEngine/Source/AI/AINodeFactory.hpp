#pragma once 

#include "AIStateTree.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace Kerberos::AI {

enum class AINodeKind : std::uint8_t
{
    Task,
    Condition
};

using AINodeParam = std::variant<bool, std::int64_t, double, std::string, glm::vec3>;

struct AINodeDesc
{
    std::string Type;
    std::unordered_map<std::string, AINodeParam> Params;

    [[nodiscard]] bool GetBool(std::string_view key, bool fallback = false) const;
    [[nodiscard]] std::int64_t GetInt(std::string_view key, std::int64_t fallback = 0) const;
    [[nodiscard]] float GetFloat(std::string_view key, float fallback = 0.0f) const;
    [[nodiscard]] std::string GetString(std::string_view key, std::string_view fallback = {}) const;
    [[nodiscard]] glm::vec3 GetVec3(std::string_view key, const glm::vec3& fallback = glm::vec3(0.0f)) const;
};

/// Parameter types exposed to authoring tools. Float parameters are stored as
/// double in AINodeParam so the YAML round-trip preserves authored precision.
enum class AINodeParamType : std::uint8_t
{
    Bool,
    Int,
    Float,
    String,
    Vec3
};

struct AINodeParamDef
{
    std::string Name;
    AINodeParamType Type = AINodeParamType::Float;
    AINodeParam Default{};
};

/// Authoring metadata for a registered task or condition type. Runtime
/// behaviour still lives in the factory, this only describes the parameters.
struct AINodeTypeInfo
{
    std::string Type;
    AINodeKind Kind = AINodeKind::Task;
    std::vector<AINodeParamDef> Params;
};

using AITaskFactory = std::function<Ref<IAITask>(const AINodeDesc&)>;
using AIConditionFactory = std::function<Ref<IAICondition>(const AINodeDesc&)>;

void RegisterAITaskType(std::string_view type, AITaskFactory factory);
void RegisterAITaskType(std::string_view type, AITaskFactory factory, AINodeTypeInfo info);
void RegisterAIConditionType(std::string_view type, AIConditionFactory factory);
void RegisterAIConditionType(std::string_view type, AIConditionFactory factory, AINodeTypeInfo info);

/// Returns the registered authoring metadata, or nullopt for an unknown type.
[[nodiscard]] std::optional<AINodeTypeInfo> FindAINodeType(std::string_view type, AINodeKind kind);

/// Returns all registered authoring metadata of the given kind, sorted by type name.
[[nodiscard]] std::vector<AINodeTypeInfo> GetAINodeTypes(AINodeKind kind);

/// Creates a task node from a parsed descriptor. Returns nullptr and logs when the type is unknown
/// or its parameters are invalid.
Ref<IAITask> CreateTask(const AINodeDesc& desc);
/// Creates a condition node from a parsed descriptor. Returns nullptr and logs when the type is unknown
/// or its parameters are invalid.
Ref<IAICondition> CreateCondition(const AINodeDesc& desc);

// std::call_once, called by AISystem ctor and the registry
void RegisterStandardAINodes();

}
