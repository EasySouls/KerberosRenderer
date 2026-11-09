#pragma once 

#include "AIStateTree.hpp"

#include <glm/glm.hpp>

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <cstdint>

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

using AITaskFactory = std::function<Ref<IAITask>(const AINodeDesc&)>;
using AIConditionFactory = std::function<Ref<IAICondition>(const AINodeDesc&)>;

void RegisterAITaskType(std::string_view type, AITaskFactory factory);
void RegisterAIConditionType(std::string_view type, AIConditionFactory factory);

/// Creates a node from a parsed descriptor. Returns nullptr and logs when the type is unknown
/// or its parameters are invalid.
Ref<IAITask> CreateTask(const AINodeDesc& desc);
Ref<IAICondition> CreateCondition(const AINodeDesc& desc);

// std::call_once, called by AISystem ctor and the registry
void RegisterStandardAINodes();

}