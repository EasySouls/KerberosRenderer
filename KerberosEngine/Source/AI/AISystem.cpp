#include "AISystem.hpp"
#include "Scene/Scene.hpp"
#include "Scene/Entity.hpp"

namespace Kerberos {


AISystem::AISystem(Scene& scene)
    : m_Scene(&scene)
{
}

void AISystem::OnStart()
{
    // TODO: Initialize runtime blackboard values from authored values, build task instances
}

void AISystem::OnStop()
{
    // TODO: Clear runtime blackboard values, destroy task instances
}

void AISystem::Update(const float dt) const
{
    const auto view = m_Scene->m_Registry.view<AIStateMachineComponent, AIBlackboardComponent>();

    for (const auto entity : view)
    {
        auto& sm = view.get<AIStateMachineComponent>(entity);
        if (!sm.Tree)
            continue;

        const UUID entityID = m_Scene->m_Registry.get<IDComponent>(entity).ID;

        AIContext ctx{ .World = m_Scene, .Entity = entityID, .DeltaTime = dt };
        const AIStateDefinition* currentStateDef = sm.Tree->FindState(sm.CurrentState);

        // Initial State Entry
        if (!sm.IsInitialized && currentStateDef)
        {
            for (const Ref<IAITask>& task : currentStateDef->Tasks)
                task->OnEnter(ctx);

            sm.IsInitialized = true;
        }

        sm.TimeInState += dt;

        // Evaluate Transitions
        StateID nextState = 0;
        if (currentStateDef)
        {
            for (const AITransition& transition : currentStateDef->Transitions)
            {
                bool conditionsMet = true;
                for (const Ref<IAICondition>& condition : transition.Conditions)
                {
                    if (!condition->Test(ctx))
                    {
                        conditionsMet = false;
                        break;
                    }
                }
                if (conditionsMet)
                {
                    nextState = transition.TargetState;
                    break; // High-priority transition taken
                }
            }
        }

        // State Transition Execution
        if (nextState != 0 && nextState != sm.CurrentState)
        {
            if (currentStateDef)
            {
                for (const Ref<IAITask>& task : currentStateDef->Tasks)
                    task->OnExit(ctx);
            }

            sm.PreviousState = sm.CurrentState;
            sm.CurrentState = nextState;
            sm.TimeInState = 0.0f;

            currentStateDef = sm.Tree->FindState(sm.CurrentState);
            if (currentStateDef)
            {
                for (const Ref<IAITask>& task : currentStateDef->Tasks)
                    task->OnEnter(ctx);
            }
        }

        // Tick Active Tasks
        if (currentStateDef)
        {
            for (const Ref<IAITask>& task : currentStateDef->Tasks)
            {
                task->OnTick(ctx);
            }
        }
    }
}

bool AISystem::IsEnabled() const
{
    return m_Enabled;
}

void AISystem::SetEnabled(const bool enabled)
{
    m_Enabled = enabled;
}

void AISystem::UpdatePerception([[maybe_unused]] Entity entity, [[maybe_unused]] AIBlackboardComponent& blackboard, [[maybe_unused]] AIPerceptionComponent& perception, [[maybe_unused]] const float dt)
{
    /*
     * Perception (throttled): 
     * find nearest AITargetableComponent within SightRadius, optional FOV, optional Jolt Raycast from eye to target. 
     * Keep the current target while within LoseRadius. Write TargetEntity, HasLineOfSight, LastKnownPosition. 
     * Clear stale UUIDs via TryGetEntityByUUID.
     */
}

void AISystem::UpdateStateMachine([[maybe_unused]] Entity entity, [[maybe_unused]] AIBlackboardComponent& blackboard, [[maybe_unused]] AIStateMachineComponent& stateMachine, [[maybe_unused]] const float dt)
{
    /*
     * State machine: 
     * lazily initialize (build clones, enter tree entry state).
     * Evaluate transitions in order.
     * On change run OnExit on old instances, reset TimeInState, run OnEnter on new instances.
     * Tick active task instances. 
     * Task status is recorded for debug.
     */
}

void AISystem::UpdateMovement([[maybe_unused]] Entity entity, [[maybe_unused]] AIStateMachineComponent& stateMachine, [[maybe_unused]] AIControllerComponent& controller, [[maybe_unused]] const float dt)
{
    /*
     * Movement: 
     * Update the entity's position based on its current state and the controller's input.
     * Apply any necessary physics or collision detection.
     * Zero velocity first, tasks set it. 
     * Integrate DesiredVelocity * dt into local Translation (root agents), update yaw from LookAtTarget, 
     * then call Scene::CalculateEntityTransform(entity) so WorldTransform is current for Jolt.
     */
}

}