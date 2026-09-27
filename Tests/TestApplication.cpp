#include "TestApplication.hpp"

#include "Project/Project.hpp"
#include "Serialization/SceneSerializer.hpp"

namespace Kerberos::Tests
{
	namespace
	{
		class ProjectLoadLayer final : public Layer
		{
		public:
			ProjectLoadLayer(std::filesystem::path projectPath, TestApplicationResult& result)
				: Layer("ProjectLoadLayer")
				, m_ProjectPath(std::move(projectPath))
				, m_Result(result)
			{
			}

			void OnAttach() override
			{
				const auto project = Project::Load(m_ProjectPath);
				m_Result.ProjectLoaded = project != nullptr;
				m_Result.AssetManagerInitialized =
					project != nullptr && project->GetEditorAssetManager() != nullptr;

				if (m_Result.ProjectLoaded)
				{
					m_Result.ProjectName = project->GetInfo().Name;
					m_Result.AssetDirectory = project->GetInfo().AssetDirectory;
					const auto startScene = CreateRef<Scene>();
					const auto scenePath = Project::GetProjectDirectory()
						/ Project::GetAssetDirectory()
						/ project->GetInfo().StartScenePath;
					SceneSerializer serializer(startScene);
					m_Result.StartSceneLoaded = serializer.Deserialize(scenePath);
					if (m_Result.StartSceneLoaded)
						m_Result.StartSceneRootEntityCount = startScene->GetRootEntities().size();
				}

				Application::Get().Close();
			}

			void OnDetach() override {}
			void OnUpdate(float) override {}
			void OnEvent(Event&) override {}
			void OnImGuiRender() override {}

		private:
			std::filesystem::path m_ProjectPath;
			TestApplicationResult& m_Result;
		};
	}

	TestApplication::TestApplication(const std::filesystem::path& projectPath, TestApplicationResult& result)
		: Application(ApplicationSpecification{
			.Name = "Kerberos Test Application",
			.WorkingDirectory = projectPath.parent_path(),
			.CommandLineArgs = {},
			.Headless = true
		})
	{
		PushLayer<ProjectLoadLayer>(projectPath, result);
	}
}
