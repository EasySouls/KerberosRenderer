#include "Renderer/Animation/AnimationEvaluator.hpp"
#include "Scene/Components/AnimationComponents.hpp"
#include "Scene/Entity.hpp"
#include "Scene/Scene.hpp"

#include <gtest/gtest.h>

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include <array>
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

	Kerberos::Skeleton MakeTwoJointSkeleton()
	{
		return Kerberos::Skeleton(MakeTwoJointJoints());
	}

	Kerberos::AnimationTrack MakeTrack(const uint32_t joint,
	                                   const Kerberos::AnimationPath path,
	                                   const Kerberos::AnimationInterpolation interpolation,
	                                   std::vector<float> times,
	                                   std::vector<glm::vec4> values)
	{
		Kerberos::AnimationTrack track{};
		track.Joint = joint;
		track.Path = path;
		track.Interpolation = interpolation;
		track.Times = std::move(times);
		track.Values = std::move(values);
		return track;
	}

	void ExpectMatrixNear(const glm::mat4& actual, const glm::mat4& expected, const float epsilon = 1.0e-5f)
	{
		for (int column = 0; column < 4; ++column)
		{
			for (int row = 0; row < 4; ++row)
				EXPECT_NEAR(actual[column][row], expected[column][row], epsilon);
		}
	}

	TEST(AnimationEvaluatorTest, RestPoseProducesIdentitySkinMatrices)
	{
		const Kerberos::Skeleton skeleton = MakeTwoJointSkeleton();

		Kerberos::SkeletonPose pose;
		Kerberos::EvaluateRestPose(skeleton, pose);

		ASSERT_EQ(pose.Local.size(), 2u);
		ASSERT_EQ(pose.Global.size(), 2u);
		ASSERT_EQ(pose.SkinMatrices.size(), 2u);

		for (const glm::mat4& skinMatrix : pose.SkinMatrices)
			ExpectMatrixNear(skinMatrix, glm::mat4(1.0f));
	}

	TEST(AnimationEvaluatorTest, LinearTrackInterpolatesTranslation)
	{
		const Kerberos::Skeleton skeleton = MakeTwoJointSkeleton();
		const std::vector<Kerberos::AnimationTrack> tracks{
			MakeTrack(1,
			          Kerberos::AnimationPath::Translation,
			          Kerberos::AnimationInterpolation::Linear,
			          { 0.0f, 1.0f },
			          { glm::vec4(0.0f), glm::vec4(10.0f, 0.0f, 0.0f, 0.0f) }),
		};
		const Kerberos::AnimationClip clip("Linear", tracks, 1.0f);

		const Kerberos::ClipTrackCache cache = Kerberos::BuildClipTrackCache(skeleton, clip);
		ASSERT_EQ(cache.TracksByJoint.size(), 2u);
		EXPECT_EQ(cache.TracksByJoint[1][static_cast<size_t>(Kerberos::AnimationPath::Translation)], 0);

		Kerberos::SkeletonPose pose;
		Kerberos::EvaluatePose(skeleton, clip, cache, 0.5f, pose);

		EXPECT_NEAR(pose.Local[1].Translation.x, 5.0f, 1.0e-5f);
	}

	TEST(AnimationEvaluatorTest, StepTrackKeepsPreviousKey)
	{
		const Kerberos::Skeleton skeleton = MakeTwoJointSkeleton();
		const std::vector<Kerberos::AnimationTrack> tracks{
			MakeTrack(1,
			          Kerberos::AnimationPath::Translation,
			          Kerberos::AnimationInterpolation::Step,
			          { 0.0f, 1.0f },
			          { glm::vec4(0.0f), glm::vec4(10.0f, 0.0f, 0.0f, 0.0f) }),
		};
		const Kerberos::AnimationClip clip("Step", tracks, 1.0f);
		const Kerberos::ClipTrackCache cache = Kerberos::BuildClipTrackCache(skeleton, clip);

		Kerberos::SkeletonPose pose;
		Kerberos::EvaluatePose(skeleton, clip, cache, 0.5f, pose);

		EXPECT_NEAR(pose.Local[1].Translation.x, 0.0f, 1.0e-5f);
	}

	TEST(AnimationEvaluatorTest, CubicSplineScalesTangentsByInterval)
	{
		const Kerberos::Skeleton skeleton = MakeTwoJointSkeleton();

		Kerberos::AnimationTrack track = MakeTrack(1,
		                                           Kerberos::AnimationPath::Translation,
		                                           Kerberos::AnimationInterpolation::CubicSpline,
		                                           { 0.0f, 1.0f },
		                                           { glm::vec4(0.0f), glm::vec4(1.0f, 0.0f, 0.0f, 0.0f) });
		track.OutTangents = { glm::vec4(2.0f, 0.0f, 0.0f, 0.0f), glm::vec4(0.0f) };
		track.InTangents = { glm::vec4(0.0f), glm::vec4(0.0f) };

		const std::vector<Kerberos::AnimationTrack> tracks{ track };
		const Kerberos::AnimationClip clip("Cubic", tracks, 1.0f);
		const Kerberos::ClipTrackCache cache = Kerberos::BuildClipTrackCache(skeleton, clip);

		Kerberos::SkeletonPose pose;
		Kerberos::EvaluatePose(skeleton, clip, cache, 0.5f, pose);

		// h10 * dt * outTangent at t = 0.5 is 0.125 * 1.0 * 2.0, so x = 0.5 + 0.25.
		EXPECT_NEAR(pose.Local[1].Translation.x, 0.75f, 1.0e-5f);
	}

	TEST(AnimationEvaluatorTest, RotationSlerpUsesShortestPath)
	{
		const Kerberos::Skeleton skeleton = MakeTwoJointSkeleton();

		const glm::quat end = glm::angleAxis(glm::radians(350.0f), glm::vec3(0.0f, 0.0f, 1.0f));
		const std::vector<Kerberos::AnimationTrack> tracks{
			MakeTrack(0,
			          Kerberos::AnimationPath::Rotation,
			          Kerberos::AnimationInterpolation::Linear,
			          { 0.0f, 1.0f },
			          { glm::vec4(0.0f, 0.0f, 0.0f, 1.0f), glm::vec4(end.x, end.y, end.z, end.w) }),
		};
		const Kerberos::AnimationClip clip("Rotation", tracks, 1.0f);
		const Kerberos::ClipTrackCache cache = Kerberos::BuildClipTrackCache(skeleton, clip);

		Kerberos::SkeletonPose pose;
		Kerberos::EvaluatePose(skeleton, clip, cache, 0.5f, pose);

		const glm::quat expected = glm::angleAxis(glm::radians(-5.0f), glm::vec3(0.0f, 0.0f, 1.0f));
		EXPECT_NEAR(std::abs(glm::dot(pose.Local[0].Rotation, expected)), 1.0f, 1.0e-4f);
	}

	TEST(AnimationEvaluatorTest, TracksOutsideTheSkeletonAreIgnored)
	{
		const Kerberos::Skeleton skeleton = MakeTwoJointSkeleton();
		const std::vector<Kerberos::AnimationTrack> tracks{
			MakeTrack(9,
			          Kerberos::AnimationPath::Translation,
			          Kerberos::AnimationInterpolation::Linear,
			          { 0.0f, 1.0f },
			          { glm::vec4(0.0f), glm::vec4(10.0f, 0.0f, 0.0f, 0.0f) }),
		};
		const Kerberos::AnimationClip clip("OutOfRange", tracks, 1.0f);

		const Kerberos::ClipTrackCache cache = Kerberos::BuildClipTrackCache(skeleton, clip);
		EXPECT_EQ(cache.IgnoredTrackCount, 1u);

		Kerberos::SkeletonPose pose;
		Kerberos::EvaluatePose(skeleton, clip, cache, 0.5f, pose);

		EXPECT_NEAR(pose.Local[1].Translation.x, 1.0f, 1.0e-5f);
	}

	TEST(AnimationPlaybackTest, LoopWrapsForwardAndBackward)
	{
		Kerberos::PlaybackTime playback{ .Time = 1.5f, .IsPlaying = true };
		Kerberos::AdvancePlaybackTime(playback, 1.0f, true, 1.0f, 0.7f, true);
		EXPECT_NEAR(playback.Time, 0.2f, 1.0e-5f);
		EXPECT_TRUE(playback.IsPlaying);

		playback.Time = 0.2f;
		Kerberos::AdvancePlaybackTime(playback, 1.0f, true, -1.0f, 0.5f, true);
		EXPECT_NEAR(playback.Time, 0.7f, 1.0e-5f);
		EXPECT_TRUE(playback.IsPlaying);
	}

	TEST(AnimationPlaybackTest, NonLoopingStopsAtBothEnds)
	{
		Kerberos::PlaybackTime playback{ .Time = 0.9f, .IsPlaying = true };
		Kerberos::AdvancePlaybackTime(playback, 1.0f, false, 1.0f, 0.2f, true);
		EXPECT_NEAR(playback.Time, 1.0f, 1.0e-5f);
		EXPECT_FALSE(playback.IsPlaying);

		playback = { .Time = 0.1f, .IsPlaying = true };
		Kerberos::AdvancePlaybackTime(playback, 1.0f, false, -1.0f, 0.2f, true);
		EXPECT_NEAR(playback.Time, 0.0f, 1.0e-5f);
		EXPECT_FALSE(playback.IsPlaying);
	}

	TEST(AnimationPlaybackTest, ZeroDurationNeverProducesNaN)
	{
		Kerberos::PlaybackTime playback{ .Time = 5.0f, .IsPlaying = true };
		Kerberos::AdvancePlaybackTime(playback, 0.0f, true, 1.0f, 0.016f, true);

		EXPECT_FLOAT_EQ(playback.Time, 0.0f);
		EXPECT_FALSE(std::isnan(playback.Time));
		EXPECT_TRUE(playback.IsPlaying);
	}

	TEST(AnimationSceneTest, CopyStartsAnimatorsAccordingToPlayOnStart)
	{
		const Kerberos::Ref<Kerberos::Scene> editorScene = Kerberos::CreateRef<Kerberos::Scene>();

		Kerberos::Entity entity = editorScene->CreateEntity("Animated");
		auto& animator = entity.AddComponent<Kerberos::AnimatorComponent>();
		animator.PlayOnStart = true;
		animator.IsPlaying = false;
		animator.CurrentTime = 3.0f;
		animator.ActiveClip = 0;
		animator.AnimationClips.push_back(Kerberos::AssetHandle::Invalid());

		const Kerberos::Ref<Kerberos::Scene> runtimeScene = Kerberos::Scene::Copy(editorScene);
		const Kerberos::Entity copiedEntity = runtimeScene->GetEntityByUUID(entity.GetUUID());

		ASSERT_TRUE(copiedEntity);
		const auto& copiedAnimator = copiedEntity.GetComponent<Kerberos::AnimatorComponent>();
		EXPECT_TRUE(copiedAnimator.PlayOnStart);
		EXPECT_TRUE(copiedAnimator.IsPlaying);
		EXPECT_FLOAT_EQ(copiedAnimator.CurrentTime, 0.0f);
		EXPECT_TRUE(copiedAnimator.LoadedClips.empty());

		runtimeScene->StopAnimators();
		EXPECT_FALSE(copiedAnimator.IsPlaying);

		runtimeScene->StartAnimators();
		EXPECT_TRUE(copiedAnimator.IsPlaying);
	}

	TEST(AnimationRuntimeTest, EvaluatesRestPoseAndTracksSkeletonInstances)
	{
		const Kerberos::Ref<Kerberos::Scene> scene = Kerberos::CreateRef<Kerberos::Scene>();
		Kerberos::Entity entity = scene->CreateEntity("Skeleton");

		auto& skeletonComponent = entity.AddComponent<Kerberos::SkeletonComponent>();
		skeletonComponent.Skeleton = Kerberos::CreateRef<Kerberos::Skeleton>(MakeTwoJointJoints());

		auto& runtime = scene->GetAnimationRuntime();
		runtime.AdvanceClocks(0.0f, false);
		runtime.EvaluatePose();

		const Kerberos::SkeletonPose* pose = runtime.FindPose(entity.GetUUID());
		ASSERT_NE(pose, nullptr);
		ASSERT_EQ(pose->SkinMatrices.size(), 2u);

		for (const glm::mat4& skinMatrix : pose->SkinMatrices)
			ExpectMatrixNear(skinMatrix, glm::mat4(1.0f));

		ASSERT_EQ(runtime.GetInstances().size(), 1u);
		const uint64_t poseVersion = runtime.GetInstances().front().PoseVersion;
		EXPECT_GE(poseVersion, 1u);

		// A rest-pose skeleton does not need a new pose version when nothing changed.
		runtime.AdvanceClocks(0.0f, false);
		runtime.EvaluatePose();
		EXPECT_EQ(runtime.GetInstances().front().PoseVersion, poseVersion);

		glm::mat4 jointGlobal(1.0f);
		ASSERT_TRUE(runtime.TryGetJointGlobalTransform(entity.GetUUID(), 1, jointGlobal));
		EXPECT_NEAR(jointGlobal[3].x, 1.0f, 1.0e-5f);

		scene->DestroyEntity(entity);
		runtime.AdvanceClocks(0.0f, false);
		EXPECT_TRUE(runtime.GetInstances().empty());
	}

	TEST(AnimationRuntimeTest, SocketFollowsJointAndChildTransforms)
	{
		const Kerberos::Ref<Kerberos::Scene> scene = Kerberos::CreateRef<Kerberos::Scene>();

		Kerberos::Entity skeletonEntity = scene->CreateEntity("Skeleton");
		skeletonEntity.GetComponent<Kerberos::TransformComponent>().Translation = glm::vec3(5.0f, 0.0f, 0.0f);
		auto& skeletonComponent = skeletonEntity.AddComponent<Kerberos::SkeletonComponent>();
		skeletonComponent.Skeleton = Kerberos::CreateRef<Kerberos::Skeleton>(MakeTwoJointJoints());

		Kerberos::Entity swordEntity = scene->CreateEntity("Sword");
		swordEntity.GetComponent<Kerberos::TransformComponent>().Translation = glm::vec3(100.0f);
		auto& socket = swordEntity.AddComponent<Kerberos::SocketComponent>();
		socket.SkeletonEntity = skeletonEntity.GetUUID();
		socket.JointName = "Child";

		const Kerberos::Entity socketChild = scene->CreateEntity("SocketChild");
		socketChild.GetComponent<Kerberos::TransformComponent>().Translation = glm::vec3(0.0f, 0.0f, 1.0f);
		scene->SetParent(socketChild, swordEntity, false);

		scene->CalculateEntityTransforms();

		auto& runtime = scene->GetAnimationRuntime();
		runtime.AdvanceClocks(0.0f, false);
		runtime.EvaluatePose();

		const glm::mat4& socketWorld = swordEntity.GetComponent<Kerberos::TransformComponent>().WorldTransform;
		EXPECT_NEAR(socketWorld[3].x, 6.0f, 1.0e-5f);
		EXPECT_NEAR(socketWorld[3].z, 0.0f, 1.0e-5f);

		// Authored local transform is not overwritten, only the derived world transform is.
		EXPECT_NEAR(swordEntity.GetComponent<Kerberos::TransformComponent>().Translation.x, 100.0f, 1.0e-5f);

		const glm::mat4& childWorld = socketChild.GetComponent<Kerberos::TransformComponent>().WorldTransform;
		EXPECT_NEAR(childWorld[3].x, 6.0f, 1.0e-5f);
		EXPECT_NEAR(childWorld[3].z, 1.0f, 1.0e-5f);
	}

	TEST(AnimationSceneTest, CopyDoesNotStartAnimatorsWithoutPlayOnStart)
	{
		const Kerberos::Ref<Kerberos::Scene> editorScene = Kerberos::CreateRef<Kerberos::Scene>();

		Kerberos::Entity entity = editorScene->CreateEntity("Animated");
		auto& animator = entity.AddComponent<Kerberos::AnimatorComponent>();
		animator.PlayOnStart = false;
		animator.IsPlaying = true;
		animator.CurrentTime = 2.0f;

		const Kerberos::Ref<Kerberos::Scene> runtimeScene = Kerberos::Scene::Copy(editorScene);
		const Kerberos::Entity copiedEntity = runtimeScene->GetEntityByUUID(entity.GetUUID());

		ASSERT_TRUE(copiedEntity);
		const auto& copiedAnimator = copiedEntity.GetComponent<Kerberos::AnimatorComponent>();
		EXPECT_FALSE(copiedAnimator.IsPlaying);
		EXPECT_FLOAT_EQ(copiedAnimator.CurrentTime, 0.0f);
	}

}
