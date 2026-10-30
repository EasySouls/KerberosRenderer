#include "TestApplication.hpp"

#include "Assets/AssetManager.hpp"
#include "Scene/Components/AnimationComponents.hpp"
#include "Scene/Entity.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

import Kerberos;

namespace
{
	class ProjectIntegrationTest : public testing::Test
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
	};
}

namespace
{
	void CheckImportedSkinnedPrefab(bool& success)
	{
		const auto project = Kerberos::Project::GetActive();
		ASSERT_NE(project, nullptr);
		const auto manager = project->GetEditorAssetManager();
		ASSERT_NE(manager, nullptr);

		Kerberos::AssetHandle prefabHandle = Kerberos::AssetHandle::Invalid();
		for (const auto handle : Kerberos::AssetManager::GetAssetsOfType(Kerberos::AssetType::Prefab))
		{
			const auto& metadata = manager->GetMetadata(handle);
			if (metadata.Filepath.generic_string().find("CesiumMan/glTF/CesiumMan.gltf") == std::string::npos)
				continue;

			bool hasSkinnedMesh = false;
			for (const auto& subMetadata : Kerberos::AssetManager::GetAssetRegistry() | std::views::values)
			{
				if (subMetadata.RootSourceHandle == handle && subMetadata.SubAssetKey == "skinnedmesh:0_0")
				{
					hasSkinnedMesh = true;
					break;
				}
			}
			if (hasSkinnedMesh)
			{
				prefabHandle = handle;
				break;
			}
		}
		ASSERT_TRUE(prefabHandle.IsValid());

		const auto scene = Kerberos::CreateRef<Kerberos::Scene>();
		const Kerberos::Entity root = scene->InstantiatePrefab(prefabHandle, "CesiumManInstance");
		ASSERT_TRUE(root);

		std::vector<Kerberos::Entity> entities{ root };
		for (size_t index = 0; index < entities.size(); ++index)
		{
			for (const Kerberos::Entity child : scene->GetChildren(entities[index]))
				entities.push_back(child);
		}

		size_t jointCount = 0;
		size_t skeletonCount = 0;
		size_t skinnedMeshCount = 0;
		size_t animatorCount = 0;
		Kerberos::UUID skeletonEntity;
		for (const Kerberos::Entity entity : entities)
		{
			if (entity.HasComponent<Kerberos::JointComponent>())
				++jointCount;
			if (entity.HasComponent<Kerberos::SkeletonComponent>())
			{
				++skeletonCount;
				const auto& skeleton = entity.GetComponent<Kerberos::SkeletonComponent>();
				EXPECT_TRUE(skeleton.SkeletonAsset.IsValid());
				EXPECT_EQ(skeleton.JointEntities.size(), 19u);
				skeletonEntity = entity.GetUUID();
			}
			if (entity.HasComponent<Kerberos::SkinnedMeshComponent>())
			{
				++skinnedMeshCount;
				const auto& mesh = entity.GetComponent<Kerberos::SkinnedMeshComponent>();
				EXPECT_TRUE(mesh.MeshAsset.IsValid());
				EXPECT_TRUE(mesh.SkeletonAsset.IsValid());
				EXPECT_TRUE(mesh.MaterialAsset.IsValid());
				EXPECT_NE(mesh.Mesh, nullptr);
				EXPECT_NE(mesh.Material, nullptr);
			}
			if (entity.HasComponent<Kerberos::AnimatorComponent>())
			{
				++animatorCount;
				const auto& animator = entity.GetComponent<Kerberos::AnimatorComponent>();
				EXPECT_EQ(animator.AnimationClips.size(), 1u);
				EXPECT_EQ(animator.ActiveClip, 0);
				EXPECT_FALSE(animator.IsPlaying);
			}
		}

		EXPECT_EQ(jointCount, 19u);
		EXPECT_EQ(skeletonCount, 1u);
		EXPECT_EQ(skinnedMeshCount, 1u);
		EXPECT_EQ(animatorCount, 1u);

		bool skinnedLinkValid = true;
		bool animatorLinkValid = true;
		for (const Kerberos::Entity entity : entities)
		{
			if (entity.HasComponent<Kerberos::SkinnedMeshComponent>())
			{
				skinnedLinkValid = skinnedLinkValid
					&& entity.GetComponent<Kerberos::SkinnedMeshComponent>().SkeletonEntity == skeletonEntity;
			}
			if (entity.HasComponent<Kerberos::AnimatorComponent>())
			{
				animatorLinkValid = animatorLinkValid
					&& entity.GetComponent<Kerberos::AnimatorComponent>().SkeletonEntity == skeletonEntity;
			}
		}
		EXPECT_TRUE(skinnedLinkValid);
		EXPECT_TRUE(animatorLinkValid);
		success = true;
	}
}

TEST_F(ProjectIntegrationTest, LoadsProjectWithAssetManagerAndImportsSkinnedPrefab)
{
	const std::filesystem::path projectPath =
		std::filesystem::path{KBR_SOURCE_DIR} / "KerberosEditor" / "TestProject.kbrproj";

	ASSERT_TRUE(std::filesystem::exists(projectPath));

	Kerberos::Tests::TestApplicationResult result;
	bool prefabChecked = false;
	{
		Kerberos::Tests::TestApplication application(projectPath, result, [&prefabChecked]
		{
			CheckImportedSkinnedPrefab(prefabChecked);
		});
		application.Run();
	}

	ASSERT_TRUE(result.ProjectLoaded);
	EXPECT_TRUE(result.AssetManagerInitialized);
	EXPECT_EQ(result.ProjectName, "TestProject");
	EXPECT_EQ(result.AssetDirectory, std::filesystem::path{"Assets"});
	EXPECT_TRUE(prefabChecked);
}
