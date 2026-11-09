#include "AISystem.hpp"

#include "AIStateTreeRegistry.hpp"
#include "AIKeys.hpp"
#include "AINodeFactory.hpp"

#include "Scene/Scene.hpp"
#include "Scene/Entity.hpp"
#include "Scene/Components.hpp"
#include "Scene/Components/PhysicsComponents.hpp"
#include "Profiling/Profilers.hpp"

#include <glm/gtx/norm.hpp>
#include <glm/gtx/quaternion.hpp>

#include <cmath>
#include <limits>
#include <vector>

import Kerberos;

namespace Kerberos {

AISystem::AISystem(Scene& scene)
    : m_Scene(&scene)
{
}

void AISystem::OnStart()
{
    AI::RegisterStandardAINodes();

    auto stateMachines = m_Scene->m_Registry.view<AIStateMachineComponent>();
    for (const entt::entity handle : stateMachines)
        ResetRuntimeState(stateMachines.get<AIStateMachineComponent>(handle));

    auto blackboards = m_Scene->m_Registry.view<AIBlackboardComponent>();
    for (const entt::entity handle : blackboards)
    {
        auto& blackboard = blackboards.get<AIBlackboardComponent>(handle);
        blackboard.AuthoredValuesSeeded = false;
        SeedAuthoredValues(blackboard);
    }
}

void AISystem::OnStop()
{
    auto blackboards = m_Scene->m_Registry.view<AIBlackboardComponent>();
    for (const entt::entity handle : blackboards)
    {
        auto& blackboard = blackboards.get<AIBlackboardComponent>(handle);
        blackboard.Values.clear();
        blackboard.AuthoredValuesSeeded = false;
    }

    auto stateMachines = m_Scene->m_Registry.view<AIStateMachineComponent>();
    for (const entt::entity handle : stateMachines)
        ResetRuntimeState(stateMachines.get<AIStateMachineComponent>(handle));
}

void AISystem::Update(const float dt)
{
    KBR_TRACY_FUNCTION();

    if (!m_Enabled || dt < 0.0f)
        return;

    // Tasks and scripts may spawn or destroy entities. Snapshot the handles before ticking.
    std::vector<entt::entity> entities;
    const auto view = m_Scene->m_Registry.view<AIStateMachineComponent>();
    entities.assign(view.begin(), view.end());

    for (const entt::entity handle : entities)
    {
        if (!m_Scene->m_Registry.valid(handle))
            continue;

        Entity entity{ handle, m_Scene };
        if (!entity.HasComponent<AIBlackboardComponent>())
            continue;

        auto& stateMachine = entity.GetComponent<AIStateMachineComponent>();
        auto& blackboard = entity.GetComponent<AIBlackboardComponent>();

        if (!EnsureInitialized(entity, blackboard, stateMachine))
            continue;

        if (entity.HasComponent<AIPerceptionComponent>())
            UpdatePerception(entity, blackboard, entity.GetComponent<AIPerceptionComponent>(), dt);

        UpdateStateMachine(entity, blackboard, stateMachine, dt);

        if (entity.HasComponent<AIControllerComponent>())
            UpdateMovement(entity, stateMachine, entity.GetComponent<AIControllerComponent>(), dt);
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

bool AISystem::EnsureInitialized(Entity entity,
                                 AIBlackboardComponent& blackboard,
                                 AIStateMachineComponent& stateMachine)
{
    SeedAuthoredValues(blackboard);

    if (!stateMachine.Tree && !stateMachine.TreeName.empty())
        stateMachine.Tree = AI::AIStateTreeRegistry::Find(stateMachine.TreeName);

    if (!stateMachine.Tree)
    {
        if (!stateMachine.TreeName.empty() && !stateMachine.ResolutionFailed)
        {
            Log::CoreError("AI entity {} references unknown state tree '{}'.",
                           entity.GetUUID(),
                           stateMachine.TreeName);
            stateMachine.ResolutionFailed = true;
        }
        return false;
    }

    stateMachine.ResolutionFailed = false;

    if (stateMachine.IsInitialized)
        return true;

    const AI::StateID entryState = stateMachine.Tree->GetEntryState();
    if (entryState == AI::InvalidState)
    {
        Log::CoreError("AI state tree '{}' has no entry state.", stateMachine.TreeName);
        return false;
    }

    ResetRuntimeState(stateMachine);

    for (const auto& [stateID, definition] : stateMachine.Tree->GetStates())
    {
        auto& instances = stateMachine.TaskInstances[stateID];
        instances.reserve(definition.Tasks.size());
        for (const Ref<AI::IAITask>& prototype : definition.Tasks)
        {
            if (prototype)
                instances.push_back(prototype->Clone());
        }
    }

    stateMachine.CurrentState = entryState;
    stateMachine.PreviousState = AI::InvalidState;
    stateMachine.TimeInState = 0.0f;
    stateMachine.IsInitialized = true;

    AI::AIContext context{ m_Scene, entity.GetUUID(), 0.0f };
    if (const auto it = stateMachine.TaskInstances.find(entryState); it != stateMachine.TaskInstances.end())
    {
        for (const Owner<AI::IAITask>& task : it->second)
            task->OnEnter(context);
    }

    return true;
}

void AISystem::SeedAuthoredValues(AIBlackboardComponent& blackboard)
{
    if (blackboard.AuthoredValuesSeeded)
        return;

    for (const NamedBlackboardValue& entry : blackboard.AuthoredValues)
    {
        if (entry.Key.empty())
            continue;

        // Runtime and script writes win over authored defaults.
        blackboard.Values.try_emplace(AI::HashKey(entry.Key), entry.Value);
    }

    blackboard.AuthoredValuesSeeded = true;
}

void AISystem::ResetRuntimeState(AIStateMachineComponent& stateMachine)
{
    stateMachine.CurrentState = AI::InvalidState;
    stateMachine.PreviousState = AI::InvalidState;
    stateMachine.PendingState = AI::InvalidState;
    stateMachine.TimeInState = 0.0f;
    stateMachine.IsInitialized = false;
    stateMachine.LastTaskStatus = AI::TaskStatus::Running;
    stateMachine.TaskInstances.clear();
}

void AISystem::TransitionTo(const Entity entity,
                            AIBlackboardComponent& blackboard,
                            AIStateMachineComponent& stateMachine,
                            const AI::StateID targetState,
                            AI::AIContext& context)
{
    if (!stateMachine.Tree || !stateMachine.Tree->HasState(targetState))
    {
        Log::CoreError("AI entity {} tried to transition to an unknown state.", entity.GetUUID());
        return;
    }

    if (const auto oldIt = stateMachine.TaskInstances.find(stateMachine.CurrentState);
        oldIt != stateMachine.TaskInstances.end())
    {
        for (const Owner<AI::IAITask>& task : oldIt->second)
            task->OnExit(context);
    }

    stateMachine.PreviousState = stateMachine.CurrentState;
    stateMachine.CurrentState = targetState;
    stateMachine.TimeInState = 0.0f;
    blackboard.Set(AI::StateTime, 0.0f);

    if (const auto newIt = stateMachine.TaskInstances.find(targetState);
        newIt != stateMachine.TaskInstances.end())
    {
        for (const Owner<AI::IAITask>& task : newIt->second)
            task->OnEnter(context);
    }
}

void AISystem::UpdatePerception(Entity entity,
                                AIBlackboardComponent& blackboard,
                                AIPerceptionComponent& perception,
                                const float dt) const
{
    perception.TimeSincePerception += dt;
    if (perception.TimeSincePerception < perception.PerceptionInterval)
        return;
    perception.TimeSincePerception = 0.0f;

    if (!entity.HasComponent<TransformComponent>())
        return;

    const TransformComponent& selfTransform = entity.GetComponent<TransformComponent>();
    const glm::vec3 selfPosition = glm::vec3(selfTransform.WorldTransform[3]);

    UUID currentTarget = UUID::Invalid();
    if (const UUID* target = blackboard.Get<UUID>(perception.TargetKey))
        currentTarget = *target;

    UUID bestTarget = UUID::Invalid();
    glm::vec3 bestPosition = glm::vec3(0.0f);
    float bestDistanceSq = std::numeric_limits<float>::max();

    const auto targets = m_Scene->m_Registry.view<AITargetableComponent, TransformComponent>();
    for (const entt::entity targetHandle : targets)
    {
        if (targetHandle == static_cast<entt::entity>(entity))
            continue;

        const AITargetableComponent& targetable = targets.get<AITargetableComponent>(targetHandle);
        if (targetable.FactionId == perception.FactionId)
            continue;

        const auto* idComponent = m_Scene->m_Registry.try_get<IDComponent>(targetHandle);
        if (!idComponent)
            continue;

        const UUID candidate = idComponent->ID;
        if (!candidate.IsValid())
            continue;

        const TransformComponent& targetTransform = targets.get<TransformComponent>(targetHandle);
        const glm::vec3 targetPosition = glm::vec3(targetTransform.WorldTransform[3]);

        const glm::vec3 toTarget = targetPosition - selfPosition;
        const glm::vec3 flatToTarget(toTarget.x, 0.0f, toTarget.z);
        const float distanceSq = glm::length2(flatToTarget);

        // Hysteresis: keep the current target until it leaves LoseRadius.
        const float radius = candidate == currentTarget ? perception.LoseRadius : perception.SightRadius;
        if (distanceSq > radius * radius)
            continue;

        if (perception.FieldOfViewDegrees < 359.9f && distanceSq > 1.0e-6f)
        {
            const glm::vec3 forward = glm::normalize(
                glm::quat(selfTransform.Rotation) * glm::vec3(0.0f, 0.0f, 1.0f));
            const glm::vec3 direction = glm::normalize(flatToTarget);
            const float cosHalfFov = std::cos(glm::radians(perception.FieldOfViewDegrees * 0.5f));
            if (glm::dot(forward, direction) < cosHalfFov)
                continue;
        }

        if (perception.RequireLineOfSight)
        {
            const glm::vec3 eye = selfPosition + glm::vec3(0.0f, perception.EyeHeight, 0.0f);
            const glm::vec3 targetEye = targetPosition + glm::vec3(0.0f, perception.EyeHeight, 0.0f);
            glm::vec3 direction = targetEye - eye;
            const float distance = glm::length(direction);
            if (distance > 1.0e-4f)
            {
                direction /= distance;

                RaycastHit hit;
                const bool hasHit = m_Scene->GetPhysicsSystem().Raycast(eye, direction, distance, hit);
                const bool blockedByOther = hasHit &&
                    hit.EntityID != candidate &&
                    hit.EntityID != entity.GetUUID();
                if (blockedByOther)
                    continue;
            }
        }

        if (distanceSq < bestDistanceSq)
        {
            bestDistanceSq = distanceSq;
            bestTarget = candidate;
            bestPosition = targetPosition;
        }
    }

    if (bestTarget.IsValid())
    {
        blackboard.Set(perception.TargetKey, bestTarget);
        blackboard.Set(AI::HasLineOfSight, true);
        blackboard.Set(AI::LastKnownPosition, bestPosition);
    }
    else
    {
        blackboard.Remove(perception.TargetKey);
        blackboard.Set(AI::HasLineOfSight, false);
    }
}

void AISystem::UpdateStateMachine(const Entity entity,
                                  AIBlackboardComponent& blackboard,
                                  AIStateMachineComponent& stateMachine,
                                  const float dt) const
{
    if (!stateMachine.Tree)
        return;

    stateMachine.TimeInState += dt;
    blackboard.Set(AI::StateTime, stateMachine.TimeInState);

    AI::AIContext context{ .World = m_Scene, .Entity = entity.GetUUID(), .DeltaTime = dt };

    if (stateMachine.PendingState != AI::InvalidState)
    {
        const AI::StateID requestedState = stateMachine.PendingState;
        stateMachine.PendingState = AI::InvalidState;

        if (requestedState != stateMachine.CurrentState && stateMachine.Tree->HasState(requestedState))
            TransitionTo(entity, blackboard, stateMachine, requestedState, context);
    }

    const AI::AIStateDefinition* currentDefinition =
        stateMachine.Tree->FindState(stateMachine.CurrentState);

    if (!currentDefinition)
        return;

    AI::StateID nextState = AI::InvalidState;
    for (const AI::AITransition& transition : currentDefinition->Transitions)
    {
        bool conditionsMet = true;
        for (const Ref<AI::IAICondition>& condition : transition.Conditions)
        {
            if (!condition || !condition->TestCondition(context))
            {
                conditionsMet = false;
                break;
            }
        }

        if (conditionsMet)
        {
            nextState = transition.TargetState;
            break;
        }
    }

    if (nextState != AI::InvalidState && nextState != stateMachine.CurrentState)
    {
        TransitionTo(entity, blackboard, stateMachine, nextState, context);
        currentDefinition = stateMachine.Tree->FindState(stateMachine.CurrentState);
    }

    if (!currentDefinition)
        return;

    if (const auto it = stateMachine.TaskInstances.find(stateMachine.CurrentState);
        it != stateMachine.TaskInstances.end())
    {
        for (const Owner<AI::IAITask>& task : it->second)
        {
            const AI::TaskStatus status = task->OnTick(context);
            stateMachine.LastTaskStatus = status;
            if (status == AI::TaskStatus::Failed)
                break;
        }
    }
}

void AISystem::UpdateMovement(const Entity entity,
                              [[maybe_unused]] AIStateMachineComponent& stateMachine,
                              AIControllerComponent& controller,
                              const float dt) const
{
    if (!controller.Enabled || !entity.HasComponent<TransformComponent>())
    {
        controller.DesiredVelocity = glm::vec3(0.0f);
        controller.HasLookAt = false;
        return;
    }

    TransformComponent& transform = entity.GetComponent<TransformComponent>();

    if (controller.HasLookAt)
    {
        glm::vec3 toTarget = controller.LookAtTarget - glm::vec3(transform.WorldTransform[3]);
        toTarget.y = 0.0f;

        if (glm::length2(toTarget) > 1.0e-6f)
        {
            // TransformComponent::Rotation is euler radians; +Z is forward.
            transform.Rotation.y = std::atan2(toTarget.x, toTarget.z);
        }

        controller.HasLookAt = false;
    }

    if (glm::length2(controller.DesiredVelocity) > 1.0e-6f)
    {
        if (entity.HasComponent<RigidBody3DComponent>())
        {
            const RigidBody3DComponent& rigidBody = entity.GetComponent<RigidBody3DComponent>();
            if (rigidBody.Type == RigidBody3DComponent::BodyType::Dynamic &&
                !controller.WarnedDynamicBody)
            {
                Log::CoreWarn("AI entity {} is a dynamic body; AI movement will fight Jolt. "
                              "Use a Kinematic body for TransformComponent-driven movement.",
                              entity.GetUUID());
                controller.WarnedDynamicBody = true;
            }
        }

        transform.Translation += controller.DesiredVelocity * dt;
        m_Scene->CalculateEntityTransform(entity);
    }

    controller.DesiredVelocity = glm::vec3(0.0f);
}

}