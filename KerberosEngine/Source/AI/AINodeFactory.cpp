#include "AINodeFactory.hpp"

#include "AINodes.hpp"

#include <mutex>

import Kerberos;

namespace Kerberos::AI {

namespace {

std::unordered_map<std::string, AITaskFactory>& TaskFactories()
{
    static std::unordered_map<std::string, AITaskFactory> factories;
    return factories;
}

std::unordered_map<std::string, AIConditionFactory>& ConditionFactories()
{
    static std::unordered_map<std::string, AIConditionFactory> factories;
    return factories;
}

const AINodeParam* FindParam(const AINodeDesc& desc, const std::string_view key)
{
    const auto it = desc.Params.find(std::string(key));
    return it != desc.Params.end() ? &it->second : nullptr;
}

}

bool AINodeDesc::GetBool(const std::string_view key, const bool fallback) const
{
    const AINodeParam* param = FindParam(*this, key);
    if (!param)
        return fallback;

    if (const auto* value = std::get_if<bool>(param))
        return *value;
    if (const auto* value = std::get_if<std::int64_t>(param))
        return *value != 0;
    if (const auto* value = std::get_if<double>(param))
        return *value != 0.0;

    return fallback;
}

std::int64_t AINodeDesc::GetInt(const std::string_view key, const std::int64_t fallback) const
{
    const AINodeParam* param = FindParam(*this, key);
    if (!param)
        return fallback;

    if (const auto* value = std::get_if<std::int64_t>(param))
        return *value;
    if (const auto* value = std::get_if<bool>(param))
        return *value ? 1 : 0;
    if (const auto* value = std::get_if<double>(param))
        return static_cast<std::int64_t>(*value);

    return fallback;
}

float AINodeDesc::GetFloat(const std::string_view key, const float fallback) const
{
    const AINodeParam* param = FindParam(*this, key);
    if (!param)
        return fallback;

    if (const auto* value = std::get_if<double>(param))
        return static_cast<float>(*value);
    if (const auto* value = std::get_if<std::int64_t>(param))
        return static_cast<float>(*value);
    if (const auto* value = std::get_if<bool>(param))
        return *value ? 1.0f : 0.0f;

    return fallback;
}

std::string AINodeDesc::GetString(const std::string_view key, const std::string_view fallback) const
{
    const AINodeParam* param = FindParam(*this, key);
    if (!param)
        return std::string(fallback);

    if (const auto* value = std::get_if<std::string>(param))
        return *value;

    return std::string(fallback);
}

glm::vec3 AINodeDesc::GetVec3(const std::string_view key, const glm::vec3& fallback) const
{
    const AINodeParam* param = FindParam(*this, key);
    if (!param)
        return fallback;

    if (const auto* value = std::get_if<glm::vec3>(param))
        return *value;

    return fallback;
}

void RegisterAITaskType(const std::string_view type, AITaskFactory factory)
{
    if (type.empty() || !factory)
    {
        Log::CoreError("Cannot register an AI task with an empty type or factory.");
        return;
    }

    TaskFactories()[std::string(type)] = std::move(factory);
}

void RegisterAIConditionType(const std::string_view type, AIConditionFactory factory)
{
    if (type.empty() || !factory)
    {
        Log::CoreError("Cannot register an AI condition with an empty type or factory.");
        return;
    }

    ConditionFactories()[std::string(type)] = std::move(factory);
}

Ref<IAITask> CreateTask(const AINodeDesc& desc)
{
    RegisterStandardAINodes();

    const auto it = TaskFactories().find(desc.Type);
    if (it == TaskFactories().end())
    {
        Log::CoreError("Unknown AI task type '{}'.", desc.Type);
        return nullptr;
    }

    Ref<IAITask> task = it->second(desc);
    if (!task)
    {
        Log::CoreError("Failed to create AI task '{}'.", desc.Type);
    }
    return task;
}

Ref<IAICondition> CreateCondition(const AINodeDesc& desc)
{
    RegisterStandardAINodes();

    const auto it = ConditionFactories().find(desc.Type);
    if (it == ConditionFactories().end())
    {
        Log::CoreError("Unknown AI condition type '{}'.", desc.Type);
        return nullptr;
    }

    Ref<IAICondition> condition = it->second(desc);
    if (!condition)
    {
        Log::CoreError("Failed to create AI condition '{}'.", desc.Type);
    }
    return condition;
}

void RegisterStandardAINodes()
{
    static std::once_flag once;
    std::call_once(once, [] {
        RegisterAITaskType("ChaseTarget", [](const AINodeDesc& desc) -> Ref<IAITask> {
            const std::string targetKey = desc.GetString("TargetKey", "TargetEntity");
            return CreateRef<TaskChaseTarget>(HashKey(targetKey), desc.GetFloat("Speed", 4.0f));
        });

        RegisterAITaskType("FaceTarget", [](const AINodeDesc& desc) -> Ref<IAITask> {
            const std::string targetKey = desc.GetString("TargetKey", "TargetEntity");
            return CreateRef<TaskFaceTarget>(HashKey(targetKey));
        });

        RegisterAITaskType("Stop", [](const AINodeDesc&) -> Ref<IAITask> {
            return CreateRef<TaskStop>();
        });

        RegisterAITaskType("Wait", [](const AINodeDesc& desc) -> Ref<IAITask> {
            return CreateRef<TaskWait>(desc.GetFloat("Duration", 1.0f));
        });

        RegisterAITaskType("SetBlackboardBool", [](const AINodeDesc& desc) -> Ref<IAITask> {
            const std::string key = desc.GetString("Key");
            if (key.empty())
            {
                Log::CoreError("SetBlackboardBool requires a 'Key' parameter.");
                return nullptr;
            }
            return CreateRef<TaskSetBlackboardBool>(HashKey(key), desc.GetBool("Value"));
        });

        RegisterAITaskType("AttackTarget", [](const AINodeDesc&) -> Ref<IAITask> {
            return CreateRef<TaskAttackTarget>();
        });

        RegisterAIConditionType("DistanceToTarget", [](const AINodeDesc& desc) -> Ref<IAICondition> {
            const std::string targetKey = desc.GetString("TargetKey", "TargetEntity");
            return CreateRef<ConditionDistanceToTarget>(
                HashKey(targetKey),
                desc.GetFloat("Distance", 10.0f),
                desc.GetBool("LessThan", true));
        });

        RegisterAIConditionType("BlackboardBool", [](const AINodeDesc& desc) -> Ref<IAICondition> {
            const std::string key = desc.GetString("Key");
            if (key.empty())
            {
                Log::CoreError("BlackboardBool requires a 'Key' parameter.");
                return nullptr;
            }
            return CreateRef<ConditionBlackboardBool>(HashKey(key), desc.GetBool("Value"));
        });

        RegisterAIConditionType("TimeInState", [](const AINodeDesc& desc) -> Ref<IAICondition> {
            return CreateRef<ConditionTimeInState>(
                desc.GetFloat("Time", 1.0f),
                desc.GetBool("LessThan", false));
        });

        RegisterAIConditionType("TargetVisible", [](const AINodeDesc&) -> Ref<IAICondition> {
            return CreateRef<ConditionTargetVisible>();
        });

        RegisterAIConditionType("TargetExists", [](const AINodeDesc& desc) -> Ref<IAICondition> {
            const std::string targetKey = desc.GetString("TargetKey", "TargetEntity");
            return CreateRef<ConditionTargetExists>(HashKey(targetKey));
        });
    });
}

}
