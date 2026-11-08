#pragma once

#include "AIIdentifier.hpp"
#include "Core/UUID.hpp"
#include "Core/Core.hpp"

#include <string>
#include <vector>
#include <unordered_map>

namespace Kerberos {
class Scene;
}

namespace Kerberos::AI {

struct AIContext
{
    Scene* World = nullptr;
    UUID Entity = UUID::Invalid();
    float DeltaTime = 0.0f;
};

enum class TaskStatus : std::uint8_t
{
    Running,
    Succeeded,
    Failed,
};

class IAICondition
{
public:
    virtual ~IAICondition() = default;

    IAICondition(const IAICondition& other) = default;
    IAICondition(IAICondition&& other) noexcept = default;
    IAICondition& operator=(const IAICondition& other) = default;
    IAICondition& operator=(IAICondition&& other) noexcept = default;

    virtual bool Test(const AIContext& context) const = 0;
};

class IAITask
{
public:
    virtual ~IAITask() = default;

    IAITask(const IAITask& other) = default;
    IAITask(IAITask&& other) noexcept = default;
    IAITask& operator=(const IAITask& other) = default;
    IAITask& operator=(IAITask&& other) noexcept = default;
    
    virtual void OnEnter(AIContext& context);
    virtual TaskStatus OnTick(AIContext& context);
    virtual void OnExit(AIContext& context);

    virtual Owner<IAITask> Clone() const = 0;
};

/**
 * CRTP-helper for implementing IAITask. The derived class must implement a copy constructor.
 * @tparam T The derived class type.
 */
template <typename T> 
class TAITask : public IAITask
{
public:
    Owner<IAITask> Clone() const override
    {
        return CreateOwner<T>(static_cast<const T&>(*this));
    }
};

struct AITransition
{
    StateID TargetState = InvalidState;
    std::vector<Ref<IAICondition>> Conditions;
    std::string DebugName;
};

struct AIStateDefinition
{
    StateID ID = InvalidState;
    std::string Name;
    std::vector<Ref<IAITask>> Tasks;
    std::vector<AITransition> Transitions;
};

class AIStateTree final
{
public:
    explicit AIStateTree(std::string name);

    AIStateDefinition& AddState(StateID id, std::string name);

    void SetEntryState(StateID id);

    const AIStateDefinition* FindState(StateID id) const;

private:
    std::string m_Name;
    StateID m_EntryState = InvalidState;
    std::unordered_map<StateID, AIStateDefinition> m_States;
};

}