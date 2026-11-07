#include "Assets/Formats/AnimationFormat.hpp"
#include "Assets/Formats/NativeAssetFormat.hpp"
#include "Assets/Formats/NativeAssetSerializer.hpp"
#include "Assets/Formats/SkeletonFormat.hpp"
#include "Assets/Importers/GLTFSceneImporter.hpp"
#include "Renderer/Animation/AnimationClip.hpp"
#include "Renderer/Animation/AnimationEvaluator.hpp"
#include "Renderer/Animation/Skeleton.hpp"
#include "Renderer/Animation/SkeletalMesh.hpp"

#include <gtest/gtest.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <vector>

import Kerberos;

namespace
{
	std::filesystem::path FindArtifact(const std::filesystem::path& root, const std::string& extension)
	{
		std::error_code error;
		for (const auto& entry : std::filesystem::recursive_directory_iterator(root, error))
		{
			if (error)
				break;
			if (entry.is_regular_file(error) && entry.path().extension() == extension)
				return entry.path();
		}
		return {};
	}

	class GLTFImportTest : public testing::Test
	{
	protected:
		static void SetUpTestSuite()
		{
			Kerberos::Log::Init();
		}

		static void TearDownTestSuite()
		{
			Kerberos::Log::Shutdown();
		}

		void SetUp() override
		{
			m_Output = std::filesystem::temp_directory_path() / "kbr_gltf_import_test";
			std::error_code error;
			std::filesystem::remove_all(m_Output, error);
			std::filesystem::create_directories(m_Output, error);
		}

		void TearDown() override
		{
			std::error_code error;
			std::filesystem::remove_all(m_Output, error);
		}

		std::filesystem::path m_Output;
	};
}

TEST_F(GLTFImportTest, ImportsCesiumManSkeletonAnimationAndSkinnedMesh)
{
	const std::filesystem::path source =
		std::filesystem::path{ KBR_SOURCE_DIR } / "KerberosEditor" / "Assets" / "Models"
		/ "CesiumMan" / "glTF" / "CesiumMan.gltf";
	ASSERT_TRUE(std::filesystem::exists(source)) << source.string();

	Kerberos::GltfSceneManifest manifest;
	ASSERT_TRUE(Kerberos::GLTFSceneImporter::Import(source, m_Output, "CesiumMan", &manifest));

	ASSERT_EQ(manifest.Skins.size(), 1u);
	EXPECT_EQ(manifest.Skins[0].JointNodes.size(), 19u);
	EXPECT_FALSE(manifest.Skins[0].AnimationKeys.empty());

	const auto skeletonPath = FindArtifact(m_Output, ".kbrskeleton");
	const auto animationPath = FindArtifact(m_Output, ".kbranimation");
	const auto skinnedMeshPath = FindArtifact(m_Output, ".kbrskinnedmesh");
	ASSERT_FALSE(skeletonPath.empty());
	ASSERT_FALSE(animationPath.empty());
	ASSERT_FALSE(skinnedMeshPath.empty());

	Kerberos::SkeletonFile skeleton;
	ASSERT_TRUE(Kerberos::NativeAssetSerializer::DeserializeSkeleton(skeletonPath, skeleton));
	ASSERT_EQ(skeleton.Joints.size(), 19u);
	for (size_t i = 0; i < skeleton.Joints.size(); ++i)
	{
		EXPECT_TRUE(skeleton.Joints[i].ParentJoint == -1
			|| static_cast<size_t>(skeleton.Joints[i].ParentJoint) < i);
		EXPECT_GE(skeleton.Joints[i].SourceNode, 0);
	}

	// Rest-pose globals multiplied by inverse-bind matrices should be identity.
	std::vector<glm::mat4> globals(skeleton.Joints.size(), glm::mat4(1.0f));
	for (size_t i = 0; i < skeleton.Joints.size(); ++i)
	{
		const auto& joint = skeleton.Joints[i];
		const glm::mat4 local =
			glm::translate(glm::mat4(1.0f), joint.RestTranslation) *
			glm::toMat4(joint.RestRotation) *
			glm::scale(glm::mat4(1.0f), joint.RestScale);
		globals[i] = (joint.ParentJoint >= 0 ? globals[static_cast<size_t>(joint.ParentJoint)] : glm::mat4(1.0f)) * local;
		const glm::mat4 bindPose = globals[i] * joint.InverseBind;
		for (int column = 0; column < 4; ++column)
			for (int row = 0; row < 4; ++row)
				EXPECT_NEAR(bindPose[column][row], column == row ? 1.0f : 0.0f, 1e-3f);
	}

	Kerberos::AnimationClipFile animation;
	ASSERT_TRUE(Kerberos::NativeAssetSerializer::DeserializeAnimationClip(animationPath, animation));
	EXPECT_GT(animation.DurationSeconds, 0.0f);
	EXPECT_FALSE(animation.Tracks.empty());
	EXPECT_EQ(animation.JointCount, 19u);
	for (const auto& track : animation.Tracks)
	{
		EXPECT_LT(track.JointIndex, static_cast<int32_t>(animation.JointCount));
		EXPECT_EQ(track.Times.size(), track.Values.size());
		if (track.Interpolation == Kerberos::AnimationClipInterpolation::CubicSpline)
		{
			EXPECT_EQ(track.InTangents.size(), track.Values.size());
			EXPECT_EQ(track.OutTangents.size(), track.Values.size());
		}
	}

	Kerberos::NativeMeshPayload skinnedMesh;
	ASSERT_TRUE(Kerberos::NativeAssetSerializer::DeserializeSkeletalMesh(skinnedMeshPath, skinnedMesh));
	EXPECT_EQ(skinnedMesh.VertexStride, sizeof(Kerberos::SkinVertex));
	EXPECT_GT(skinnedMesh.VertexData.size(), 0u);
	EXPECT_FALSE(skinnedMesh.Indices.empty());
}

TEST_F(GLTFImportTest, EvaluatesImportedCesiumManAnimation)
{
	const std::filesystem::path source =
		std::filesystem::path{ KBR_SOURCE_DIR } / "KerberosEditor" / "Assets" / "Models"
		/ "CesiumMan" / "glTF" / "CesiumMan.gltf";
	ASSERT_TRUE(std::filesystem::exists(source)) << source.string();

	ASSERT_TRUE(Kerberos::GLTFSceneImporter::Import(source, m_Output, "CesiumMan", nullptr));

	const auto skeletonPath = FindArtifact(m_Output, ".kbrskeleton");
	const auto animationPath = FindArtifact(m_Output, ".kbranimation");
	ASSERT_FALSE(skeletonPath.empty());
	ASSERT_FALSE(animationPath.empty());

	Kerberos::SkeletonFile skeletonFile;
	ASSERT_TRUE(Kerberos::NativeAssetSerializer::DeserializeSkeleton(skeletonPath, skeletonFile));

	Kerberos::AnimationClipFile animationFile;
	ASSERT_TRUE(Kerberos::NativeAssetSerializer::DeserializeAnimationClip(animationPath, animationFile));

	std::vector<Kerberos::SkeletonJoint> joints;
	joints.reserve(skeletonFile.Joints.size());
	for (const Kerberos::SkeletonJointFile& jointFile : skeletonFile.Joints)
	{
		Kerberos::SkeletonJoint joint{};
		joint.Name = jointFile.Name;
		joint.Parent = jointFile.ParentJoint;
		joint.SourceNode = jointFile.SourceNode;
		joint.RestTranslation = jointFile.RestTranslation;
		joint.RestRotation = jointFile.RestRotation;
		joint.RestScale = jointFile.RestScale;
		joint.InverseBind = jointFile.InverseBind;
		joints.push_back(std::move(joint));
	}
	const Kerberos::Skeleton skeleton(joints);

	std::vector<Kerberos::AnimationTrack> tracks;
	tracks.reserve(animationFile.Tracks.size());
	for (const Kerberos::AnimationTrackFile& trackFile : animationFile.Tracks)
	{
		const auto path = [&trackFile]() -> Kerberos::AnimationPath
		{
			switch (trackFile.Path)
			{
			case Kerberos::AnimTrackPath::Translation: return Kerberos::AnimationPath::Translation;
			case Kerberos::AnimTrackPath::Rotation: return Kerberos::AnimationPath::Rotation;
			case Kerberos::AnimTrackPath::Scale: return Kerberos::AnimationPath::Scale;
			}
			return Kerberos::AnimationPath::Translation;
		}();

		const auto interpolation = [&trackFile]() -> Kerberos::AnimationInterpolation
		{
			switch (trackFile.Interpolation)
			{
			case Kerberos::AnimationClipInterpolation::Linear: return Kerberos::AnimationInterpolation::Linear;
			case Kerberos::AnimationClipInterpolation::Step: return Kerberos::AnimationInterpolation::Step;
			case Kerberos::AnimationClipInterpolation::CubicSpline: return Kerberos::AnimationInterpolation::CubicSpline;
			}
			return Kerberos::AnimationInterpolation::Linear;
		}();

		Kerberos::AnimationTrack track{};
		track.Joint = static_cast<uint32_t>(trackFile.JointIndex);
		track.Path = path;
		track.Interpolation = interpolation;
		track.Times = trackFile.Times;
		track.Values = trackFile.Values;
		track.InTangents = trackFile.InTangents;
		track.OutTangents = trackFile.OutTangents;
		tracks.push_back(std::move(track));
	}

	const Kerberos::AnimationClip clip(animationFile.Name, tracks, animationFile.DurationSeconds);
	const Kerberos::ClipTrackCache cache = Kerberos::BuildClipTrackCache(skeleton, clip);
	EXPECT_EQ(cache.IgnoredTrackCount, 0u);

	const auto maxDifference = [](const Kerberos::SkeletonPose& a, const Kerberos::SkeletonPose& b)
	{
		float maxDifference = 0.0f;
		if (a.SkinMatrices.size() != b.SkinMatrices.size())
			return std::numeric_limits<float>::infinity();

		for (size_t jointIndex = 0; jointIndex < a.SkinMatrices.size(); ++jointIndex)
		{
			for (int column = 0; column < 4; ++column)
			{
				for (int row = 0; row < 4; ++row)
				{
					maxDifference = std::max(maxDifference,
					                         std::abs(a.SkinMatrices[jointIndex][column][row] -
					                                  b.SkinMatrices[jointIndex][column][row]));
				}
			}
		}
		return maxDifference;
	};

	Kerberos::SkeletonPose firstPose;
	Kerberos::EvaluatePose(skeleton, clip, cache, 0.0f, firstPose);

	float largestChange = 0.0f;
	for (int sampleIndex = 1; sampleIndex <= 4; ++sampleIndex)
	{
		const float time = animationFile.DurationSeconds * static_cast<float>(sampleIndex) / 5.0f;

		Kerberos::SkeletonPose sampledPose;
		Kerberos::EvaluatePose(skeleton, clip, cache, time, sampledPose);
		largestChange = std::max(largestChange, maxDifference(firstPose, sampledPose));
	}

	EXPECT_GT(largestChange, 1.0e-3f);
}

TEST_F(GLTFImportTest, BakesAnimationMarkersAndEventsFromMetadata)
{
	const std::filesystem::path source =
		std::filesystem::path{ KBR_SOURCE_DIR } / "KerberosEditor" / "Assets" / "Models"
		/ "CesiumMan" / "glTF" / "CesiumMan.gltf";
	ASSERT_TRUE(std::filesystem::exists(source)) << source.string();

	std::map<std::string, Kerberos::AnimationClipMetaEntry> animationClips;
	animationClips["animation:0"].Markers = {
		{ .Time = 0.8f, .Name = "Late" },
		{ .Time = 0.2f, .Name = "Early" },
		{ .Time = 999.0f, .Name = "Clamped" },
	};
	animationClips["animation:0"].Events = {
		{
			.Time = 0.6f,
			.Duration = 0.0f,
			.Name = "Footstep",
			.Payload = "left",
			.FireInEditor = false,
			.MinTriggerWeight = 0.0f,
			.Flags = 0,
		},
		{
			.Time = 0.3f,
			.Duration = 0.2f,
			.Name = "WeaponTrail",
			.Payload = "right",
			.FireInEditor = true,
			.MinTriggerWeight = 0.25f,
			.Flags = 2,
			.Parameters = {
				{
					.Name = "Damage",
					.Type = Kerberos::AnimationEventParameterTypeFile::Float,
					.FloatValue = 25.0f,
				},
				{
					.Name = "Socket",
					.Type = Kerberos::AnimationEventParameterTypeFile::String,
					.StringValue = "Weapon",
				},
			},
		},
	};

	ASSERT_TRUE(Kerberos::GLTFSceneImporter::Import(source, m_Output, "CesiumMan", nullptr,
	                                                nullptr, nullptr, &animationClips));

	const auto animationPath = FindArtifact(m_Output, ".kbranimation");
	ASSERT_FALSE(animationPath.empty());

	Kerberos::AnimationClipFile animation;
	ASSERT_TRUE(Kerberos::NativeAssetSerializer::DeserializeAnimationClip(animationPath, animation));
	EXPECT_EQ(animation.Version, 3u);

	ASSERT_EQ(animation.Markers.size(), 3u);
	EXPECT_EQ(animation.Markers[0].Name, "Early");
	EXPECT_FLOAT_EQ(animation.Markers[0].Time, 0.2f);
	EXPECT_EQ(animation.Markers[1].Name, "Late");
	EXPECT_FLOAT_EQ(animation.Markers[1].Time, 0.8f);
	EXPECT_EQ(animation.Markers[2].Name, "Clamped");
	EXPECT_LE(animation.Markers[2].Time, animation.DurationSeconds);

	ASSERT_EQ(animation.Events.size(), 2u);
	EXPECT_EQ(animation.Events[0].Name, "WeaponTrail");
	EXPECT_FLOAT_EQ(animation.Events[0].Time, 0.3f);
	EXPECT_EQ(animation.Events[0].Payload, "right");
	EXPECT_TRUE(animation.Events[0].FireInEditor);
	EXPECT_FLOAT_EQ(animation.Events[0].MinTriggerWeight, 0.25f);
	EXPECT_EQ(animation.Events[0].Flags, 2u);

	ASSERT_EQ(animation.Events[0].Parameters.size(), 2u);
	EXPECT_EQ(animation.Events[0].Parameters[0].Name, "Damage");
	EXPECT_FLOAT_EQ(animation.Events[0].Parameters[0].FloatValue, 25.0f);
	EXPECT_EQ(animation.Events[0].Parameters[1].StringValue, "Weapon");

	EXPECT_EQ(animation.Events[1].Name, "Footstep");
	EXPECT_FLOAT_EQ(animation.Events[1].Time, 0.6f);
	EXPECT_EQ(animation.Events[1].Payload, "left");
}
