#pragma once

#include "Application.hpp"
#include "Project/Project.hpp"
#include "Scene/Scene.hpp"

#include <filesystem>
#include <functional>
#include <string>
#include <utility>

namespace Kerberos::Tests
{
	struct TestApplicationResult
	{
		bool ProjectLoaded = false;
		bool AssetManagerInitialized = false;
		bool StartSceneLoaded = false;
		std::string ProjectName;
		std::filesystem::path AssetDirectory;
		size_t StartSceneRootEntityCount = 0;
		size_t StartSceneStaticMeshCount = 0;
		size_t StartSceneResolvedMeshCount = 0;
		size_t StartSceneResolvedMaterialCount = 0;
	};

	class TestApplication final : public Application
	{
	public:
		TestApplication(const std::filesystem::path& projectPath, TestApplicationResult& result,
		std::function<void()> onProjectLoaded = {});

		TestApplication(const TestApplication&) = delete;
		TestApplication(TestApplication&&) = delete;
		TestApplication& operator=(const TestApplication&) = delete;
		TestApplication& operator=(TestApplication&&) = delete;
	};
}
