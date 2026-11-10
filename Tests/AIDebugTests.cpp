#include "AI/AIDebug.hpp"
#include "AI/AINodes.hpp"
#include "AI/AIStateTreeRegistry.hpp"
#include "Scene/Components/AIComponents.hpp"
#include "Scene/Entity.hpp"
#include "Scene/Scene.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

import Kerberos;

namespace {

using namespace Kerberos;
using namespace Kerberos::AI;

Entity CreateAgent(const Ref<Scene>& scene, const char* name)
{
    Entity entity = scene->CreateEntity(name);
    entity.AddComponent<AIBlackboardComponent>();
    entity.AddComponent<AIStateMachineComponent>();
    entity.AddComponent<AIControllerComponent>();
    return entity;
}

class CountingTask final : public TAITask<CountingTask>
{
public:
    TaskStatus OnTick(AIContext&) override
    {
        ++TickCount;
        return TaskStatus::Running;
    }

    int TickCount = 0;
};

class FailingTask final : public TAITask<FailingTask>
{
public:
    TaskStatus OnTick(AIContext&) override { return TaskStatus::Failed; }
};

Ref<IAITask> MakeTask(const char* debugType)
{
    Ref<CountingTask> task = CreateRef<CountingTask>();
    task->SetDebugType(debugType);
    return task;
}

Ref<IAITask> MakeFailingTask(const char* debugType)
{
    Ref<FailingTask> task = CreateRef<FailingTask>();
    task->SetDebugType(debugType);
    return task;
}

Ref<IAICondition> MakeTimeCondition(const float time, const bool lessThan, const char* debugType = "TimeInState")
{
    Ref<ConditionTimeInState> condition = CreateRef<ConditionTimeInState>(time, lessThan);
    condition->SetDebugType(debugType);
    return condition;
}

Ref<AIStateTree> MakeTransitionTree()
{
    Ref<AIStateTree> tree = CreateRef<AIStateTree>("TransitionTree");

    AIStateDefinition& idle = tree->AddState("Idle"_id, "Idle");
    idle.Tasks.push_back(MakeTask("IdleTask"));

    AITransition transition;
    transition.TargetState = "Chase"_id;
    transition.DebugName = "WaitFinished";
    transition.Conditions.push_back(MakeTimeCondition(0.1f, false));
    idle.Transitions.push_back(std::move(transition));

    AIStateDefinition& chase = tree->AddState("Chase"_id, "Chase");
    chase.Tasks.push_back(MakeTask("ChaseTask"));

    tree->SetEntryState("Idle"_id);
    return tree;
}

Ref<AIStateTree> MakeToggleTree()
{
    Ref<AIStateTree> tree = CreateRef<AIStateTree>("ToggleTree");

    AIStateDefinition& a = tree->AddState("A"_id, "A");
    AITransition toB;
    toB.TargetState = "B"_id;
    toB.DebugName = "AtoB";
    a.Transitions.push_back(std::move(toB));

    AIStateDefinition& b = tree->AddState("B"_id, "B");
    AITransition toA;
    toA.TargetState = "A"_id;
    toA.DebugName = "BtoA";
    b.Transitions.push_back(std::move(toA));

    tree->SetEntryState("A"_id);
    return tree;
}

class AIDebugCaptureTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        AIStateTreeRegistry::ClearAll();
    }

    void TearDown() override
    {
        AIStateTreeRegistry::ClearAll();
    }
};

const AITransitionEvent* FindEvent(const std::vector<AITransitionEvent>& events, const AITransitionReason reason)
{
    const auto it = std::find_if(events.begin(), events.end(), [reason](const AITransitionEvent& event) {
        return event.Reason == reason;
    });
    return it != events.end() ? &*it : nullptr;
}

} // namespace

TEST_F(AIDebugCaptureTest, CaptureDisabledProducesNoData)
{
    const Ref<Scene> scene = CreateRef<Scene>();
    Entity entity = CreateAgent(scene, "Agent");
    entity.GetComponent<AIStateMachineComponent>().Tree = MakeTransitionTree();

    scene->GetAISystem().OnStart();
    scene->GetAISystem().Update(0.05f);

    EXPECT_FALSE(scene->GetAISystem().IsDebugCaptureEnabled());
    EXPECT_TRUE(scene->GetAISystem().GetTransitionEvents().empty());
    EXPECT_EQ(scene->GetAISystem().GetDebugSnapshot(entity), nullptr);
}

TEST_F(AIDebugCaptureTest, InitializationRecordsEntryEventAndSnapshot)
{
    const Ref<Scene> scene = CreateRef<Scene>();
    Entity entity = CreateAgent(scene, "Agent");
    entity.GetComponent<AIStateMachineComponent>().Tree = MakeTransitionTree();

    scene->GetAISystem().SetDebugCaptureEnabled(true);
    scene->GetAISystem().OnStart();
    scene->GetAISystem().Update(0.05f);

    const auto& events = scene->GetAISystem().GetTransitionEvents();
    ASSERT_EQ(events.size(), 1u);

    const AITransitionEvent& event = events.front();
    EXPECT_EQ(event.Reason, AITransitionReason::Initialize);
    EXPECT_EQ(event.FromState, InvalidState);
    EXPECT_EQ(event.ToState, "Idle"_id);
    EXPECT_TRUE(event.FromStateName.empty());
    EXPECT_EQ(event.ToStateName, "Idle");
    EXPECT_EQ(event.TransitionName, "Entry");
    EXPECT_FLOAT_EQ(event.TimeInPreviousState, 0.0f);

    const AIAgentDebugSnapshot* snapshot = scene->GetAISystem().GetDebugSnapshot(entity);
    ASSERT_NE(snapshot, nullptr);
    EXPECT_TRUE(snapshot->Valid);
    EXPECT_EQ(snapshot->Entity, entity.GetUUID());
    EXPECT_EQ(snapshot->TreeName, "TransitionTree");
    EXPECT_EQ(snapshot->EvaluatedState, "Idle"_id);
    EXPECT_EQ(snapshot->CurrentState, "Idle"_id);
    EXPECT_EQ(snapshot->PreviousState, InvalidState);
}

TEST_F(AIDebugCaptureTest, ConditionTransitionTracesCandidateAndEvent)
{
    const Ref<Scene> scene = CreateRef<Scene>();
    Entity entity = CreateAgent(scene, "Agent");
    entity.GetComponent<AIStateMachineComponent>().Tree = MakeTransitionTree();

    scene->GetAISystem().SetDebugCaptureEnabled(true);
    scene->GetAISystem().OnStart();

    // First tick: below the threshold, candidate is evaluated but not satisfied.
    scene->GetAISystem().Update(0.05f);

    const AIAgentDebugSnapshot* firstSnapshot = scene->GetAISystem().GetDebugSnapshot(entity);
    ASSERT_NE(firstSnapshot, nullptr);
    ASSERT_EQ(firstSnapshot->Candidates.size(), 1u);
    const AITransitionDebugTrace& firstCandidate = firstSnapshot->Candidates.front();
    EXPECT_EQ(firstCandidate.Name, "WaitFinished");
    EXPECT_EQ(firstCandidate.TargetState, "Chase"_id);
    EXPECT_EQ(firstCandidate.TargetStateName, "Chase");
    EXPECT_FALSE(firstCandidate.Satisfied);
    ASSERT_EQ(firstCandidate.Conditions.size(), 1u);
    EXPECT_EQ(firstCandidate.Conditions.front().Type, "TimeInState");
    EXPECT_FALSE(firstCandidate.Conditions.front().Result);
    EXPECT_EQ(firstSnapshot->EvaluatedState, "Idle"_id);
    EXPECT_EQ(firstSnapshot->CurrentState, "Idle"_id);
    ASSERT_EQ(firstSnapshot->Tasks.size(), 1u);
    EXPECT_EQ(firstSnapshot->Tasks.front().Type, "IdleTask");

    // Second tick: the transition fires and the new state's tasks tick.
    scene->GetAISystem().Update(0.06f);

    const auto& events = scene->GetAISystem().GetTransitionEvents();
    const AITransitionEvent* conditionEvent =
        FindEvent(events, AITransitionReason::Condition);
    ASSERT_NE(conditionEvent, nullptr);
    EXPECT_EQ(conditionEvent->FromState, "Idle"_id);
    EXPECT_EQ(conditionEvent->ToState, "Chase"_id);
    EXPECT_EQ(conditionEvent->FromStateName, "Idle");
    EXPECT_EQ(conditionEvent->ToStateName, "Chase");
    EXPECT_EQ(conditionEvent->TransitionName, "WaitFinished");
    EXPECT_NEAR(conditionEvent->TimeInPreviousState, 0.11f, 1.0e-4f);

    const AIAgentDebugSnapshot* snapshot = scene->GetAISystem().GetDebugSnapshot(entity);
    ASSERT_NE(snapshot, nullptr);
    EXPECT_TRUE(snapshot->Valid);
    EXPECT_EQ(snapshot->EvaluatedState, "Idle"_id);
    EXPECT_EQ(snapshot->CurrentState, "Chase"_id);
    EXPECT_EQ(snapshot->PreviousState, "Idle"_id);
    ASSERT_EQ(snapshot->Candidates.size(), 1u);
    EXPECT_TRUE(snapshot->Candidates.front().Satisfied);
    ASSERT_EQ(snapshot->Tasks.size(), 1u);
    EXPECT_EQ(snapshot->Tasks.front().Type, "ChaseTask");
    EXPECT_EQ(snapshot->Tasks.front().Status, TaskStatus::Running);
}

TEST_F(AIDebugCaptureTest, ScriptRequestRecordsScriptRequestEvent)
{
    const Ref<Scene> scene = CreateRef<Scene>();
    Entity entity = CreateAgent(scene, "Agent");

    Ref<AIStateTree> tree = CreateRef<AIStateTree>("RequestTree");
    tree->AddState("Idle"_id, "Idle");
    tree->AddState("Chase"_id, "Chase");
    tree->SetEntryState("Idle"_id);
    entity.GetComponent<AIStateMachineComponent>().Tree = tree;

    scene->GetAISystem().SetDebugCaptureEnabled(true);
    scene->GetAISystem().OnStart();
    scene->GetAISystem().Update(0.01f);

    auto& stateMachine = entity.GetComponent<AIStateMachineComponent>();
    ASSERT_EQ(stateMachine.CurrentState, "Idle"_id);
    stateMachine.PendingState = "Chase"_id;

    scene->GetAISystem().Update(0.1f);

    const auto& events = scene->GetAISystem().GetTransitionEvents();
    const AITransitionEvent* requestEvent =
        FindEvent(events, AITransitionReason::ScriptRequest);
    ASSERT_NE(requestEvent, nullptr);
    EXPECT_EQ(requestEvent->FromStateName, "Idle");
    EXPECT_EQ(requestEvent->ToStateName, "Chase");
    EXPECT_EQ(requestEvent->TransitionName, "PendingState");

    const AIAgentDebugSnapshot* snapshot = scene->GetAISystem().GetDebugSnapshot(entity);
    ASSERT_NE(snapshot, nullptr);
    EXPECT_EQ(snapshot->EvaluatedState, "Chase"_id);
    EXPECT_EQ(snapshot->CurrentState, "Chase"_id);
    EXPECT_EQ(snapshot->PreviousState, "Idle"_id);
}

TEST_F(AIDebugCaptureTest, TaskFailureStopsTracingAndReportsFailedStatus)
{
    Ref<AIStateTree> tree = CreateRef<AIStateTree>("FailureTree");
    AIStateDefinition& state = tree->AddState("Idle"_id, "Idle");
    state.Tasks.push_back(MakeFailingTask("FailTask"));
    state.Tasks.push_back(MakeTask("AfterFailTask"));
    tree->SetEntryState("Idle"_id);

    const Ref<Scene> scene = CreateRef<Scene>();
    Entity entity = CreateAgent(scene, "Agent");
    entity.GetComponent<AIStateMachineComponent>().Tree = tree;

    scene->GetAISystem().SetDebugCaptureEnabled(true);
    scene->GetAISystem().OnStart();
    scene->GetAISystem().Update(0.1f);

    const AIAgentDebugSnapshot* snapshot = scene->GetAISystem().GetDebugSnapshot(entity);
    ASSERT_NE(snapshot, nullptr);
    ASSERT_EQ(snapshot->Tasks.size(), 1u);
    EXPECT_EQ(snapshot->Tasks.front().Type, "FailTask");
    EXPECT_EQ(snapshot->Tasks.front().Status, TaskStatus::Failed);
    EXPECT_EQ(snapshot->LastTaskStatus, TaskStatus::Failed);
}

TEST_F(AIDebugCaptureTest, NoTransitionLeavesEvaluatedStateEqualAndReportsUnsatisfiedCandidate)
{
    const Ref<Scene> scene = CreateRef<Scene>();
    Entity entity = CreateAgent(scene, "Agent");
    entity.GetComponent<AIStateMachineComponent>().Tree = MakeTransitionTree();

    scene->GetAISystem().SetDebugCaptureEnabled(true);
    scene->GetAISystem().OnStart();
    scene->GetAISystem().Update(0.05f);

    const AIAgentDebugSnapshot* snapshot = scene->GetAISystem().GetDebugSnapshot(entity);
    ASSERT_NE(snapshot, nullptr);
    EXPECT_EQ(snapshot->EvaluatedState, snapshot->CurrentState);
    ASSERT_EQ(snapshot->Candidates.size(), 1u);
    EXPECT_FALSE(snapshot->Candidates.front().Satisfied);

    const AITransitionEvent* conditionEvent =
        FindEvent(scene->GetAISystem().GetTransitionEvents(), AITransitionReason::Condition);
    EXPECT_EQ(conditionEvent, nullptr);
}

TEST_F(AIDebugCaptureTest, TransitionRingBufferIsBounded)
{
    const Ref<Scene> scene = CreateRef<Scene>();
    Entity entity = CreateAgent(scene, "Agent");
    entity.GetComponent<AIStateMachineComponent>().Tree = MakeToggleTree();

    scene->GetAISystem().SetDebugCaptureEnabled(true);
    scene->GetAISystem().OnStart();

    constexpr int UpdateCount = 300;
    for (int i = 0; i < UpdateCount; ++i)
        scene->GetAISystem().Update(0.01f);

    const auto& events = scene->GetAISystem().GetTransitionEvents();
    EXPECT_EQ(events.size(), 256u);

    // Initialization and the oldest transitions have been dropped.
    EXPECT_EQ(events.front().Reason, AITransitionReason::Condition);
    EXPECT_EQ(events.back().Reason, AITransitionReason::Condition);
    EXPECT_GT(events.back().Time, events.front().Time);
}

TEST_F(AIDebugCaptureTest, ClearingDisablesAndResetsCaptureData)
{
    const Ref<Scene> scene = CreateRef<Scene>();
    Entity entity = CreateAgent(scene, "Agent");
    entity.GetComponent<AIStateMachineComponent>().Tree = MakeTransitionTree();

    AISystem& system = scene->GetAISystem();
    system.SetDebugCaptureEnabled(true);
    system.OnStart();
    system.Update(0.05f);

    ASSERT_FALSE(system.GetTransitionEvents().empty());
    ASSERT_NE(system.GetDebugSnapshot(entity), nullptr);

    // Enabling again while already enabled must not clear the current data.
    system.SetDebugCaptureEnabled(true);
    EXPECT_FALSE(system.GetTransitionEvents().empty());
    EXPECT_NE(system.GetDebugSnapshot(entity), nullptr);

    system.SetDebugCaptureEnabled(false);
    EXPECT_FALSE(system.IsDebugCaptureEnabled());
    EXPECT_TRUE(system.GetTransitionEvents().empty());
    EXPECT_EQ(system.GetDebugSnapshot(entity), nullptr);

    // OnStop clears data but does not disable capture.
    system.SetDebugCaptureEnabled(true);
    system.OnStart();
    system.Update(0.05f);
    ASSERT_FALSE(system.GetTransitionEvents().empty());
    ASSERT_NE(system.GetDebugSnapshot(entity), nullptr);

    system.OnStop();
    EXPECT_TRUE(system.IsDebugCaptureEnabled());
    EXPECT_TRUE(system.GetTransitionEvents().empty());
    EXPECT_EQ(system.GetDebugSnapshot(entity), nullptr);
}
