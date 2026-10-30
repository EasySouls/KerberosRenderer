#include "Assets/Formats/AnimationFormat.hpp"
#include "Assets/Formats/NativeAssetFormat.hpp"
#include "Assets/Formats/NativeAssetSerializer.hpp"
#include "Assets/Formats/SkeletonFormat.hpp"
#include "Assets/Importers/GLTFSceneImporter.hpp"
#include "Renderer/Animation/SkeletalMesh.hpp"

#include <gtest/gtest.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include <filesystem>
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
