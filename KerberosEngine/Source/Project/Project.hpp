#pragma once

#include "Core/Core.hpp"
#include "Assets/AssetManagerBase.hpp"
#include "Assets/EditorAssetManager.hpp"
#include "Assets/RuntimeAssetManager.hpp"

#include <filesystem>

namespace Kerberos
{
	struct ProjectInfo
	{
		std::string Name = "Untitled";
		std::filesystem::path AssetDirectory = "Assets";

		std::filesystem::path StartScenePath;
	};

	class Project
	{
	public:
		static Ref<Project> New();
		static Ref<Project> Load(const std::filesystem::path& filepath);
		static bool SaveActive();

		/**
		* Returns the path to the active project's asset directory.
		*/
		static const std::filesystem::path& GetAssetDirectory();

        /**
         * Returns the path to the active project's root directory.
         */
        static const std::filesystem::path& GetProjectDirectory();

        /**
         * @brief Returns the full filesystem path for an asset within the active project.
         * @param assetPath The path to the asset relative to the project's asset directory.
         * @return The full filesystem path to the asset.
         */
        static std::filesystem::path GetAssetFileSystemPath(const std::filesystem::path& assetPath);

        void SetInfo(const ProjectInfo& info);

		ProjectInfo& GetInfo() { return m_Info; }

		static Ref<Project> GetActive() { return s_ActiveProject; }
		static void ReleaseActiveProjectResources();

		Ref<AssetManagerBase> GetAssetManager() const { return m_AssetManager; }
		Ref<EditorAssetManager> GetEditorAssetManager() const { return std::dynamic_pointer_cast<EditorAssetManager>(m_AssetManager); }
		Ref<RuntimeAssetManager> GetRuntimeAssetManager() const { return std::dynamic_pointer_cast<RuntimeAssetManager>(m_AssetManager); }
		Ref<RuntimeAssetManager> UseRuntimeAssetManager();

	private:
		ProjectInfo m_Info;
		std::filesystem::path m_ProjectDirectory;

		Ref<AssetManagerBase> m_AssetManager;

		inline static Ref<Project> s_ActiveProject;
	};
}