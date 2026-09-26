#include "Project/Project.hpp"

#include <gtest/gtest.h>

#include <filesystem>

import Kerberos;

namespace
{
	class ProjectLoadTest : public testing::Test
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

		~ProjectLoadTest() override
		{
			Kerberos::Project::ReleaseActiveProjectResources();
		}
	};
}

TEST_F(ProjectLoadTest, OpensEditorProject)
{
	const std::filesystem::path projectPath =
		std::filesystem::path{KBR_SOURCE_DIR} / "KerberosEditor" / "TestProject.kbrproj";

	ASSERT_TRUE(std::filesystem::exists(projectPath))
		<< "Project fixture does not exist: " << projectPath.string();

	const auto project = Kerberos::Project::Load(projectPath, {.InitializeAssetManager = false});

	ASSERT_NE(project, nullptr);
	EXPECT_EQ(project->GetInfo().Name, "TestProject");
	EXPECT_EQ(Kerberos::Project::GetActive(), project);
	EXPECT_EQ(Kerberos::Project::GetProjectDirectory(), projectPath.parent_path());
	EXPECT_EQ(project->GetInfo().AssetDirectory, std::filesystem::path{"Assets"});
	EXPECT_EQ(project->GetInfo().StartScenePath,
			  std::filesystem::path{"Scenes"} / "Example.kerberos");
}
