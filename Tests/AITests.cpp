#include "AI/AIKeys.hpp"
#include "AI/AINodes.hpp"
#include "AI/AIStateTreeRegistry.hpp"
#include "Scene/Components/AIComponents.hpp"
#include "Scene/Entity.hpp"
#include "Scene/Scene.hpp"
#include "Serialization/SceneSerializer.hpp"

#include <glm/gtc/constants.hpp>
#include <gtest/gtest.h>

#include <string>
#include <vector>

import Kerberos;

namespace {

using namespace Kerberos;
using namespace Kerberos::AI;

/// Arbitrary non-zero asset handle used to verify TreeAsset serialization. It
/// does not need to resolve to a real imported asset in this headless test.
constexpr uint64_t kSerializedTreeAssetId = 77;

constexpr const char* kChaseTreeDocument = R"json(
{
  "Entry": "Idle",
  "States": [
    {
      "Name": "Idle",
      "Tasks": [ { "Type": "Stop" } ],
      "Transitions": [
        {
          "To": "Chase",
          "Name": "WaitFinished",
          "When": [ { "Type": "TimeInState", "Params": { "Time": 0.1, "LessThan": false } } ]
        }
      ]
    },
    {
      "Name": "Chase",
      "Tasks": [
        { "Type": "ChaseTarget", "Params": { "TargetKey": "TargetEntity", "Speed": 4.0 } }
      ]
    }
  ]
}
)json";

class AITest : public ::testing::Test
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

class CountingTask final : public TAITask<CountingTask>
{
public:
    void OnEnter(AIContext&) override { ++EnterCount; }
    TaskStatus OnTick(AIContext&) override { ++TickCount; return TaskStatus::Running; }

    int EnterCount = 0;
    int TickCount = 0;
};

Entity CreateAgent(const Ref<Scene>& scene, const char* name, const glm::vec3& position)
{
    Entity entity = scene->CreateEntity(name);
    entity.GetComponent<TransformComponent>().Translation = position;
    entity.AddComponent<AIBlackboardComponent>();
    entity.AddComponent<AIStateMachineComponent>();
    entity.AddComponent<AIControllerComponent>();
    return entity;
}

} // namespace

TEST_F(AITest, HashMaterialsAreStableAndDistinct)
{
    EXPECT_EQ(HashKey("TargetEntity"), "TargetEntity"_bb);
    EXPECT_EQ(HashState("Chase"), "Chase"_id);
    EXPECT_NE("Idle"_id, "Chase"_id);
    EXPECT_EQ(HashKey("TargetEntity"), HashFnv1a("TargetEntity"));
}

TEST_F(AITest, BlackboardStoresTypedValues)
{
    AIBlackboardComponent blackboard;

    blackboard.Set(HashKey("Bool"), true);
    blackboard.Set(HashKey("Int"), std::int64_t{ 7 });
    blackboard.Set(HashKey("Float"), 2.5f);
    blackboard.Set(HashKey("Vec"), glm::vec3(1.0f, 2.0f, 3.0f));
    blackboard.Set(HashKey("Entity"), Kerberos::UUID(42));

    ASSERT_NE(blackboard.Get<bool>(HashKey("Bool")), nullptr);
    EXPECT_TRUE(*blackboard.Get<bool>(HashKey("Bool")));
    EXPECT_EQ(*blackboard.Get<std::int64_t>(HashKey("Int")), 7);
    EXPECT_FLOAT_EQ(*blackboard.Get<float>(HashKey("Float")), 2.5f);
    EXPECT_EQ(*blackboard.Get<glm::vec3>(HashKey("Vec")), glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_EQ(*blackboard.Get<Kerberos::UUID>(HashKey("Entity")), Kerberos::UUID(42));

    // Wrong type lookups return null instead of throwing.
    EXPECT_EQ(blackboard.Get<float>(HashKey("Bool")), nullptr);

    blackboard.Remove(HashKey("Bool"));
    EXPECT_FALSE(blackboard.Has(HashKey("Bool")));
}

TEST_F(AITest, RegistryParsesDocumentsAndRejectsInvalidTrees)
{
    std::string error;
    ASSERT_TRUE(AIStateTreeRegistry::RegisterFromDocument(
        "Enemy", kChaseTreeDocument, AIStateTreeOrigin::BuiltIn, &error)) << error;

    const Ref<AIStateTree> tree = AIStateTreeRegistry::Find("Enemy");
    ASSERT_NE(tree, nullptr);
    EXPECT_EQ(tree->GetName(), "Enemy");
    EXPECT_EQ(tree->GetEntryState(), "Idle"_id);

    const AIStateDefinition* idle = tree->FindState("Idle"_id);
    ASSERT_NE(idle, nullptr);
    EXPECT_EQ(idle->Transitions.size(), 1u);
    EXPECT_EQ(tree->FindState("Chase"_id)->Tasks.size(), 1u);

    // C# serializes unused transition names as null; that must remain valid.
    ASSERT_TRUE(AIStateTreeRegistry::RegisterFromDocument(
        "NullTransitionName",
        R"json({
          "Entry": "A",
          "States": [
            { "Name": "A", "Transitions": [ { "To": "B", "Name": null, "When": [] } ] },
            { "Name": "B" }
          ]
        })json",
        AIStateTreeOrigin::BuiltIn, &error)) << error;

    EXPECT_FALSE(AIStateTreeRegistry::RegisterFromDocument(
        "Invalid", R"json({ "Entry": "Missing", "States": [ { "Name": "Idle" } ] })json",
        AIStateTreeOrigin::BuiltIn, &error));
    EXPECT_FALSE(error.empty());

    EXPECT_FALSE(AIStateTreeRegistry::RegisterFromDocument(
        "Invalid2", R"json({ "Entry": "Idle", "States": [ { "Name": "Idle", "Tasks": [ { "Type": "Nope" } ] } ] })json",
        AIStateTreeOrigin::BuiltIn, &error));

    EXPECT_TRUE(AIStateTreeRegistry::Unregister("Enemy"));
    EXPECT_EQ(AIStateTreeRegistry::Find("Enemy"), nullptr);
}

TEST_F(AITest, StateMachineEntersAndTransitions)
{
    std::string error;
    ASSERT_TRUE(AIStateTreeRegistry::RegisterFromDocument(
        "Enemy", kChaseTreeDocument, AIStateTreeOrigin::BuiltIn, &error)) << error;

    const Ref<Scene> scene = CreateRef<Scene>();
    Entity entity = CreateAgent(scene, "Enemy", glm::vec3(0.0f));
    entity.GetComponent<AIStateMachineComponent>().TreeName = "Enemy";

    scene->GetAISystem().OnStart();
    scene->GetAISystem().Update(0.05f);

    auto& stateMachine = entity.GetComponent<AIStateMachineComponent>();
    EXPECT_EQ(stateMachine.CurrentState, "Idle"_id);

    scene->GetAISystem().Update(0.06f);
    EXPECT_EQ(stateMachine.CurrentState, "Chase"_id);
    EXPECT_EQ(stateMachine.PreviousState, "Idle"_id);
    EXPECT_FLOAT_EQ(stateMachine.TimeInState, 0.0f);
}

TEST_F(AITest, TaskInstancesAreClonedPerEntity)
{
    Ref<AIStateTree> tree = CreateRef<AIStateTree>("Shared");
    AIStateDefinition& idle = tree->AddState("Idle"_id, "Idle");
    idle.Tasks.push_back(CreateRef<CountingTask>());
    tree->SetEntryState("Idle"_id);

    const Ref<Scene> scene = CreateRef<Scene>();
    Entity first = CreateAgent(scene, "First", glm::vec3(0.0f));
    Entity second = CreateAgent(scene, "Second", glm::vec3(1.0f, 0.0f, 0.0f));

    first.GetComponent<AIStateMachineComponent>().Tree = tree;
    second.GetComponent<AIStateMachineComponent>().Tree = tree;

    scene->GetAISystem().OnStart();
    scene->GetAISystem().Update(0.1f);

    auto& firstInstances = first.GetComponent<AIStateMachineComponent>().TaskInstances["Idle"_id];
    auto& secondInstances = second.GetComponent<AIStateMachineComponent>().TaskInstances["Idle"_id];
    ASSERT_EQ(firstInstances.size(), 1u);
    ASSERT_EQ(secondInstances.size(), 1u);
    EXPECT_NE(firstInstances.front().get(), secondInstances.front().get());

    const auto* firstTask = dynamic_cast<const CountingTask*>(firstInstances.front().get());
    const auto* secondTask = dynamic_cast<const CountingTask*>(secondInstances.front().get());
    ASSERT_NE(firstTask, nullptr);
    ASSERT_NE(secondTask, nullptr);
    EXPECT_EQ(firstTask->EnterCount, 1);
    EXPECT_EQ(secondTask->EnterCount, 1);
    EXPECT_EQ(firstTask->TickCount, 1);
    EXPECT_EQ(secondTask->TickCount, 1);

    const auto* prototype = dynamic_cast<const CountingTask*>(idle.Tasks.front().get());
    ASSERT_NE(prototype, nullptr);
    EXPECT_EQ(prototype->TickCount, 0);
}

TEST_F(AITest, MovementConsumesDesiredVelocityAndFacesTarget)
{
    Ref<AIStateTree> tree = CreateRef<AIStateTree>("ChaseTree");
    AIStateDefinition& chase = tree->AddState("Chase"_id, "Chase");
    chase.Tasks.push_back(CreateRef<TaskChaseTarget>(TargetEntity, 4.0f));
    tree->SetEntryState("Chase"_id);

    const Ref<Scene> scene = CreateRef<Scene>();
    Entity agent = CreateAgent(scene, "Agent", glm::vec3(0.0f));
    Entity target = scene->CreateEntity("Target");
    target.GetComponent<TransformComponent>().Translation = glm::vec3(10.0f, 0.0f, 0.0f);

    auto& stateMachine = agent.GetComponent<AIStateMachineComponent>();
    stateMachine.Tree = tree;
    agent.GetComponent<AIBlackboardComponent>().Set(TargetEntity, target.GetUUID());

    scene->CalculateEntityTransforms();
    scene->GetAISystem().OnStart();
    scene->GetAISystem().Update(0.5f);

    const TransformComponent& transform = agent.GetComponent<TransformComponent>();
    EXPECT_NEAR(transform.Translation.x, 2.0f, 1.0e-4f);
    EXPECT_NEAR(transform.Translation.z, 0.0f, 1.0e-4f);
    EXPECT_NEAR(transform.Rotation.y, glm::half_pi<float>(), 1.0e-4f);

    const AIControllerComponent& controller = agent.GetComponent<AIControllerComponent>();
    EXPECT_EQ(controller.DesiredVelocity, glm::vec3(0.0f));
}

TEST_F(AITest, PerceptionAcquiresAndLosesTargets)
{
    const Ref<Scene> scene = CreateRef<Scene>();
    Entity agent = CreateAgent(scene, "Agent", glm::vec3(0.0f));
    Entity target = scene->CreateEntity("Target");
    target.GetComponent<TransformComponent>().Translation = glm::vec3(0.0f, 0.0f, 5.0f);
    target.AddComponent<AITargetableComponent>().FactionId = 1;

    auto& perception = agent.AddComponent<AIPerceptionComponent>();
    perception.FactionId = 0;
    perception.SightRadius = 10.0f;
    perception.LoseRadius = 12.0f;
    perception.FieldOfViewDegrees = 360.0f;
    perception.RequireLineOfSight = false;
    perception.PerceptionInterval = 0.0f;

    Ref<AIStateTree> tree = CreateRef<AIStateTree>("IdleTree");
    tree->AddState("Idle"_id, "Idle");
    tree->SetEntryState("Idle"_id);
    agent.GetComponent<AIStateMachineComponent>().Tree = tree;

    scene->GetAISystem().OnStart();
    scene->GetAISystem().Update(0.1f);

    const auto& blackboard = agent.GetComponent<AIBlackboardComponent>();
    const Kerberos::UUID* targetId = blackboard.Get<Kerberos::UUID>(TargetEntity);
    ASSERT_NE(targetId, nullptr);
    EXPECT_EQ(*targetId, target.GetUUID());
    EXPECT_TRUE(blackboard.Get<bool>(HasLineOfSight) && *blackboard.Get<bool>(HasLineOfSight));

    target.GetComponent<TransformComponent>().Translation = glm::vec3(0.0f, 0.0f, 30.0f);
    scene->CalculateEntityTransform(target);
    scene->GetAISystem().Update(0.1f);

    EXPECT_EQ(blackboard.Get<Kerberos::UUID>(TargetEntity), nullptr);
    EXPECT_FALSE(blackboard.Get<bool>(HasLineOfSight) && *blackboard.Get<bool>(HasLineOfSight));
}

TEST_F(AITest, SceneSubtreeSerializationRoundTripsAuthoredAI)
{
    const Ref<Scene> scene = CreateRef<Scene>();
    Entity entity = CreateAgent(scene, "Serialized AI", glm::vec3(1.0f, 2.0f, 3.0f));
    entity.AddComponent<AIPerceptionComponent>();
    entity.AddComponent<AITargetableComponent>();

    auto& blackboard = entity.GetComponent<AIBlackboardComponent>();
    blackboard.AuthoredValues.push_back(NamedBlackboardValue{ "TargetEntity", Kerberos::UUID(99) });
    blackboard.AuthoredValues.push_back(NamedBlackboardValue{ "Aggro", true });
    blackboard.AuthoredValues.push_back(NamedBlackboardValue{ "Speed", 3.5f });
    blackboard.AuthoredValues.push_back(NamedBlackboardValue{ "Home", glm::vec3(1.0f, 2.0f, 3.0f) });

    auto& stateMachine = entity.GetComponent<AIStateMachineComponent>();
    stateMachine.TreeAsset = AssetHandle(kSerializedTreeAssetId);
    stateMachine.TreeName = "Enemy";

    auto& perception = entity.GetComponent<AIPerceptionComponent>();
    perception.SightRadius = 7.0f;
    perception.LoseRadius = 9.0f;
    perception.FactionId = 3;

    auto& controller = entity.GetComponent<AIControllerComponent>();
    controller.Enabled = false;

    entity.GetComponent<AITargetableComponent>().FactionId = 7;

    const std::string blob = SceneSerializer::SerializeEntitySubtree(scene, entity);
    ASSERT_FALSE(blob.empty());

    const Ref<Scene> restoredScene = CreateRef<Scene>();
    Entity restored = SceneSerializer::DeserializeEntitySubtree(restoredScene, blob);
    ASSERT_TRUE(restored);

    const auto& restoredBlackboard = restored.GetComponent<AIBlackboardComponent>();
    ASSERT_EQ(restoredBlackboard.AuthoredValues.size(), 4u);
    EXPECT_EQ(restoredBlackboard.AuthoredValues[0].Key, "TargetEntity");
    EXPECT_EQ(*std::get_if<Kerberos::UUID>(&restoredBlackboard.AuthoredValues[0].Value), Kerberos::UUID(99));
    EXPECT_TRUE(*std::get_if<bool>(&restoredBlackboard.AuthoredValues[1].Value));
    EXPECT_FLOAT_EQ(*std::get_if<float>(&restoredBlackboard.AuthoredValues[2].Value), 3.5f);

    const auto& restoredStateMachine = restored.GetComponent<AIStateMachineComponent>();
    EXPECT_EQ(restoredStateMachine.TreeAsset, AssetHandle(kSerializedTreeAssetId));
    EXPECT_EQ(restoredStateMachine.TreeName, "Enemy");
    EXPECT_FALSE(restoredStateMachine.IsInitialized);

    const auto& restoredPerception = restored.GetComponent<AIPerceptionComponent>();
    EXPECT_FLOAT_EQ(restoredPerception.SightRadius, 7.0f);
    EXPECT_FLOAT_EQ(restoredPerception.LoseRadius, 9.0f);
    EXPECT_EQ(restoredPerception.FactionId, 3u);

    EXPECT_FALSE(restored.GetComponent<AIControllerComponent>().Enabled);
    EXPECT_EQ(restored.GetComponent<AITargetableComponent>().FactionId, 7u);
}
