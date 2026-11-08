#include "AINodes.hpp"

#include "Scene/Components.hpp"
#include "Scene/Components/AIComponents.hpp"
#include "Scene/Scene.hpp"
#include "Scene/Entity.hpp"

namespace Kerberos::AI {

ConditionDistanceToTarget::ConditionDistanceToTarget(const BlackboardKey targetKey, const float threshold, const bool lessThan)
    : m_TargetKey(targetKey), m_ThresholdSq(threshold * threshold), m_LessThan(lessThan)
{}

bool ConditionDistanceToTarget::TestCondition(const AIContext& ctx) const override
{
    const Entity entity = ctx.World->GetEntityByUUID(ctx.Entity);
    const auto& bb = entity.GetComponent<AIBlackboardComponent>();
    const auto* target = bb.Get<uint64_t>(m_TargetKey);
    if (!target || *target == 0)
        return false;

    const auto& myPos = entity.GetComponent<TransformComponent>().WorldTransform[3];
    const auto& targetPos = ctx.World->GetRegistry().get<TransformComponent>(static_cast<entt::entity>(*target)).WorldTransform[3];

    const float distSq = glm::distance2(myPos, targetPos);
    return m_LessThan ? (distSq <= m_ThresholdSq) : (distSq > m_ThresholdSq);
}

ConditionBlackboardBool::ConditionBlackboardBool(const BlackboardKey key, const bool expectedValue)
    : m_Key(key), m_ExpectedValue(expectedValue)
{
}

bool ConditionBlackboardBool::TestCondition(const AIContext& ctx) const
{
    const Entity entity = ctx.World->GetEntityByUUID(ctx.Entity);
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
    const Entity entity = ctx.World->GetEntityByUUID(ctx.Entity);
    const auto& bb = entity.GetComponent<AIBlackboardComponent>();
    const auto* timeInState = bb.Get<float>(StateTime);
    if (!timeInState)
        return false;

    return m_LessThan ? (*timeInState < m_Threshold) : (*timeInState >= m_Threshold);
}

bool ConditionTargetVisible::TestCondition(const AIContext& ctx) const
{
    const Entity entity = ctx.World->GetEntityByUUID(ctx.Entity);
    const auto& bb = entity.GetComponent<AIBlackboardComponent>();
    const auto* target = bb.Get<uint64_t>(HasLineOfSight);
    return target && *target != 0;
}

ConditionTargetExists::ConditionTargetExists(const BlackboardKey targetKey)
    : m_TargetKey(targetKey)
{
}

bool ConditionTargetExists::TestCondition(const AIContext& ctx) const
{
    const Entity entity = ctx.World->GetEntityByUUID(ctx.Entity);
    const auto& bb = entity.GetComponent<AIBlackboardComponent>();
    const auto* target = bb.Get<uint64_t>(m_TargetKey);
    return target && *target != 0;
}


TaskChaseTarget::TaskChaseTarget(const float speed) 
    : TAITask<TaskChaseTarget>(), m_Speed(speed) 
{}

TaskStatus TaskChaseTarget::OnTick(AIContext& ctx)
{
    const Entity entity = ctx.World->GetEntityByUUID(ctx.Entity);
    const auto& bb = entity.GetComponent<AIBlackboardComponent>();
    const auto* target = bb.Get<uint64_t>(TargetEntity);
    if (!target || *target == 0)
        return TaskStatus::Failed;

    const auto& myPos = entity.GetComponent<TransformComponent>().Translation;
    const auto& targetPos = ctx.World->GetRegistry().get<TransformComponent>(static_cast<entt::entity>(*target)).Translation;

    glm::vec3 toTarget = targetPos - myPos;
    toTarget.y = 0.0f; // Constrain to ground plane if walking

    if (glm::length2(toTarget) > 0.001f)
    {
        auto& controller = entity.GetComponent<AIControllerComponent>();
        controller.DesiredVelocity = glm::normalize(toTarget) * m_Speed;
        controller.LookAtTarget = targetPos;
    }

    return TaskStatus::Running;
}

void TaskChaseTarget::OnExit(AIContext& ctx)
{
    const Entity entity = ctx.World->GetEntityByUUID(ctx.Entity);
    auto& controller = entity.GetComponent<AIControllerComponent>();
    controller.DesiredVelocity = glm::vec3(0.0f);
}

TaskFaceTarget::TaskFaceTarget(const float rotationSpeed)
    : TAITask<TaskFaceTarget>(), m_RotationSpeed(rotationSpeed)
{
}

TaskStatus TaskFaceTarget::OnTick(AIContext& ctx)
{
    const Entity entity = ctx.World->GetEntityByUUID(ctx.Entity);
    const auto& bb = entity.GetComponent<AIBlackboardComponent>();
    const auto* target = bb.Get<uint64_t>(TargetEntity);
    if (!target || *target == 0)
        return TaskStatus::Failed;

    const auto& myPos = entity.GetComponent<TransformComponent>().WorldTransform[3];
    const auto& targetPos = ctx.World->GetRegistry().get<TransformComponent>(static_cast<entt::entity>(*target)).WorldTransform[3];

    glm::vec3 toTarget = targetPos - myPos;
    toTarget.y = 0.0f; // Constrain to ground plane if walking

    constexpr float epsilon = 1.0e-5f;
    if (glm::length2(toTarget) > epsilon)
    {
        auto& controller = entity.GetComponent<AIControllerComponent>();
        controller.LookAtTarget = targetPos;
    }

    return TaskStatus::Running;
}

TaskStop::TaskStop() 
    : TAITask<TaskStop>() 
{
}

void TaskStop::OnEnter(AIContext& context)
{
    const Entity entity = context.World->GetEntityByUUID(context.Entity);
    auto& controller = entity.GetComponent<AIControllerComponent>();
    controller.DesiredVelocity = glm::vec3(0.0f);
}

TaskWait::TaskWait(const float duration)
    : TAITask<TaskWait>(), m_Duration(duration)
{
}

TaskStatus TaskWait::OnTick(AIContext& ctx)
{
    const Entity entity = ctx.World->GetEntityByUUID(ctx.Entity);
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
    const Entity entity = context.World->GetEntityByUUID(context.Entity);
    auto& bb = entity.GetComponent<AIBlackboardComponent>();
    bb.Set(m_Key, m_Value);
}

TaskAttackTarget::TaskAttackTarget() 
    : TAITask<TaskAttackTarget>()
{
}

TaskStatus TaskAttackTarget::OnTick(AIContext& ctx)
{
    const Entity entity = ctx.World->GetEntityByUUID(ctx.Entity);
    auto& controller = entity.GetComponent<AIControllerComponent>();
    controller.WantsToAttack = true;

    return TaskStatus::Succeeded;
}


}