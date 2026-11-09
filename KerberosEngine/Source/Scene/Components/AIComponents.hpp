#pragma once

#include "AI/AIStateTree.hpp"
#include "AI/AIKeys.hpp"

#include <glm/glm.hpp>

#include <vector>
#include <variant>

namespace Kerberos {

using BlackboardValue = std::variant<bool, std::int64_t, float, glm::vec3, UUID, std::string>;

struct NamedBlackboardValue
{
    std::string Key;
    BlackboardValue Value;
};

struct AIBlackboardComponent
{
    // Runtime, per-entity blackboard values. The AI system will read/write these values during execution.
    std::unordered_map<AI::BlackboardKey, BlackboardValue> Values;
    /// Serializable, name + value
    std::vector<NamedBlackboardValue> AuthoredValues;
    /// Transient: authored defaults have been seeded into Values for this run.
    bool AuthoredValuesSeeded = false;

    template <typename T>
    void Set(const AI::BlackboardKey key, T value)
    {
        Values[key] = std::move(value);
    }

    template <typename T>
    [[nodiscard]] const T* Get(const AI::BlackboardKey key) const
    {
        const auto it = Values.find(key);
        if (it != Values.end())
        {
            return std::get_if<T>(&it->second);
        }
        return nullptr;
    }

    template <typename T>
    bool TryGet(const AI::BlackboardKey key, T& out) const
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

    bool Has(const AI::BlackboardKey key) const
    {
        return Values.contains(key);
    }

    void Remove(const AI::BlackboardKey key)
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
    std::string TreeName;
    // Shared tree across entities
    Ref<AI::AIStateTree> Tree = nullptr;

    AI::StateID CurrentState = AI::InvalidState;
    AI::StateID PreviousState = AI::InvalidState;
    // State requested by scripts
    AI::StateID PendingState = AI::InvalidState;

    float TimeInState = 0.0f;
    bool IsInitialized = false;
    bool ResolutionFailed = false;
    AI::TaskStatus LastTaskStatus = AI::TaskStatus::Running;

    // Transient, per-entity node instances.
    std::unordered_map<AI::StateID, std::vector<Owner<AI::IAITask>>> TaskInstances;

    AIStateMachineComponent() = default;
    ~AIStateMachineComponent() = default;

    // Shares tree, drops instances
    AIStateMachineComponent(const AIStateMachineComponent& other)
        : TreeName(other.TreeName), Tree(other.Tree)
    {
    }

    AIStateMachineComponent(AIStateMachineComponent&& other) noexcept
    {
        TreeName = std::move(other.TreeName);
        Tree = std::move(other.Tree);
        TaskInstances = std::move(other.TaskInstances);
        CurrentState = other.CurrentState;
        PreviousState = other.PreviousState;
        TimeInState = other.TimeInState;
        IsInitialized = other.IsInitialized;
        LastTaskStatus = other.LastTaskStatus;
        other.CurrentState = AI::InvalidState;
        other.PreviousState = AI::InvalidState;
        other.TimeInState = 0.0f;
        other.IsInitialized = false;
        other.LastTaskStatus = AI::TaskStatus::Running;
    }

    AIStateMachineComponent& operator=(const AIStateMachineComponent& other)
    {
        if (this != &other)
        {
            TreeName = other.TreeName;
            Tree = other.Tree;
            CurrentState = AI::InvalidState;
            PreviousState = AI::InvalidState;
            PendingState = AI::InvalidState;
            TimeInState = 0.0f;
            IsInitialized = false;
            ResolutionFailed = false;
            LastTaskStatus = AI::TaskStatus::Running;
            TaskInstances.clear();
        }
        return *this;
    }

    AIStateMachineComponent& operator=(AIStateMachineComponent&& other) noexcept
    {
        if (this != &other)
        {
            TreeName = std::move(other.TreeName);
            Tree = std::move(other.Tree);
            TaskInstances = std::move(other.TaskInstances);
            CurrentState = other.CurrentState;
            PreviousState = other.PreviousState;
            TimeInState = other.TimeInState;
            IsInitialized = other.IsInitialized;
            LastTaskStatus = other.LastTaskStatus;
            other.CurrentState = AI::InvalidState;
            other.PreviousState = AI::InvalidState;
            other.TimeInState = 0.0f;
            other.IsInitialized = false;
            other.LastTaskStatus = AI::TaskStatus::Running;
        }
        return *this;
    }
};

struct AIControllerComponent
{
    bool Enabled = true;
    glm::vec3 DesiredVelocity = glm::vec3(0.0f);
    glm::vec3 LookAtTarget = glm::vec3(0.0f);
    bool HasLookAt = false;
    bool WantsToAttack = false;

    // Transient attributes
    bool WarnedDynamicBody = false;
};

struct AIPerceptionComponent
{
    AI::BlackboardKey TargetKey = AI::TargetEntity;
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