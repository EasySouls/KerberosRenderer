#include "TestApplication.hpp"

#include <gtest/gtest.h>

#include <filesystem>

import Kerberos;

namespace
{
	class EditorEndToEndTest : public testing::Test
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

TEST_F(EditorEndToEndTest, LoadsProjectAndStartScene)
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
	ASSERT_TRUE(result.AssetManagerInitialized);
	ASSERT_TRUE(result.StartSceneLoaded);
	EXPECT_GT(result.StartSceneRootEntityCount, 0);
}
