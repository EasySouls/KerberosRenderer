#include "TestApplication.hpp"

#include "Project/Project.hpp"
#include "Scene/Entity.hpp"
#include "Serialization/SceneSerializer.hpp"

namespace Kerberos::Tests
{
	namespace
	{
		class ProjectLoadLayer final : public Layer
		{
		public:
			ProjectLoadLayer(std::filesystem::path projectPath, TestApplicationResult& result,
				std::function<void()> onProjectLoaded)
				: Layer("ProjectLoadLayer")
				, m_ProjectPath(std::move(projectPath))
				, m_Result(result)
				, m_OnProjectLoaded(std::move(onProjectLoaded))
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

					const SceneSerializer serializer(startScene);
					m_Result.StartSceneLoaded = serializer.Deserialize(scenePath);
					if (m_Result.StartSceneLoaded)
					{
						m_Result.StartSceneRootEntityCount = startScene->GetRootEntities().size();

						std::vector<Entity> entities;
						for (const entt::entity root : startScene->GetRootEntities())
							entities.emplace_back(root, startScene.get());
						for (size_t index = 0; index < entities.size(); ++index)
						{
							for (const Entity child : startScene->GetChildren(entities[index]))
								entities.push_back(child);
						}

						for (const Entity entity : entities)
						{
							if (!entity.HasComponent<StaticMeshComponent>())
								continue;
							++m_Result.StartSceneStaticMeshCount;
							const auto& component = entity.GetComponent<StaticMeshComponent>();
							if (component.StaticMesh)
								++m_Result.StartSceneResolvedMeshCount;
							if (component.MeshMaterial)
								++m_Result.StartSceneResolvedMaterialCount;
						}
					}

					if (m_OnProjectLoaded)
						m_OnProjectLoaded();
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
			std::function<void()> m_OnProjectLoaded;
		};
	}

	TestApplication::TestApplication(const std::filesystem::path& projectPath, TestApplicationResult& result,
		std::function<void()> onProjectLoaded)
		: Application(ApplicationSpecification{
			.Name = "Kerberos Test Application",
			.WorkingDirectory = projectPath.parent_path(),
			.CommandLineArgs = {},
			.Headless = true
		})
	{
		PushLayer<ProjectLoadLayer>(projectPath, result, std::move(onProjectLoaded));
	}
}
