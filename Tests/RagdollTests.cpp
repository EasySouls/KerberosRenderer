#include "Scene/Components/AnimationComponents.hpp"
#include "Scene/Components/PhysicsComponents.hpp"
#include "Scene/Entity.hpp"
#include "Scene/Scene.hpp"
#include "Serialization/SceneSerializer.hpp"

#include <gtest/gtest.h>

#include <glm/gtc/quaternion.hpp>

#include <vector>

import Kerberos;

namespace
{
	std::vector<Kerberos::SkeletonJoint> MakeTwoJointJoints()
	{
		std::vector<Kerberos::SkeletonJoint> joints(2);

		joints[0].Name = "Root";
		joints[0].Parent = -1;
		joints[0].RestTranslation = glm::vec3(0.0f);
		joints[0].RestRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		joints[0].RestScale = glm::vec3(1.0f);
		joints[0].InverseBind = glm::mat4(1.0f);

		joints[1].Name = "Child";
		joints[1].Parent = 0;
		joints[1].RestTranslation = glm::vec3(1.0f, 0.0f, 0.0f);
		joints[1].RestRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		joints[1].RestScale = glm::vec3(1.0f);

		const glm::mat4 childGlobal = glm::translate(glm::mat4(1.0f), joints[1].RestTranslation);
		joints[1].InverseBind = glm::inverse(childGlobal);

		return joints;
	}

	Kerberos::Ref<Kerberos::AnimationClip> MakeTranslationClip(const float start, const float end)
	{
		Kerberos::AnimationTrack track{};
		track.Joint = 1;
		track.Path = Kerberos::AnimationPath::Translation;
		track.Interpolation = Kerberos::AnimationInterpolation::Linear;
		track.Times = { 0.0f, 1.0f };
		track.Values = {
			glm::vec4(start, 0.0f, 0.0f, 0.0f),
			glm::vec4(end, 0.0f, 0.0f, 0.0f),
		};

		const Kerberos::Ref<Kerberos::AnimationClip> clip = Kerberos::CreateRef<Kerberos::AnimationClip>(
			"Move", std::vector<Kerberos::AnimationTrack>{ track }, 1.0f);
		clip->GetHandle() = Kerberos::AssetHandle::Invalid();
		return clip;
	}

	struct RagdollFixture
	{
		Kerberos::Ref<Kerberos::Scene> Scene;
		Kerberos::Entity SkeletonEntity;
		Kerberos::Entity RootJoint;
		Kerberos::Entity ChildJoint;

		Kerberos::UUID RagdollUUID;
	};

	RagdollFixture MakeRagdollScene(const float blendDuration = 0.1f, const bool startInRagdoll = false)
	{
		RagdollFixture fixture{};
		fixture.Scene = Kerberos::CreateRef<Kerberos::Scene>();

		fixture.SkeletonEntity = fixture.Scene->CreateEntity("Skeleton");
		auto& skeletonComponent = fixture.SkeletonEntity.AddComponent<Kerberos::SkeletonComponent>();
		skeletonComponent.Skeleton = Kerberos::CreateRef<Kerberos::Skeleton>(MakeTwoJointJoints());

		fixture.RootJoint = fixture.Scene->CreateEntity("RootJoint");
		fixture.ChildJoint = fixture.Scene->CreateEntity("ChildJoint");
		skeletonComponent.JointEntities = {
			fixture.RootJoint.GetUUID(),
			fixture.ChildJoint.GetUUID(),
		};

		auto& ragdoll = fixture.SkeletonEntity.AddComponent<Kerberos::RagdollComponent>();
		ragdoll.BlendDuration = blendDuration;
		ragdoll.StartInRagdoll = startInRagdoll;
		ragdoll.JointRadiusScale = 1.0f;

		fixture.RagdollUUID = fixture.SkeletonEntity.GetUUID();
		return fixture;
	}

	void Step(const RagdollFixture& fixture, const float deltaTime)
	{
		auto& runtime = fixture.Scene->GetAnimationRuntime();
		fixture.Scene->CalculateEntityTransforms();
		runtime.AdvanceClocks(deltaTime, false);
		runtime.EvaluatePose();

		auto& ragdollSystem = fixture.Scene->GetRagdollSystem();
		ragdollSystem.PreparePhysics(deltaTime);
		fixture.Scene->GetPhysicsSystem().Update(deltaTime);
		ragdollSystem.ReadPhysicsPose(deltaTime);

		runtime.FinalizePose(deltaTime);
	}

	void StartSimulation(const RagdollFixture& fixture)
	{
		fixture.Scene->OnSimulationStart();
		fixture.Scene->CalculateEntityTransforms();
		fixture.Scene->GetAnimationRuntime().AdvanceClocks(0.0f, false);
		fixture.Scene->GetAnimationRuntime().EvaluatePose();
		fixture.Scene->GetRagdollSystem().PreparePhysics(0.0f);
	}
}

TEST(RagdollTest, CreatesKinematicBodiesWhileAnimated)
{
	const RagdollFixture fixture = MakeRagdollScene();
	StartSimulation(fixture);

	const auto& ragdollSystem = fixture.Scene->GetRagdollSystem();
	EXPECT_EQ(ragdollSystem.GetBodyCount(fixture.RagdollUUID), 2u);
	EXPECT_FALSE(ragdollSystem.IsRagdoll(fixture.RagdollUUID));
	EXPECT_EQ(ragdollSystem.GetState(fixture.RagdollUUID), Kerberos::RagdollState::Animated);
	EXPECT_FLOAT_EQ(ragdollSystem.GetBlendWeight(fixture.RagdollUUID), 0.0f);

	fixture.Scene->OnSimulationStop();
}

TEST(RagdollTest, EnableRagdollSimulatesAndPublishesJointTransforms)
{
	const RagdollFixture fixture = MakeRagdollScene(0.05f);
	StartSimulation(fixture);

	auto& ragdollSystem = fixture.Scene->GetRagdollSystem();
	ragdollSystem.EnableRagdoll(fixture.RagdollUUID);

	for (int frame = 0; frame < 120; ++frame)
		Step(fixture, 1.0f / 60.0f);

	EXPECT_EQ(ragdollSystem.GetState(fixture.RagdollUUID), Kerberos::RagdollState::Ragdoll);
	EXPECT_FLOAT_EQ(ragdollSystem.GetBlendWeight(fixture.RagdollUUID), 1.0f);

	const Kerberos::SkeletonPose* pose =
		fixture.Scene->GetAnimationRuntime().FindPose(fixture.RagdollUUID);
	ASSERT_NE(pose, nullptr);
	ASSERT_GT(pose->Local.size(), 1u);
	EXPECT_LT(pose->Local[0].Translation.y, -0.5f);

	const glm::mat4& rootWorld =
		fixture.RootJoint.GetComponent<Kerberos::TransformComponent>().WorldTransform;
	const glm::mat4& childWorld =
		fixture.ChildJoint.GetComponent<Kerberos::TransformComponent>().WorldTransform;
	EXPECT_LT(rootWorld[3].y, -0.5f);
	EXPECT_LT(childWorld[3].y, -0.5f);

	fixture.Scene->OnSimulationStop();
}

TEST(RagdollTest, DisableRagdollBlendsBackToAnimation)
{
	RagdollFixture fixture = MakeRagdollScene(0.05f);
	StartSimulation(fixture);

	auto& ragdollSystem = fixture.Scene->GetRagdollSystem();
	ragdollSystem.EnableRagdoll(fixture.RagdollUUID);

	for (int frame = 0; frame < 60; ++frame)
		Step(fixture, 1.0f / 60.0f);

	ASSERT_EQ(ragdollSystem.GetState(fixture.RagdollUUID), Kerberos::RagdollState::Ragdoll);

	ragdollSystem.DisableRagdoll(fixture.RagdollUUID);

	for (int frame = 0; frame < 20; ++frame)
		Step(fixture, 1.0f / 60.0f);

	EXPECT_EQ(ragdollSystem.GetState(fixture.RagdollUUID), Kerberos::RagdollState::Animated);
	EXPECT_FLOAT_EQ(ragdollSystem.GetBlendWeight(fixture.RagdollUUID), 0.0f);

	const Kerberos::SkeletonPose* pose =
		fixture.Scene->GetAnimationRuntime().FindPose(fixture.RagdollUUID);
	ASSERT_NE(pose, nullptr);
	EXPECT_NEAR(pose->Local[0].Translation.y, 0.0f, 1.0e-3f);

	const glm::mat4& rootWorld =
		fixture.RootJoint.GetComponent<Kerberos::TransformComponent>().WorldTransform;
	EXPECT_NEAR(rootWorld[3].y, 0.0f, 1.0e-3f);

	fixture.Scene->OnSimulationStop();
}

TEST(RagdollTest, KinematicProxiesFollowAnimatedPose)
{
	RagdollFixture fixture = MakeRagdollScene();

	auto& animator = fixture.SkeletonEntity.AddComponent<Kerberos::AnimatorComponent>();
	animator.AnimationClips = { Kerberos::AssetHandle::Invalid() };
	animator.LoadedClips = { MakeTranslationClip(1.0f, 11.0f) };
	animator.ActiveClip = 0;
	animator.IsPlaying = true;
	animator.IsLooping = false;
	animator.PlayOnStart = true;

	StartSimulation(fixture);

	// Advance the clip half-way: the child local translation is 6. The rigid proxy keeps the
	// creation-time capsule length but its joint end must track the animated joint.
	fixture.Scene->CalculateEntityTransforms();
	fixture.Scene->GetAnimationRuntime().AdvanceClocks(0.5f, false);
	fixture.Scene->GetAnimationRuntime().EvaluatePose();
	fixture.Scene->GetRagdollSystem().PreparePhysics(0.0f);

	glm::vec3 jointPosition(0.0f);
	glm::quat jointRotation(1.0f, 0.0f, 0.0f, 0.0f);
	ASSERT_TRUE(fixture.Scene->GetRagdollSystem().TryGetJointWorldTransform(
		fixture.RagdollUUID, 1, jointPosition, jointRotation));
	EXPECT_NEAR(jointPosition.x, 6.0f, 1.0e-3f);

	fixture.Scene->OnSimulationStop();
}

TEST(RagdollTest, SerializesAuthoredSetup)
{
	const RagdollFixture fixture = MakeRagdollScene(0.4f, true);

	auto& ragdoll = fixture.SkeletonEntity.GetComponent<Kerberos::RagdollComponent>();
	ragdoll.SkeletonEntity = fixture.SkeletonEntity.GetUUID();
	ragdoll.JointRadiusScale = 1.75f;

	const std::string blob =
		Kerberos::SceneSerializer::SerializeEntitySubtree(fixture.Scene, fixture.SkeletonEntity);

	const Kerberos::Ref<Kerberos::Scene> restoredScene = Kerberos::CreateRef<Kerberos::Scene>();
	const Kerberos::Entity restored =
		Kerberos::SceneSerializer::DeserializeEntitySubtree(restoredScene, blob);

	ASSERT_TRUE(restored);
	ASSERT_TRUE(restored.HasComponent<Kerberos::RagdollComponent>());

	const auto& restoredRagdoll = restored.GetComponent<Kerberos::RagdollComponent>();
	EXPECT_EQ(restoredRagdoll.SkeletonEntity, ragdoll.SkeletonEntity);
	EXPECT_FLOAT_EQ(restoredRagdoll.BlendDuration, 0.4f);
	EXPECT_TRUE(restoredRagdoll.StartInRagdoll);
	EXPECT_FLOAT_EQ(restoredRagdoll.JointRadiusScale, 1.75f);
}

TEST(RagdollTest, StartInRagdollSkipsAnimationBlendIn)
{
	const RagdollFixture fixture = MakeRagdollScene(0.0f, true);
	StartSimulation(fixture);

	const auto& ragdollSystem = fixture.Scene->GetRagdollSystem();
	EXPECT_EQ(ragdollSystem.GetState(fixture.RagdollUUID), Kerberos::RagdollState::Ragdoll);
	EXPECT_FLOAT_EQ(ragdollSystem.GetBlendWeight(fixture.RagdollUUID), 1.0f);

	fixture.Scene->OnSimulationStop();
}
