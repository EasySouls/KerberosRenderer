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
#include <utility>
#include <vector>

import Kerberos;

namespace Kerberos {

using namespace AI;

AISystem::AISystem(Scene& scene)
    : m_Scene(&scene)
{
}

void AISystem::OnStart()
{
    RegisterStandardAINodes();
    
    ClearDebugCapture();

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
    const auto blackboards = m_Scene->m_Registry.view<AIBlackboardComponent>();
    for (const entt::entity handle : blackboards)
    {
        auto& blackboard = blackboards.get<AIBlackboardComponent>(handle);
        blackboard.Values.clear();
        blackboard.AuthoredValuesSeeded = false;
    }

    const auto stateMachines = m_Scene->m_Registry.view<AIStateMachineComponent>();
    for (const entt::entity handle : stateMachines)
    {
        ResetRuntimeState(stateMachines.get<AIStateMachineComponent>(handle));
    }
    
    ClearDebugCapture();
}

void AISystem::Update(const float dt)
{
    KBR_TRACY_FUNCTION();

    if (!m_Enabled || dt < 0.0f)
        return;
    
    if (m_DebugCaptureEnabled)
    {
        m_DebugTime += dt;
        m_DebugSnapshots.clear();
    }

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

void AISystem::SetDebugCaptureEnabled(bool enabled)
{
    if (m_DebugCaptureEnabled == enabled)
        return;

    m_DebugCaptureEnabled = enabled;
    ClearDebugCapture();
}

bool AISystem::IsDebugCaptureEnabled() const
{
    return m_DebugCaptureEnabled;
}

const std::vector<AITransitionEvent>& AISystem::GetTransitionEvents() const
{
    return m_TransitionEvents;
}

const AIAgentDebugSnapshot* AISystem::GetDebugSnapshot(const Entity entity) const
{
    if (!entity || !m_Scene || !m_Scene->IsEntityValid(entity))
        return nullptr;

    return GetDebugSnapshot(entity.GetUUID());
}

const AIAgentDebugSnapshot* AISystem::GetDebugSnapshot(const UUID entityID) const
{
    const auto it = m_DebugSnapshots.find(entityID);
    return it != m_DebugSnapshots.end() ? &it->second : nullptr;
}

bool AISystem::EnsureInitialized(const Entity entity,
                                 AIBlackboardComponent& blackboard,
                                 AIStateMachineComponent& stateMachine)
{
    KBR_TRACY_FUNCTION();
    
    SeedAuthoredValues(blackboard);

    if (!stateMachine.Tree && !stateMachine.TreeName.empty())
        stateMachine.Tree = AIStateTreeRegistry::Find(stateMachine.TreeName);

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

    const StateID entryState = stateMachine.Tree->GetEntryState();
    if (entryState == InvalidState)
    {
        Log::CoreError("AI state tree '{}' has no entry state.", stateMachine.TreeName);
        return false;
    }

    ResetRuntimeState(stateMachine);

    for (const auto& [stateID, definition] : stateMachine.Tree->GetStates())
    {
        auto& instances = stateMachine.TaskInstances[stateID];
        instances.reserve(definition.Tasks.size());
        for (const Ref<IAITask>& prototype : definition.Tasks)
        {
            if (prototype)
                instances.push_back(prototype->Clone());
        }
    }

    stateMachine.CurrentState = entryState;
    stateMachine.PreviousState = InvalidState;
    stateMachine.TimeInState = 0.0f;
    stateMachine.IsInitialized = true;

    AIContext context{ .World = m_Scene, .Entity = entity.GetUUID(), .DeltaTime = 0.0f };
    if (const auto it = stateMachine.TaskInstances.find(entryState); it != stateMachine.TaskInstances.end())
    {
        for (const Owner<IAITask>& task : it->second)
            task->OnEnter(context);
    }

    RecordTransition(stateMachine,
                     entity.GetUUID(),
                     InvalidState,
                     entryState,
                     "Entry",
                     AITransitionReason::Initialize,
                     0.0f);

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
        blackboard.Values.try_emplace(HashKey(entry.Key), entry.Value);
    }

    blackboard.AuthoredValuesSeeded = true;
}

void AISystem::ResetRuntimeState(AIStateMachineComponent& stateMachine)
{
    stateMachine.CurrentState = InvalidState;
    stateMachine.PreviousState = InvalidState;
    stateMachine.PendingState = InvalidState;
    stateMachine.TimeInState = 0.0f;
    stateMachine.IsInitialized = false;
    stateMachine.LastTaskStatus = TaskStatus::Running;
    stateMachine.TaskInstances.clear();
}

void AISystem::TransitionTo(const Entity entity,
                            AIBlackboardComponent& blackboard,
                            AIStateMachineComponent& stateMachine,
                            const StateID targetState,
                            AIContext& context,
                            const std::string_view transitionName,
                            const AITransitionReason reason)
{
    if (!stateMachine.Tree || !stateMachine.Tree->HasState(targetState))
    {
        Log::CoreError("AI entity {} tried to transition to an unknown state.", entity.GetUUID());
        return;
    }

    const float previousTime = stateMachine.TimeInState;

    if (const auto oldIt = stateMachine.TaskInstances.find(stateMachine.CurrentState);
        oldIt != stateMachine.TaskInstances.end())
    {
        for (const Owner<IAITask>& task : oldIt->second)
            task->OnExit(context);
    }

    stateMachine.PreviousState = stateMachine.CurrentState;
    stateMachine.CurrentState = targetState;
    stateMachine.TimeInState = 0.0f;
    blackboard.Set(StateTime, 0.0f);

    if (const auto newIt = stateMachine.TaskInstances.find(targetState);
        newIt != stateMachine.TaskInstances.end())
    {
        for (const Owner<IAITask>& task : newIt->second)
            task->OnEnter(context);
    }

    RecordTransition(stateMachine,
                     entity.GetUUID(),
                     stateMachine.PreviousState,
                     targetState,
                     transitionName,
                     reason,
                     previousTime);
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
        blackboard.Set(HasLineOfSight, true);
        blackboard.Set(LastKnownPosition, bestPosition);
    }
    else
    {
        blackboard.Remove(perception.TargetKey);
        blackboard.Set(HasLineOfSight, false);
    }
}

void AISystem::UpdateStateMachine(const Entity entity,
                                  AIBlackboardComponent& blackboard,
                                  AIStateMachineComponent& stateMachine,
                                  const float dt)
{
    if (!stateMachine.Tree)
        return;

    stateMachine.TimeInState += dt;
    blackboard.Set(StateTime, stateMachine.TimeInState);

    const UUID entityID = entity.GetUUID();
    AIContext context{ .World = m_Scene, .Entity = entityID, .DeltaTime = dt };

    AIAgentDebugSnapshot snapshot;
    if (m_DebugCaptureEnabled)
    {
        snapshot.Entity = entityID;
        snapshot.TreeName = stateMachine.TreeName.empty()
            ? stateMachine.Tree->GetName()
            : stateMachine.TreeName;
        snapshot.TimeInState = stateMachine.TimeInState;
        snapshot.CurrentState = stateMachine.CurrentState;
        snapshot.PreviousState = stateMachine.PreviousState;
        snapshot.PendingState = stateMachine.PendingState;
        snapshot.LastTaskStatus = stateMachine.LastTaskStatus;
    }

    const auto storeSnapshot = [&]() {
        if (!m_DebugCaptureEnabled)
            return;

        snapshot.CurrentState = stateMachine.CurrentState;
        snapshot.PreviousState = stateMachine.PreviousState;
        snapshot.PendingState = stateMachine.PendingState;
        snapshot.LastTaskStatus = stateMachine.LastTaskStatus;
        snapshot.Valid = true;
        m_DebugSnapshots[entityID] = std::move(snapshot);
    };

    if (stateMachine.PendingState != InvalidState)
    {
        const StateID requestedState = stateMachine.PendingState;
        stateMachine.PendingState = InvalidState;

        if (requestedState != stateMachine.CurrentState && stateMachine.Tree->HasState(requestedState))
        {
            TransitionTo(entity,
                         blackboard,
                         stateMachine,
                         requestedState,
                         context,
                         "PendingState",
                         AITransitionReason::ScriptRequest);
        }
    }

    if (m_DebugCaptureEnabled)
        snapshot.EvaluatedState = stateMachine.CurrentState;

    const AIStateDefinition* currentDefinition =
        stateMachine.Tree->FindState(stateMachine.CurrentState);

    if (!currentDefinition)
    {
        storeSnapshot();
        return;
    }

    StateID nextState = InvalidState;
    const AITransition* winningTransition = nullptr;

    for (const AITransition& transition : currentDefinition->Transitions)
    {
        AITransitionDebugTrace* trace = nullptr;
        if (m_DebugCaptureEnabled)
        {
            snapshot.Candidates.emplace_back();
            trace = &snapshot.Candidates.back();
            trace->Name = transition.DebugName;
            trace->TargetState = transition.TargetState;
            if (const AIStateDefinition* target = stateMachine.Tree->FindState(transition.TargetState))
                trace->TargetStateName = target->Name;
        }

        bool conditionsMet = true;
        for (const Ref<IAICondition>& condition : transition.Conditions)
        {
            const bool result = condition && condition->TestCondition(context);

            if (trace)
            {
                trace->Conditions.push_back({
                    .Type = condition ? condition->GetDebugType() : std::string("<null>"),
                    .Result = result
                });
            }

            if (!result)
            {
                conditionsMet = false;
                break;
            }
        }

        if (trace)
            trace->Satisfied = conditionsMet;

        if (conditionsMet)
        {
            nextState = transition.TargetState;
            winningTransition = &transition;
            break;
        }
    }

    if (nextState != InvalidState && nextState != stateMachine.CurrentState)
    {
        const std::string_view transitionName =
            winningTransition && !winningTransition->DebugName.empty()
                ? std::string_view(winningTransition->DebugName)
                : std::string_view("Condition");

        TransitionTo(entity,
                     blackboard,
                     stateMachine,
                     nextState,
                     context,
                     transitionName,
                     AITransitionReason::Condition);

        currentDefinition = stateMachine.Tree->FindState(stateMachine.CurrentState);
    }

    if (!currentDefinition)
    {
        storeSnapshot();
        return;
    }

    if (const auto it = stateMachine.TaskInstances.find(stateMachine.CurrentState);
        it != stateMachine.TaskInstances.end())
    {
        for (const Owner<IAITask>& task : it->second)
        {
            const TaskStatus status = task->OnTick(context);
            stateMachine.LastTaskStatus = status;

            if (m_DebugCaptureEnabled)
                snapshot.Tasks.push_back({ .Type = task->GetDebugType(), .Status = status });

            if (status == TaskStatus::Failed)
                break;
        }
    }

    storeSnapshot();
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
                Log::CoreWarn("AI entity {} is a dynamic body, AI movement will fight Jolt. "
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

void AISystem::RecordTransition(const AIStateMachineComponent& stateMachine,
                                const UUID entity,
                                const StateID from,
                                const StateID to,
                                const std::string_view transitionName,
                                const AITransitionReason reason,
                                const float timeInPreviousState)
{
    if (!m_DebugCaptureEnabled)
        return;

    AITransitionEvent event;
    event.Time = m_DebugTime;
    event.Entity = entity;
    event.FromState = from;
    event.ToState = to;

    if (stateMachine.Tree)
    {
        if (from != InvalidState)
        {
            if (const AIStateDefinition* definition = stateMachine.Tree->FindState(from))
                event.FromStateName = definition->Name;
        }

        if (to != InvalidState)
        {
            if (const AIStateDefinition* definition = stateMachine.Tree->FindState(to))
                event.ToStateName = definition->Name;
        }
    }

    event.TransitionName = std::string(transitionName);
    event.Reason = reason;
    event.TimeInPreviousState = timeInPreviousState;

    m_TransitionEvents.push_back(std::move(event));

    if (m_TransitionEvents.size() > MaxTransitionEvents)
        m_TransitionEvents.erase(m_TransitionEvents.begin());
}

void AISystem::ClearDebugCapture()
{
    m_TransitionEvents.clear();
    m_DebugSnapshots.clear();
    m_DebugTime = 0.0;
}

}