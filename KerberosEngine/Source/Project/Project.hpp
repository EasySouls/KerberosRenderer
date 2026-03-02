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
		static const std::filesystem::path& GetAssetDirectory()
		{
			KBR_CORE_ASSERT(s_ActiveProject, "An active project is not set!");

			return s_ActiveProject->m_Info.AssetDirectory;
		}

		static const std::filesystem::path& GetProjectDirectory()
		{
			KBR_CORE_ASSERT(s_ActiveProject, "An active project is not set!");

			return s_ActiveProject->m_ProjectDirectory;
		}

		static std::filesystem::path GetAssetFileSystemPath(const std::filesystem::path& assetPath)
		{
			KBR_CORE_ASSERT(s_ActiveProject, "An active project is not set!");

			return GetProjectDirectory() / GetAssetDirectory() / assetPath;
		}

		void SetInfo(const ProjectInfo& info);

		ProjectInfo& GetInfo() { return m_Info; }

		static Ref<Project> GetActive() { return s_ActiveProject; }

		Ref<AssetManagerBase> GetAssetManager() const { return m_AssetManager; }
		Ref<EditorAssetManager> GetEditorAssetManager() const { return std::static_pointer_cast<EditorAssetManager>(m_AssetManager); }
		Ref<RuntimeAssetManager> GetRuntimeAssetManager() const { return std::static_pointer_cast<RuntimeAssetManager>(m_AssetManager); }

	private:
		ProjectInfo m_Info;
		std::filesystem::path m_ProjectDirectory;

		Ref<AssetManagerBase> m_AssetManager;

		inline static Ref<Project> s_ActiveProject;
	};
}