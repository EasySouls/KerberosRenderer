#include "AIStateTree.hpp"

import Kerberos;

namespace Kerberos::AI {

void IAITask::OnEnter([[maybe_unused]] AIContext& context)
{
}

TaskStatus IAITask::OnTick([[maybe_unused]] AIContext& context)
{
    return TaskStatus::Running;
}

void IAITask::OnExit([[maybe_unused]] AIContext& context)
{
}

AIStateTree::AIStateTree(std::string name)
    : m_Name(std::move(name))
{
}

AIStateDefinition& AIStateTree::AddState(StateID id, std::string name)
{
    if (m_States.contains(id))
    {
        Log::CoreError("State ID {} already exists in the state tree.", id);
    }

    return m_States.emplace(id, AIStateDefinition{ .ID = id, .Name = std::move(name) }).first->second;
}

void AIStateTree::SetEntryState(StateID id)
{
    if (!m_States.contains(id))
    {
        Log::CoreError("Cannot set entry state to ID {} because it does not exist in the state tree.", id);
        return;
    }

    m_EntryState = id;
}

const AIStateDefinition* AIStateTree::FindState(const StateID id) const
{
    const AIStateDefinition* state = nullptr;
    if (const auto it = m_States.find(id); it != m_States.end())
    {
        state = &it->second;
    }
    return state;
}

StateID AIStateTree::GetEntryState() const
{
    return m_EntryState;
}

const std::string& AIStateTree::GetName() const
{
    return m_Name;
}

bool AIStateTree::HasState(const StateID id) const
{
    return m_States.contains(id);
}

const std::unordered_map<StateID, AIStateDefinition>& AIStateTree::GetStates() const
{
    return m_States;
}

}