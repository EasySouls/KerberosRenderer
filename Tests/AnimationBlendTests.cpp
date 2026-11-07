#include "Renderer/Animation/AnimationEvaluator.hpp"
#include "Scene/Components/AnimationComponents.hpp"
#include "Scene/Entity.hpp"
#include "Scene/Scene.hpp"

#include <gtest/gtest.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

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

	Kerberos::AnimationTrack MakeTranslationTrack(const float start, const float end)
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
		return track;
	}

	Kerberos::Ref<Kerberos::AnimationClip> MakeTranslationClip(const char* name,
	                                                           const float start,
	                                                           const float end,
	                                                           std::vector<Kerberos::AnimationEvent> events = {})
	{
		const std::vector<Kerberos::AnimationTrack> tracks{ MakeTranslationTrack(start, end) };
		const Kerberos::Ref<Kerberos::AnimationClip> clip =
			Kerberos::CreateRef<Kerberos::AnimationClip>(name, tracks, 1.0f,
			                                             std::vector<Kerberos::AnimationMarker>{},
			                                             std::move(events));

		// In-memory clips have no asset registration; mark them so the runtime keeps them when
		// resolving the animator's (invalid) asset handles.
		clip->GetHandle() = Kerberos::AssetHandle::Invalid();
		return clip;
	}

	void ExpectPoseJointX(const Kerberos::SkeletonPose* pose, const float expected, const float epsilon = 1.0e-4f)
	{
		ASSERT_NE(pose, nullptr);
		ASSERT_GT(pose->Local.size(), 1u);
		EXPECT_NEAR(pose->Local[1].Translation.x, expected, epsilon);
	}
}

TEST(AnimationPoseBlendTest, BlendEndpointsAndMidpoint)
{
	Kerberos::SkeletonPose a;
	a.Local = {
		Kerberos::JointPose{ .Translation = glm::vec3(0.0f), .Rotation = glm::quat(1, 0, 0, 0), .Scale = glm::vec3(1.0f) },
		Kerberos::JointPose{ .Translation = glm::vec3(0.0f), .Rotation = glm::quat(1, 0, 0, 0), .Scale = glm::vec3(1.0f) },
	};

	Kerberos::SkeletonPose b;
	b.Local = {
		Kerberos::JointPose{ .Translation = glm::vec3(10.0f, 0.0f, 0.0f), .Rotation = glm::quat(1, 0, 0, 0), .Scale = glm::vec3(3.0f) },
		Kerberos::JointPose{
			.Translation = glm::vec3(0.0f),
			.Rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
			.Scale = glm::vec3(1.0f),
		},
	};

	Kerberos::SkeletonPose out;

	Kerberos::BlendPose(a, b, 0.0f, out);
	EXPECT_NEAR(out.Local[0].Translation.x, 0.0f, 1.0e-5f);

	Kerberos::BlendPose(a, b, 1.0f, out);
	EXPECT_NEAR(out.Local[0].Translation.x, 10.0f, 1.0e-5f);
	EXPECT_NEAR(out.Local[0].Scale.x, 3.0f, 1.0e-5f);

	Kerberos::BlendPose(a, b, 0.5f, out);
	EXPECT_NEAR(out.Local[0].Translation.x, 5.0f, 1.0e-5f);
	EXPECT_NEAR(out.Local[0].Scale.x, 2.0f, 1.0e-5f);

	const glm::quat expectedRotation = glm::angleAxis(glm::radians(45.0f), glm::vec3(0.0f, 0.0f, 1.0f));
	EXPECT_NEAR(std::abs(glm::dot(out.Local[1].Rotation, expectedRotation)), 1.0f, 1.0e-4f);
}

TEST(AnimationPoseBlendTest, MismatchedJointCountsFallBackToDominantPose)
{
	Kerberos::SkeletonPose a;
	a.Local = { Kerberos::JointPose{ .Translation = glm::vec3(1.0f) } };

	Kerberos::SkeletonPose b;
	b.Local = { Kerberos::JointPose{ .Translation = glm::vec3(7.0f) },
	            Kerberos::JointPose{ .Translation = glm::vec3(8.0f) } };

	Kerberos::SkeletonPose out;

	Kerberos::BlendPose(a, b, 0.25f, out);
	ASSERT_EQ(out.Local.size(), 1u);
	EXPECT_NEAR(out.Local[0].Translation.x, 1.0f, 1.0e-5f);

	Kerberos::BlendPose(a, b, 0.75f, out);
	ASSERT_EQ(out.Local.size(), 2u);
	EXPECT_NEAR(out.Local[0].Translation.x, 7.0f, 1.0e-5f);
}

TEST(AnimationPoseBlendTest, AccumulateUsesNormalizedWeightedAverage)
{
	Kerberos::SkeletonPose first;
	first.Local = { Kerberos::JointPose{ .Translation = glm::vec3(0.0f), .Scale = glm::vec3(1.0f) } };

	Kerberos::SkeletonPose second;
	second.Local = { Kerberos::JointPose{ .Translation = glm::vec3(10.0f), .Scale = glm::vec3(3.0f) } };

	Kerberos::SkeletonPose accumulated;
	float accumulatedWeight = 0.0f;

	Kerberos::AccumulatePose(first, 1.0f, accumulated, accumulatedWeight);
	Kerberos::AccumulatePose(second, 1.0f, accumulated, accumulatedWeight);

	EXPECT_FLOAT_EQ(accumulatedWeight, 2.0f);
	EXPECT_NEAR(accumulated.Local[0].Translation.x, 5.0f, 1.0e-5f);
	EXPECT_NEAR(accumulated.Local[0].Scale.x, 2.0f, 1.0e-5f);
}

TEST(AnimationCrossFadeTest, WeightHandlesSnapAndClamp)
{
	EXPECT_FLOAT_EQ(Kerberos::ComputeCrossFadeWeight(0.0f, 1.0f), 0.0f);
	EXPECT_FLOAT_EQ(Kerberos::ComputeCrossFadeWeight(0.5f, 1.0f), 0.5f);
	EXPECT_FLOAT_EQ(Kerberos::ComputeCrossFadeWeight(1.5f, 1.0f), 1.0f);
	EXPECT_FLOAT_EQ(Kerberos::ComputeCrossFadeWeight(-1.0f, 1.0f), 0.0f);
	EXPECT_FLOAT_EQ(Kerberos::ComputeCrossFadeWeight(0.5f, 0.0f), 1.0f);
}

TEST(AnimationCrossFadeTest, RuntimeBlendsTwoClipsByFadeWeight)
{
	const Kerberos::Ref<Kerberos::Scene> scene = Kerberos::CreateRef<Kerberos::Scene>();
	Kerberos::Entity entity = scene->CreateEntity("Skeleton");

	auto& skeletonComponent = entity.AddComponent<Kerberos::SkeletonComponent>();
	skeletonComponent.Skeleton = Kerberos::CreateRef<Kerberos::Skeleton>(MakeTwoJointJoints());

	auto& animator = entity.AddComponent<Kerberos::AnimatorComponent>();
	const Kerberos::Ref<Kerberos::AnimationClip> clipA = MakeTranslationClip("A", 0.0f, 10.0f);
	const Kerberos::Ref<Kerberos::AnimationClip> clipB = MakeTranslationClip("B", 100.0f, 110.0f);
	const Kerberos::Ref<Kerberos::AnimationClip> clipC = MakeTranslationClip("C", 200.0f, 210.0f);

	animator.AnimationClips = {
		Kerberos::AssetHandle::Invalid(),
		Kerberos::AssetHandle::Invalid(),
		Kerberos::AssetHandle::Invalid(),
	};
	animator.LoadedClips = { clipA, clipB, clipC };
	animator.ActiveClip = 0;
	animator.CurrentTime = 0.0f;
	animator.IsPlaying = true;
	animator.IsLooping = false;

	auto& runtime = scene->GetAnimationRuntime();
	runtime.AdvanceClocks(0.0f, false);
	runtime.EvaluatePose();

	ExpectPoseJointX(runtime.FindPose(entity.GetUUID()), 0.0f);

	// A script requests a one-second cross-fade into B. Evaluation applies it immediately so the
	// pose does not stay on the old clip for an extra frame.
	animator.PendingCrossFadeClip = 1;
	animator.PendingCrossFadeDuration = 1.0f;
	animator.PendingCrossFadeRestart = true;
	animator.ActiveClip = 1;
	animator.CurrentTime = 0.0f;
	animator.PlaybackRevision++;

	runtime.EvaluatePose();
	ExpectPoseJointX(runtime.FindPose(entity.GetUUID()), 0.0f);

	// Half-way: A is at 5, B is at 105, so the blended child translation is 55.
	runtime.AdvanceClocks(0.5f, false);
	runtime.EvaluatePose();
	ExpectPoseJointX(runtime.FindPose(entity.GetUUID()), 55.0f);

	// Interrupting the fade must start from the current blended pose (55), not from A.
	animator.PendingCrossFadeClip = 2;
	animator.PendingCrossFadeDuration = 1.0f;
	animator.PendingCrossFadeRestart = true;
	animator.ActiveClip = 2;
	animator.CurrentTime = 0.0f;
	animator.PlaybackRevision++;

	runtime.EvaluatePose();
	ExpectPoseJointX(runtime.FindPose(entity.GetUUID()), 55.0f);

	// Half a second into the new fade: snapshot 55 and C at 205 blend evenly to 130.
	runtime.AdvanceClocks(0.5f, false);
	runtime.EvaluatePose();
	ExpectPoseJointX(runtime.FindPose(entity.GetUUID()), 130.0f);

	// The fade completes and the outgoing snapshot is dropped.
	runtime.AdvanceClocks(0.5f, false);
	runtime.EvaluatePose();
	ExpectPoseJointX(runtime.FindPose(entity.GetUUID()), 210.0f);
}

TEST(AnimationCrossFadeTest, OutgoingClipEventsRespectFadeWeight)
{
	const Kerberos::Ref<Kerberos::Scene> scene = Kerberos::CreateRef<Kerberos::Scene>();
	Kerberos::Entity entity = scene->CreateEntity("Skeleton");

	auto& skeletonComponent = entity.AddComponent<Kerberos::SkeletonComponent>();
	skeletonComponent.Skeleton = Kerberos::CreateRef<Kerberos::Skeleton>(MakeTwoJointJoints());

	Kerberos::AnimationEvent event{};
	event.Time = 0.5f;
	event.Name = "Weighted";
	event.MinTriggerWeight = 0.6f;

	auto& animator = entity.AddComponent<Kerberos::AnimatorComponent>();
	const Kerberos::Ref<Kerberos::AnimationClip> clipA =
		MakeTranslationClip("A", 0.0f, 10.0f, { event });
	const Kerberos::Ref<Kerberos::AnimationClip> clipB = MakeTranslationClip("B", 100.0f, 110.0f);

	animator.AnimationClips = { Kerberos::AssetHandle::Invalid(), Kerberos::AssetHandle::Invalid() };
	animator.LoadedClips = { clipA, clipB };
	animator.ActiveClip = 0;
	animator.CurrentTime = 0.0f;
	animator.IsPlaying = true;
	animator.IsLooping = false;

	auto& runtime = scene->GetAnimationRuntime();
	runtime.AdvanceClocks(0.0f, false);
	runtime.EvaluatePose();

	animator.PendingCrossFadeClip = 1;
	animator.PendingCrossFadeDuration = 1.0f;
	animator.PendingCrossFadeRestart = true;
	animator.ActiveClip = 1;
	animator.CurrentTime = 0.0f;
	animator.PlaybackRevision++;
	runtime.EvaluatePose();

	// Crossing the event at 0.5 with the outgoing clip at weight 0.5 must not fire a 0.6-threshold
	// point event.
	runtime.AdvanceClocks(0.5f, false);
	EXPECT_TRUE(runtime.GetPendingDispatches().empty());
}
