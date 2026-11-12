#pragma once

#include "AIDebug.hpp"
#include "Scene/Components/AIComponents.hpp"

#include <string_view>
#include <unordered_map>
#include <vector>

namespace Kerberos {

class Scene;
class Entity;

class AISystem
{
public:
    explicit AISystem(Scene& scene);

    /// Resets transient state and seeds authored blackboard values.
    void OnStart();

    /// Clears runtime blackboard values and per-entity task instances.
    void OnStop();

    /// Perception -> state machine -> movement for every AI agent in the scene.
    void Update(float dt);

    bool IsEnabled() const;
    void SetEnabled(bool enabled);
    
    void SetDebugCaptureEnabled(bool enabled);
    [[nodiscard]] bool IsDebugCaptureEnabled() const;

    [[nodiscard]] const std::vector<AI::AITransitionEvent>& GetTransitionEvents() const;
    [[nodiscard]] const AI::AIAgentDebugSnapshot* GetDebugSnapshot(Entity entity) const;
    [[nodiscard]] const AI::AIAgentDebugSnapshot* GetDebugSnapshot(UUID entityID) const;

private:
    bool EnsureInitialized(Entity entity,
                           AIBlackboardComponent& blackboard,
                           AIStateMachineComponent& stateMachine);

    void SeedAuthoredValues(AIBlackboardComponent& blackboard);
    static void ResetRuntimeState(AIStateMachineComponent& stateMachine);

    void TransitionTo(Entity entity,
                      AIBlackboardComponent& blackboard,
                      AIStateMachineComponent& stateMachine,
                      AI::StateID targetState,
                      AI::AIContext& context,
                      std::string_view transitionName,
                      AI::AITransitionReason reason);

    void UpdatePerception(Entity entity,
                          AIBlackboardComponent& blackboard,
                          AIPerceptionComponent& perception,
                          float dt) const;

    void UpdateStateMachine(Entity entity,
                            AIBlackboardComponent& blackboard,
                            AIStateMachineComponent& stateMachine,
                            float dt);

    void UpdateMovement(Entity entity,
                        AIStateMachineComponent& stateMachine,
                        AIControllerComponent& controller,
                        float dt) const;
    
    void RecordTransition(const AIStateMachineComponent& stateMachine,
                          UUID entity,
                          AI::StateID from,
                          AI::StateID to,
                          std::string_view transitionName,
                          AI::AITransitionReason reason,
                          float timeInPreviousState);

    void ClearDebugCapture();
    
private:
    Scene* m_Scene = nullptr;
    bool m_Enabled = true;
    
    bool m_DebugCaptureEnabled = false;
    double m_DebugTime = 0.0;
    std::vector<AI::AITransitionEvent> m_TransitionEvents;
    std::unordered_map<UUID, AI::AIAgentDebugSnapshot> m_DebugSnapshots;
    static constexpr std::size_t MaxTransitionEvents = 256;
};

}