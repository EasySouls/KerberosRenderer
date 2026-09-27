#include "TestApplication.hpp"

#include <gtest/gtest.h>

#include <filesystem>

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

TEST_F(ProjectIntegrationTest, LoadsProjectWithAssetManager)
{
	const std::filesystem::path projectPath =
		std::filesystem::path{KBR_SOURCE_DIR} / "KerberosEditor" / "TestProject.kbrproj";

	ASSERT_TRUE(std::filesystem::exists(projectPath));

	Kerberos::Tests::TestApplicationResult result;
	{
		Kerberos::Tests::TestApplication application(projectPath, result);
		application.Run();
	}

	ASSERT_TRUE(result.ProjectLoaded);
	EXPECT_TRUE(result.AssetManagerInitialized);
	EXPECT_EQ(result.ProjectName, "TestProject");
	EXPECT_EQ(result.AssetDirectory, std::filesystem::path{"Assets"});
}
