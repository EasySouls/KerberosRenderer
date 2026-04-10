#include "kbrpch.hpp"
#include "Project.hpp"
#include "Serialization/ProjectSerializer.hpp"

namespace Kerberos
{
	Ref<Project> Project::New()
	{
		s_ActiveProject = CreateRef<Project>();

		return s_ActiveProject;
	}

	Ref<Project> Project::Load(const std::filesystem::path& filepath)
	{
		const Ref<Project> projectToLoad = CreateRef<Project>();

		const ProjectSerializer deserializer(projectToLoad);
		if (deserializer.Deserialize(filepath))
		{
			const auto absolutePath = std::filesystem::absolute(filepath);
			projectToLoad->m_ProjectDirectory = absolutePath.parent_path();
			s_ActiveProject = projectToLoad;
			KBR_CORE_INFO("Project is loaded from {}", absolutePath.string());

			/// Initialize the asset manager for the project
			/// TODO: Load Editor or Runtime Asset Manager based on the project type
			const Ref<EditorAssetManager> editorAssetManager = CreateRef<EditorAssetManager>();
			s_ActiveProject->m_AssetManager = editorAssetManager;
			editorAssetManager->DeserializeAssetRegistry();

			return s_ActiveProject;
		}

		return nullptr;
	}

	bool Project::SaveActive()
	{
		KBR_CORE_ASSERT(s_ActiveProject, "Active project has not been set!");

		const auto savePath = GetActive()->m_ProjectDirectory / (GetActive()->m_Info.Name + ".kbrproj");

		const ProjectSerializer serializer(s_ActiveProject);
		if (serializer.Serialize(savePath))
		{
			s_ActiveProject->m_ProjectDirectory = savePath.parent_path();
			KBR_CORE_INFO("Project is saved to {}", std::filesystem::absolute(savePath).string());

			if (const auto editorAssetManager = std::dynamic_pointer_cast<EditorAssetManager>(s_ActiveProject->m_AssetManager))
			{
				editorAssetManager->SerializeAssetRegistry();
			}

			return true;
		}
		else
		{
			KBR_CORE_WARN("Could not save project to {}", std::filesystem::absolute(savePath).string());
			return false;
		}
	}

	void Project::SetInfo(const ProjectInfo& info)
	{
		KBR_CORE_ASSERT(s_ActiveProject, "Active project has not been set!");

		m_Info = info;

		/// The project info has changed, so we might need to update the assets
		s_ActiveProject->m_AssetManager = CreateRef<EditorAssetManager>();
	}

	void Project::ReleaseActiveProjectResources() 
	{
		s_ActiveProject.reset();
	}
}
