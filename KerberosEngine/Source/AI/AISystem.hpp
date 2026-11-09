#pragma once

#include "Scene/Components/AIComponents.hpp"

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

private:
    bool EnsureInitialized(Entity entity,
                           AIBlackboardComponent& blackboard,
                           AIStateMachineComponent& stateMachine);

    void SeedAuthoredValues(AIBlackboardComponent& blackboard);
    static void ResetRuntimeState(AIStateMachineComponent& stateMachine);

    static void TransitionTo(Entity entity,
                             AIBlackboardComponent& blackboard,
                             AIStateMachineComponent& stateMachine,
                             AI::StateID targetState,
                             AI::AIContext& context);

    void UpdatePerception(Entity entity,
                          AIBlackboardComponent& blackboard,
                          AIPerceptionComponent& perception,
                          float dt) const;

    void UpdateStateMachine(Entity entity,
                            AIBlackboardComponent& blackboard,
                            AIStateMachineComponent& stateMachine,
                            float dt) const;

    void UpdateMovement(Entity entity,
                        AIStateMachineComponent& stateMachine,
                        AIControllerComponent& controller,
                        float dt) const;

    Scene* m_Scene = nullptr;
    bool m_Enabled = true;
};

}