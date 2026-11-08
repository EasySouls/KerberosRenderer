#pragma once

#include "AI/AIStateTree.hpp"
#include "AI/AIKeys.hpp"

#include <glm/glm.hpp>

#include <vector>
#include <variant>

namespace Kerberos {

using namespace Kerberos::AI;

using BlackboardValue = std::variant<bool, std::int64_t, float, glm::vec3, UUID, std::string>;

struct AIBlackboardComponent
{
    // Runtime, per-entity blackboard values. The AI system will read/write these values during execution.
    std::unordered_map<BlackboardKey, BlackboardValue> Values;
    /// Serializable, name + value
    //std::vector<NamedBlackboardValue> AuthoredValues;

    template <typename T>
    void Set(const BlackboardKey key, T value)
    {
        Values[key] = std::move(value);
    }

    template <typename T>
    [[nodiscard]] const T* Get(const BlackboardKey key) const
    {
        const auto it = Values.find(key);
        if (it != Values.end())
        {
            return std::get_if<T>(&it->second);
        }
        return nullptr;
    }

    template <typename T>
    bool TryGet(const BlackboardKey key, T& out) const
    {
        const auto it = Values.find(key);
        if (it != Values.end())
        {
            if (const T* value = std::get_if<T>(&it->second))
            {
                out = *value;
                return true;
            }
        }
        return false;
    }

    bool Has(const BlackboardKey key) const
    {
        return Values.contains(key);
    }

    void Remove(const BlackboardKey key)
    {
        Values.erase(key);
    }

    void Clear()
    {
        Values.clear();
    }
};

struct AIStateMachineComponent
{
    // Shared tree across entities
    Ref<AIStateTree> Tree = nullptr; 
    StateID CurrentState = InvalidState;
    StateID PreviousState = InvalidState;
    float TimeInState = 0.0f;
    bool IsInitialized = false;
    TaskStatus LastTaskStatus = TaskStatus::Running;

    // Transient, per-entity node instances.
    std::unordered_map<StateID, std::vector<Owner<IAITask>>> TaskInstances;

    AIStateMachineComponent() = default;
    ~AIStateMachineComponent() = default;

    AIStateMachineComponent(const AIStateMachineComponent& other)
    {
        // Shares Tree, drops instances
        Tree = other.Tree;
        TaskInstances.clear();
    }

    AIStateMachineComponent(AIStateMachineComponent&& other) noexcept
    {
        Tree = std::move(other.Tree);
        TaskInstances = std::move(other.TaskInstances);
        CurrentState = other.CurrentState;
        PreviousState = other.PreviousState;
        TimeInState = other.TimeInState;
        IsInitialized = other.IsInitialized;
        LastTaskStatus = other.LastTaskStatus;
        other.CurrentState = InvalidState;
        other.PreviousState = InvalidState;
        other.TimeInState = 0.0f;
        other.IsInitialized = false;
        other.LastTaskStatus = TaskStatus::Running;
    }

    AIStateMachineComponent& operator=(const AIStateMachineComponent& other)
    {
        if (this != &other)
        {
            Tree = other.Tree;
            TaskInstances.clear();
            CurrentState = other.CurrentState;
            PreviousState = other.PreviousState;
            TimeInState = other.TimeInState;
            IsInitialized = other.IsInitialized;
            LastTaskStatus = other.LastTaskStatus;
        }
        return *this;
    }

    AIStateMachineComponent& operator=(AIStateMachineComponent&& other) noexcept
    {
        if (this != &other)
        {
            Tree = std::move(other.Tree);
            TaskInstances = std::move(other.TaskInstances);
            CurrentState = other.CurrentState;
            PreviousState = other.PreviousState;
            TimeInState = other.TimeInState;
            IsInitialized = other.IsInitialized;
            LastTaskStatus = other.LastTaskStatus;
            other.CurrentState = InvalidState;
            other.PreviousState = InvalidState;
            other.TimeInState = 0.0f;
            other.IsInitialized = false;
            other.LastTaskStatus = TaskStatus::Running;
        }
        return *this;
    }
};

struct AIControllerComponent
{
    glm::vec3 DesiredVelocity = glm::vec3(0.0f);
    glm::vec3 LookAtTarget = glm::vec3(0.0f);
    bool HasLookAt = false;
    bool WantsToAttack = false;
};

struct AIPerceptionComponent
{
    BlackboardKey TargetKey = AI::TargetEntity;
    float SightRadius = 15.0f;
    float LoseRadius = 20.0f;
    float FieldOfViewDegrees = 120.0f;
    float EyeHeight = 1.5f;
    bool RequireLineOfSight = true;
    float PerceptionInterval = 0.2f;
    float TimeSincePerception = 0.0f;
    std::uint32_t FactionId = 0;
};

struct AITargetableComponent
{
    std::uint32_t FactionId = 1;
};

}