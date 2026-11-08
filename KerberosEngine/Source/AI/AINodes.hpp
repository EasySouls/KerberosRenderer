#pragma once 

#include "AIStateTree.hpp"

namespace Kerberos::AI {

class ConditionDistanceToTarget : public IAICondition
{
public:
    ConditionDistanceToTarget(BlackboardKey targetKey, float threshold, bool lessThan = true);

    bool TestCondition(const AIContext& ctx) const override;

private:
    BlackboardKey m_TargetKey;
    float m_ThresholdSq;
    bool m_LessThan;
};

class ConditionBlackboardBool : public IAICondition
{
public:
    ConditionBlackboardBool(BlackboardKey key, bool expectedValue);

    bool TestCondition(const AIContext& ctx) const override;

private:
    BlackboardKey m_Key;
    bool m_ExpectedValue;
};

class ConditionTimeInState : public IAICondition
{
public:
    explicit ConditionTimeInState(float threshold, bool lessThan = true);

    bool TestCondition(const AIContext& ctx) const override;

private:
    float m_Threshold;
    bool m_LessThan;
};

class ConditionTargetVisible : public IAICondition
{
public:
    ConditionTargetVisible() = default;

    bool TestCondition(const AIContext& ctx) const override;
};

class ConditionTargetExists : public IAICondition
{
public:
    explicit ConditionTargetExists(BlackboardKey targetKey);

    bool TestCondition(const AIContext& ctx) const override;

private:
    BlackboardKey m_TargetKey;
};

class TaskChaseTarget : public TAITask<TaskChaseTarget>
{
public:
    explicit TaskChaseTarget(float speed);

    TaskStatus OnTick(AIContext& ctx) override;

    void OnExit(AIContext& ctx) override;

private:
    float m_Speed;
};

class TaskFaceTarget : public TAITask<TaskFaceTarget>
{
public:
    explicit TaskFaceTarget(float rotationSpeed);

    TaskStatus OnTick(AIContext& ctx) override;

private:
    float m_RotationSpeed;
};

class TaskStop : public TAITask<TaskStop>
{
public:
    TaskStop();

    void OnEnter(AIContext& context) override;
};

class TaskWait : public TAITask<TaskWait>
{
public:
    explicit TaskWait(float duration);

    TaskStatus OnTick(AIContext& ctx) override;

private:
    float m_Duration;
};

class TaskSetBlackboardBool : public TAITask<TaskSetBlackboardBool>
{
public:
    TaskSetBlackboardBool(BlackboardKey key, bool value);

    void OnEnter(AIContext& context) override;

private:
    BlackboardKey m_Key;
    bool m_Value;
};

class TaskAttackTarget : public TAITask<TaskAttackTarget>
{
public:
    TaskAttackTarget();

    TaskStatus OnTick(AIContext& ctx) override;
};

}