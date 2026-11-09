#pragma once
#include "AIStateTree.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace Kerberos::AI {

enum class AITransitionReason : std::uint8_t
{
    Initialize,
    Condition,
    ScriptRequest,
};

struct AITaskDebugTrace
{
    std::string Type;
    TaskStatus Status = TaskStatus::Running;
};

struct AIConditionDebugTrace
{
    std::string Type;
    bool Result = false;
};

struct AITransitionDebugTrace
{
    std::string Name;
    StateID TargetState = InvalidState;
    std::string TargetStateName;
    bool Satisfied = false;
    std::vector<AIConditionDebugTrace> Conditions;
};

struct AIAgentDebugSnapshot
{
    bool Valid = false;
    UUID Entity = UUID::Invalid();
    std::string TreeName;
    
    StateID EvaluatedState = InvalidState;  // State whose transitions were evaluated
    StateID CurrentState = InvalidState;    // State after any transition this tick
    StateID PreviousState = InvalidState;   // State before any transition this tick
    StateID PendingState = InvalidState;    // State that will be entered next tick if no transition occurs
    
    float TimeInState = 0.0f;
    TaskStatus LastTaskStatus = TaskStatus::Running;
    
    std::vector<AITaskDebugTrace> Tasks;
    std::vector<AITransitionDebugTrace> Candidates;
};

struct AITransitionEvent
{
    double Time = 0.0;
    UUID Entity = UUID::Invalid();
    StateID FromState = InvalidState;
    StateID ToState = InvalidState;
    std::string FromStateName;
    std::string ToStateName;
    std::string TransitionName;
    AITransitionReason Reason = AITransitionReason::Condition;
    float TimeInPreviousState = 0.0f;
};

}
