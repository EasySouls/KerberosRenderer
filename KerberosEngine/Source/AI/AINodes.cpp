#include "AINodes.hpp"

#include "Scene/Components.hpp"
#include "Scene/Components/AIComponents.hpp"
#include "Scene/Scene.hpp"
#include "Scene/Entity.hpp"

namespace Kerberos::AI {

ConditionDistanceToTarget::ConditionDistanceToTarget(const BlackboardKey targetKey, const float threshold, const bool lessThan)
    : m_TargetKey(targetKey), m_ThresholdSq(threshold * threshold), m_LessThan(lessThan)
{}

bool ConditionDistanceToTarget::TestCondition(const AIContext& ctx) const
{
    const Entity entity = ctx.World->TryGetEntityByUUID(ctx.Entity);
    if (!entity || !entity.HasComponent<AIBlackboardComponent>())
        return false;

    const auto& bb = entity.GetComponent<AIBlackboardComponent>();
    const UUID* target = bb.Get<UUID>(m_TargetKey);
    if (!target || *target == UUID::Invalid())
        return false;

    const Entity targetEntity = ctx.World->TryGetEntityByUUID(*target);
    if (!targetEntity ||
        !entity.HasComponent<TransformComponent>() ||
        !targetEntity.HasComponent<TransformComponent>())
    {
        return false;
    }

    const glm::vec3 myPos = glm::vec3(entity.GetComponent<TransformComponent>().WorldTransform[3]);
    const glm::vec3 targetPos = glm::vec3(targetEntity.GetComponent<TransformComponent>().WorldTransform[3]);

    const float distSq = glm::distance2(myPos, targetPos);
    return m_LessThan ? (distSq <= m_ThresholdSq) : (distSq > m_ThresholdSq);
}

ConditionBlackboardBool::ConditionBlackboardBool(const BlackboardKey key, const bool expectedValue)
    : m_Key(key), m_ExpectedValue(expectedValue)
{
}

bool ConditionBlackboardBool::TestCondition(const AIContext& ctx) const
{
    const Entity entity = ctx.World->TryGetEntityByUUID(ctx.Entity);
    if (!entity || !entity.HasComponent<AIBlackboardComponent>())
        return false;

    const auto& bb = entity.GetComponent<AIBlackboardComponent>();
    const auto* value = bb.Get<bool>(m_Key);
    if (!value)
        return false;

    return *value == m_ExpectedValue;
}

ConditionTimeInState::ConditionTimeInState(const float threshold, const bool lessThan)
    : m_Threshold(threshold), m_LessThan(lessThan)
{
}

bool ConditionTimeInState::TestCondition(const AIContext& ctx) const
{
    const Entity entity = ctx.World->TryGetEntityByUUID(ctx.Entity);
    if (!entity || !entity.HasComponent<AIBlackboardComponent>())
        return false;

    const auto& bb = entity.GetComponent<AIBlackboardComponent>();
    const auto* timeInState = bb.Get<float>(StateTime);
    if (!timeInState)
        return false;

    return m_LessThan ? (*timeInState < m_Threshold) : (*timeInState >= m_Threshold);
}

bool ConditionTargetVisible::TestCondition(const AIContext& ctx) const
{
    const Entity entity = ctx.World->TryGetEntityByUUID(ctx.Entity);
    if (!entity || !entity.HasComponent<AIBlackboardComponent>())
        return false;

    const auto& bb = entity.GetComponent<AIBlackboardComponent>();
    const auto* visible = bb.Get<bool>(HasLineOfSight);
    return visible && *visible;
}

ConditionTargetExists::ConditionTargetExists(const BlackboardKey targetKey)
    : m_TargetKey(targetKey)
{
}

bool ConditionTargetExists::TestCondition(const AIContext& ctx) const
{
    const Entity entity = ctx.World->TryGetEntityByUUID(ctx.Entity);
    if (!entity || !entity.HasComponent<AIBlackboardComponent>())
        return false;

    const auto& bb = entity.GetComponent<AIBlackboardComponent>();
    const auto* target = bb.Get<UUID>(m_TargetKey);
    return target && *target != UUID::Invalid();
}


TaskChaseTarget::TaskChaseTarget(const BlackboardKey targetKey, const float speed)
    : TAITask<TaskChaseTarget>(), m_TargetKey(targetKey), m_Speed(speed)
{
}

TaskStatus TaskChaseTarget::OnTick(AIContext& ctx)
{
    const Entity entity = ctx.World->TryGetEntityByUUID(ctx.Entity);
    if (!entity || !entity.HasComponent<AIBlackboardComponent>())
        return TaskStatus::Failed;

    if (!entity.HasComponent<AIControllerComponent>())
        return TaskStatus::Failed;

    const auto& bb = entity.GetComponent<AIBlackboardComponent>();
    const auto* target = bb.Get<UUID>(m_TargetKey);
    if (!target || *target == UUID::Invalid())
        return TaskStatus::Failed;

    const Entity targetEntity = ctx.World->TryGetEntityByUUID(*target);
    if (!targetEntity || !targetEntity.HasComponent<TransformComponent>())
        return TaskStatus::Failed;

    const glm::vec3 myPos = glm::vec3(entity.GetComponent<TransformComponent>().WorldTransform[3]);
    const glm::vec3 targetPos = glm::vec3(targetEntity.GetComponent<TransformComponent>().WorldTransform[3]);

    glm::vec3 toTarget = targetPos - myPos;
    toTarget.y = 0.0f; // Constrain to ground plane if walking

    if (glm::length2(toTarget) > 0.001f)
    {
        auto& controller = entity.GetComponent<AIControllerComponent>();
        controller.DesiredVelocity = glm::normalize(toTarget) * m_Speed;
        controller.LookAtTarget = targetPos;
        controller.HasLookAt = true;
    }

    return TaskStatus::Running;
}

void TaskChaseTarget::OnExit(AIContext& ctx)
{
    const Entity entity = ctx.World->TryGetEntityByUUID(ctx.Entity);
    if (!entity || !entity.HasComponent<AIControllerComponent>())
        return;

    auto& controller = entity.GetComponent<AIControllerComponent>();
    controller.DesiredVelocity = glm::vec3(0.0f);
    controller.HasLookAt = false;
}

TaskFaceTarget::TaskFaceTarget(const BlackboardKey targetKey)
    : TAITask<TaskFaceTarget>(), m_TargetKey(targetKey)
{
}

TaskStatus TaskFaceTarget::OnTick(AIContext& ctx)
{
    const Entity entity = ctx.World->TryGetEntityByUUID(ctx.Entity);
    if (!entity || !entity.HasComponent<AIBlackboardComponent>())
        return TaskStatus::Failed;

    if (!entity.HasComponent<AIControllerComponent>())
        return TaskStatus::Failed;

    const auto& bb = entity.GetComponent<AIBlackboardComponent>();
    const auto* target = bb.Get<UUID>(m_TargetKey);
    if (!target || *target == UUID::Invalid())
        return TaskStatus::Failed;

    const Entity targetEntity = ctx.World->TryGetEntityByUUID(*target);
    if (!targetEntity || !targetEntity.HasComponent<TransformComponent>())
        return TaskStatus::Failed;

    const glm::vec3 myPos = glm::vec3(entity.GetComponent<TransformComponent>().WorldTransform[3]);
    const glm::vec3 targetPos = glm::vec3(targetEntity.GetComponent<TransformComponent>().WorldTransform[3]);

    glm::vec3 toTarget = targetPos - myPos;
    toTarget.y = 0.0f; // Constrain to ground plane if walking

    constexpr float epsilon = 1.0e-5f;
    if (glm::length2(toTarget) > epsilon)
    {
        auto& controller = entity.GetComponent<AIControllerComponent>();
        controller.LookAtTarget = targetPos;
        controller.HasLookAt = true;
    }

    return TaskStatus::Running;
}

TaskStop::TaskStop() 
    : TAITask<TaskStop>() 
{
}

void TaskStop::OnEnter(AIContext& context)
{
    const Entity entity = context.World->TryGetEntityByUUID(context.Entity);
    if (!entity || !entity.HasComponent<AIControllerComponent>())
        return;

    auto& controller = entity.GetComponent<AIControllerComponent>();
    controller.DesiredVelocity = glm::vec3(0.0f);
    controller.HasLookAt = false;
    controller.WantsToAttack = false;
}

TaskWait::TaskWait(const float duration)
    : TAITask<TaskWait>(), m_Duration(duration)
{
}

TaskStatus TaskWait::OnTick(AIContext& ctx)
{
    const Entity entity = ctx.World->TryGetEntityByUUID(ctx.Entity);
    if (!entity || !entity.HasComponent<AIBlackboardComponent>())
        return TaskStatus::Failed;

    const auto& bb = entity.GetComponent<AIBlackboardComponent>();
    auto* timeInState = bb.Get<float>(StateTime);

    if (!timeInState)
        return TaskStatus::Failed;

    if (*timeInState >= m_Duration)
        return TaskStatus::Succeeded;

    return TaskStatus::Running;
}

TaskSetBlackboardBool::TaskSetBlackboardBool(const BlackboardKey key, const bool value)
    : TAITask<TaskSetBlackboardBool>(), m_Key(key), m_Value(value)
{
}

void TaskSetBlackboardBool::OnEnter(AIContext& context)
{
    const Entity entity = context.World->TryGetEntityByUUID(context.Entity);
    if (!entity || !entity.HasComponent<AIBlackboardComponent>())
        return;

    auto& bb = entity.GetComponent<AIBlackboardComponent>();
    bb.Set(m_Key, m_Value);
}

TaskAttackTarget::TaskAttackTarget() 
    : TAITask<TaskAttackTarget>()
{
}

TaskStatus TaskAttackTarget::OnTick(AIContext& ctx)
{
    const Entity entity = ctx.World->TryGetEntityByUUID(ctx.Entity);
    if (!entity || !entity.HasComponent<AIControllerComponent>())
        return TaskStatus::Failed;

    auto& controller = entity.GetComponent<AIControllerComponent>();
    controller.WantsToAttack = true;

    return TaskStatus::Succeeded;
}


}