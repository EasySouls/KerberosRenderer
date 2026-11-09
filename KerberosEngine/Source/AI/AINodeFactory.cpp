#include "AINodeFactory.hpp"

#include "AINodes.hpp"

#include <algorithm>
#include <mutex>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

import Kerberos;

namespace Kerberos::AI {

namespace {

struct TaskRegistration
{
    AITaskFactory Factory;
    AINodeTypeInfo Info;
};

struct ConditionRegistration
{
    AIConditionFactory Factory;
    AINodeTypeInfo Info;
};

std::unordered_map<std::string, TaskRegistration>& TaskRegistrations()
{
    static std::unordered_map<std::string, TaskRegistration> registrations;
    return registrations;
}

std::unordered_map<std::string, ConditionRegistration>& ConditionRegistrations()
{
    static std::unordered_map<std::string, ConditionRegistration> registrations;
    return registrations;
}

const AINodeParam* FindParam(const AINodeDesc& desc, const std::string_view key)
{
    const auto it = desc.Params.find(std::string(key));
    return it != desc.Params.end() ? &it->second : nullptr;
}

AINodeTypeInfo MakeInfo(const std::string_view type,
                        const AINodeKind kind,
                        std::vector<AINodeParamDef> params = {})
{
    return AINodeTypeInfo{
        .Type = std::string(type),
        .Kind = kind,
        .Params = std::move(params)
    };
}

} // namespace

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
    RegisterAITaskType(type, std::move(factory), MakeInfo(type, AINodeKind::Task));
}

void RegisterAITaskType(const std::string_view type, AITaskFactory factory, AINodeTypeInfo info)
{
    if (type.empty() || !factory)
    {
        Log::CoreError("Cannot register an AI task with an empty type or factory.");
        return;
    }

    info.Type = std::string(type);
    info.Kind = AINodeKind::Task;
    TaskRegistrations()[std::string(type)] = TaskRegistration{
        .Factory = std::move(factory),
        .Info = std::move(info)
    };
}

void RegisterAIConditionType(const std::string_view type, AIConditionFactory factory)
{
    RegisterAIConditionType(type, std::move(factory), MakeInfo(type, AINodeKind::Condition));
}

void RegisterAIConditionType(const std::string_view type, AIConditionFactory factory, AINodeTypeInfo info)
{
    if (type.empty() || !factory)
    {
        Log::CoreError("Cannot register an AI condition with an empty type or factory.");
        return;
    }

    info.Type = std::string(type);
    info.Kind = AINodeKind::Condition;
    ConditionRegistrations()[std::string(type)] = ConditionRegistration{
        .Factory = std::move(factory),
        .Info = std::move(info)
    };
}

std::optional<AINodeTypeInfo> FindAINodeType(const std::string_view type, const AINodeKind kind)
{
    RegisterStandardAINodes();

    if (kind == AINodeKind::Task)
    {
        const auto it = TaskRegistrations().find(std::string(type));
        if (it != TaskRegistrations().end())
            return it->second.Info;
    }
    else
    {
        const auto it = ConditionRegistrations().find(std::string(type));
        if (it != ConditionRegistrations().end())
            return it->second.Info;
    }

    return std::nullopt;
}

std::vector<AINodeTypeInfo> GetAINodeTypes(const AINodeKind kind)
{
    RegisterStandardAINodes();

    std::vector<AINodeTypeInfo> types;
    if (kind == AINodeKind::Task)
    {
        types.reserve(TaskRegistrations().size());
        for (const auto& registration : TaskRegistrations() | std::views::values)
            types.push_back(registration.Info);
    }
    else
    {
        types.reserve(ConditionRegistrations().size());
        for (const auto& registration : ConditionRegistrations() | std::views::values)
            types.push_back(registration.Info);
    }

    std::ranges::sort(types, {}, &AINodeTypeInfo::Type);
    return types;
}

Ref<IAITask> CreateTask(const AINodeDesc& desc)
{
    RegisterStandardAINodes();

    const auto it = TaskRegistrations().find(desc.Type);
    if (it == TaskRegistrations().end())
    {
        Log::CoreError("Unknown AI task type '{}'.", desc.Type);
        return nullptr;
    }

    Ref<IAITask> task = it->second.Factory(desc);
    if (!task)
    {
        Log::CoreError("Failed to create AI task '{}'.", desc.Type);
        return nullptr;
    }

    task->SetDebugType(desc.Type);
    return task;
}

Ref<IAICondition> CreateCondition(const AINodeDesc& desc)
{
    RegisterStandardAINodes();

    const auto it = ConditionRegistrations().find(desc.Type);
    if (it == ConditionRegistrations().end())
    {
        Log::CoreError("Unknown AI condition type '{}'.", desc.Type);
        return nullptr;
    }

    Ref<IAICondition> condition = it->second.Factory(desc);
    if (!condition)
    {
        Log::CoreError("Failed to create AI condition '{}'.", desc.Type);
        return nullptr;
    }

    condition->SetDebugType(desc.Type);
    return condition;
}

void RegisterStandardAINodes()
{
    static std::once_flag once;
    std::call_once(once, [] {
        RegisterAITaskType("ChaseTarget", [](const AINodeDesc& desc) -> Ref<IAITask> {
            const std::string targetKey = desc.GetString("TargetKey", "TargetEntity");
            return CreateRef<TaskChaseTarget>(HashKey(targetKey), desc.GetFloat("Speed", 4.0f));
        }, MakeInfo("ChaseTarget", AINodeKind::Task, {
            { .Name = "TargetKey", .Type = AINodeParamType::String, .Default = std::string("TargetEntity") },
            { .Name = "Speed", .Type = AINodeParamType::Float, .Default = 4.0 },
        }));

        RegisterAITaskType("FaceTarget", [](const AINodeDesc& desc) -> Ref<IAITask> {
            const std::string targetKey = desc.GetString("TargetKey", "TargetEntity");
            return CreateRef<TaskFaceTarget>(HashKey(targetKey));
        }, MakeInfo("FaceTarget", AINodeKind::Task, {
            { .Name = "TargetKey", .Type = AINodeParamType::String, .Default = std::string("TargetEntity") },
        }));

        RegisterAITaskType("Stop", [](const AINodeDesc&) -> Ref<IAITask> {
            return CreateRef<TaskStop>();
        }, MakeInfo("Stop", AINodeKind::Task));

        RegisterAITaskType("Wait", [](const AINodeDesc& desc) -> Ref<IAITask> {
            return CreateRef<TaskWait>(desc.GetFloat("Duration", 1.0f));
        }, MakeInfo("Wait", AINodeKind::Task, {
            { .Name = "Duration", .Type = AINodeParamType::Float, .Default = 1.0 },
        }));

        RegisterAITaskType("SetBlackboardBool", [](const AINodeDesc& desc) -> Ref<IAITask> {
            const std::string key = desc.GetString("Key");
            if (key.empty())
            {
                Log::CoreError("SetBlackboardBool requires a 'Key' parameter.");
                return nullptr;
            }
            return CreateRef<TaskSetBlackboardBool>(HashKey(key), desc.GetBool("Value"));
        }, MakeInfo("SetBlackboardBool", AINodeKind::Task, {
            { .Name = "Key", .Type = AINodeParamType::String, .Default = std::string() },
            { .Name = "Value", .Type = AINodeParamType::Bool, .Default = false },
        }));

        RegisterAITaskType("AttackTarget", [](const AINodeDesc&) -> Ref<IAITask> {
            return CreateRef<TaskAttackTarget>();
        }, MakeInfo("AttackTarget", AINodeKind::Task));

        RegisterAIConditionType("DistanceToTarget", [](const AINodeDesc& desc) -> Ref<IAICondition> {
            const std::string targetKey = desc.GetString("TargetKey", "TargetEntity");
            return CreateRef<ConditionDistanceToTarget>(
                HashKey(targetKey),
                desc.GetFloat("Distance", 10.0f),
                desc.GetBool("LessThan", true));
        }, MakeInfo("DistanceToTarget", AINodeKind::Condition, {
            { .Name = "TargetKey", .Type = AINodeParamType::String, .Default = std::string("TargetEntity") },
            { .Name = "Distance", .Type = AINodeParamType::Float, .Default = 10.0 },
            { .Name = "LessThan", .Type = AINodeParamType::Bool, .Default = true },
        }));

        RegisterAIConditionType("BlackboardBool", [](const AINodeDesc& desc) -> Ref<IAICondition> {
            const std::string key = desc.GetString("Key");
            if (key.empty())
            {
                Log::CoreError("BlackboardBool requires a 'Key' parameter.");
                return nullptr;
            }
            return CreateRef<ConditionBlackboardBool>(HashKey(key), desc.GetBool("Value"));
        }, MakeInfo("BlackboardBool", AINodeKind::Condition, {
            { .Name = "Key", .Type = AINodeParamType::String, .Default = std::string() },
            { .Name = "Value", .Type = AINodeParamType::Bool, .Default = false },
        }));

        RegisterAIConditionType("TimeInState", [](const AINodeDesc& desc) -> Ref<IAICondition> {
            return CreateRef<ConditionTimeInState>(
                desc.GetFloat("Time", 1.0f),
                desc.GetBool("LessThan", false));
        }, MakeInfo("TimeInState", AINodeKind::Condition, {
            { .Name = "Time", .Type = AINodeParamType::Float, .Default = 1.0 },
            { .Name = "LessThan", .Type = AINodeParamType::Bool, .Default = false },
        }));

        RegisterAIConditionType("TargetVisible", [](const AINodeDesc&) -> Ref<IAICondition> {
            return CreateRef<ConditionTargetVisible>();
        }, MakeInfo("TargetVisible", AINodeKind::Condition));

        RegisterAIConditionType("TargetExists", [](const AINodeDesc& desc) -> Ref<IAICondition> {
            const std::string targetKey = desc.GetString("TargetKey", "TargetEntity");
            return CreateRef<ConditionTargetExists>(HashKey(targetKey));
        }, MakeInfo("TargetExists", AINodeKind::Condition, {
            { .Name = "TargetKey", .Type = AINodeParamType::String, .Default = std::string("TargetEntity") },
        }));
    });
}

}
