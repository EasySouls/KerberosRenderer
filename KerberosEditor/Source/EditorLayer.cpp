#include "EditorLayer.hpp"

#include "VulkanContext.hpp"
#include "IO.hpp"
#include "Renderer/Renderer.hpp"
#include "Application.hpp"
#include "AssetConstants.hpp"
#include "Assets/AssetManager.hpp"
#include "Core/EnginePaths.hpp"
#include "Assets/Importers/TextureImporter.hpp"
#include "Profiling/Profilers.hpp"
#include "Input/InputSystem.hpp"
#include "Utils/SystemOperations.hpp"
#include "Scene/Camera/FirstPersonCamera.hpp"
#include "Serialization/SceneSerializer.hpp"
#include "Input/KeyCodes.hpp"
#include "Renderer/Upscaling/UpscalerTypes.hpp"
#include "Scripting/ScriptEngine.hpp"
#include "Undo/InstantiatePrefabCommand.hpp"

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui/imgui.h>
#include <ImGuizmo/src/ImGuizmo.h>

#include <limits>
#include <ranges>
#include <fstream>
#include <string_view>
#include <utility>

import Kerberos;

namespace Kerberos
{
	namespace
	{
		std::string ReplaceAll(std::string input, const std::string& from, const std::string& to)
		{
			size_t pos = 0;
			while ((pos = input.find(from, pos)) != std::string::npos)
			{
				input.replace(pos, from.size(), to);
				pos += to.size();
			}
			return input;
		}

		/// Creates the user's C# scripting project (<name>.csproj + starter script)
		/// under <project>/Assets/Scripts. The generated project references the
		/// engine's KerberosScriptCoreLib through the KerberosEnginePath property.
		void WriteUserScriptProject(const std::filesystem::path& projectDirectory, const std::string& pascalName)
		{
			const std::filesystem::path scriptsDirectory = projectDirectory / "Assets" / "Scripts";
			std::error_code ec;
			std::filesystem::create_directories(scriptsDirectory, ec);

			constexpr std::string_view csprojTemplate = R"(<Project Sdk="Microsoft.NET.Sdk">

  <PropertyGroup>
    <TargetFramework>net10.0</TargetFramework>
    <AssemblyName>PROJECT_NAME</AssemblyName>
    <RootNamespace>PROJECT_NAME</RootNamespace>
    <ImplicitUsings>enable</ImplicitUsings>
    <Nullable>enable</Nullable>
    <AllowUnsafeBlocks>true</AllowUnsafeBlocks>
  </PropertyGroup>

  <ItemGroup>
    <Reference Include="KerberosScriptCoreLib">
      <HintPath>$(KerberosEnginePath)/Resources/Scripts/KerberosScriptCoreLib.dll</HintPath>
      <Private>false</Private>
    </Reference>
  </ItemGroup>

</Project>
)";

			constexpr std::string_view scriptTemplate = R"(using Kerberos.Scene;

namespace PROJECT_NAME
{
    public class PROJECT_NAMEScript : Entity
    {
        protected override void OnCreate()
        {
        }

        protected override void OnUpdate(float deltaTime)
        {
        }
    }
}
)";

			const std::string csproj = ReplaceAll(std::string(csprojTemplate), "PROJECT_NAME", pascalName);
			const std::string script = ReplaceAll(ReplaceAll(std::string(scriptTemplate), "PROJECT_NAMEScript", pascalName + "Script"),
				"PROJECT_NAME", pascalName);

			{
				std::ofstream file(scriptsDirectory / (pascalName + ".csproj"));
				file << csproj;
			}
			{
				std::ofstream file(scriptsDirectory / (pascalName + "Script.cs"));
				file << script;
			}
		}
	}

	EditorLayer::EditorLayer()
			: Layer("EditorLayer")
	{
		m_HierarchyPanel.SetUndoRedoManager(&m_UndoRedo);

		m_HierarchyPanel.SetOnEntityDeleted([this](const Entity entity)
		{
			if (m_HoveredEntity == entity)
			{
                ResetHoveredEntity();
			} 
		});

		// Selecting an entity in the hierarchy takes over the inspector from any
		// asset import-options selection, which would otherwise keep precedence.
		m_HierarchyPanel.SetOnEntitySelected([this](const Entity entity)
		{
			if (entity && m_AssetsPanel)
				m_AssetsPanel->ClearSelection();
		});
	}

	EditorLayer::~EditorLayer()
	{
	}

	void EditorLayer::OnAttach()
	{
        KBR_TRACY_FUNCTION();

		Log::EditorInfo("EditorLayer attached!");

		constexpr bool isTesting = true;
		if (isTesting)
		{
			OpenProject("TestProject.kbrproj");
		}
		else
		{
			/// If there is a command line argument, try to open the project specified in it
			const auto &[Count, Args] = Application::Get().GetSpecification().CommandLineArgs;
			if (Count > 1)
			{
				const std::filesystem::path projectPath = Args[1];
				if (std::filesystem::exists(projectPath))
				{
					OpenProject(projectPath);
				}
				else
				{
					NewProject();
				}
			}
			else
			{
				/// If there is no command line argument, the user is prompted to open a project
				if (!OpenProject())
				{
					Application::Get().Close();
				}
			}
		}

		m_BasicFont = AssetManager::GetDefaultFont();


		constexpr float nearClip = 0.1f;
        constexpr float farClip = 10000.0f;
        constexpr float cameraMoveSpeed = 50.0f;
		m_EditorCamera = std::make_unique<FirstPersonCamera>(45.0f, 16.0f / 9.0f, nearClip, farClip);
		// m_EditorCamera = std::make_unique<EditorCamera>(45.0f, 16.0f / 9.0f, 0.1f, 1000.0f);
		m_EditorCamera->SetFlipY(true);
		m_EditorCamera->SetPosition(glm::vec3(0.0f, 15.0f, -45.0f));
		m_EditorCamera->SetRotation(glm::vec3(0.0f, 90.0f, 0.0f));
		m_EditorCamera->SetMoveSpeed(cameraMoveSpeed);

		m_ViewportSize = {1280.0f, 720.0f};

		m_AssetsPanel = CreateOwner<AssetsPanel>(m_NotificationManager);

		m_IconPlay = TextureImporter::ImportTexture(EnginePaths::Assets() / "Editor/play_button.png");
		m_IconStop = TextureImporter::ImportTexture(EnginePaths::Assets() / "Editor/stop_button.png");
		m_IconPause = TextureImporter::ImportTexture(EnginePaths::Assets() / "Editor/pause_button.png");
		m_IconResume = TextureImporter::ImportTexture(EnginePaths::Assets() / "Editor/outlined_play_button.png");
	}

	void EditorLayer::OnDetach()
	{
        Log::EditorInfo("EditorLayer detached!");
	}

	void EditorLayer::OnUpdate(const float deltaTime)
	{
		KBR_TRACY_FUNCTION();

		m_Fps = 1.0f / deltaTime;
		m_Time += deltaTime;

		// Resize the output images and the camera if the viewport size has changed
		const glm::vec2 outputSize = Renderer::GetOutputImageSize();
		if (m_ViewportSize.x > 0.0f && m_ViewportSize.y > 0.0f &&
				(static_cast<int>(outputSize.x) != static_cast<int>(m_ViewportSize.x) || static_cast<int>(outputSize.y) != static_cast<int>(m_ViewportSize.y)))
		{
			m_EditorCamera->SetViewportSize(m_ViewportSize.x, m_ViewportSize.y);
			m_ActiveScene->OnViewportResize(static_cast<uint32_t>(m_ViewportSize.x), static_cast<uint32_t>(m_ViewportSize.y));
		}

		{
			KBR_TRACY_SCOPE("EditorCamera::OnUpdate");

			m_EditorCamera->SetIsInputBlocked(m_DoesImGuiWantInput);

			/// The editor camera is only used while editing or simulating.
			/// Play mode renders through the scene's primary CameraComponent.
			if (m_SceneState != SceneState::Play)
			{
				m_EditorCamera->OnUpdate(deltaTime);
			}
		}

		{
			KBR_TRACY_SCOPE("Scene::OnUpdate");

			m_ActiveScene->CalculateEntityTransforms();

			switch (m_SceneState)
			{
			case SceneState::Edit:
				m_ActiveScene->OnUpdateEditor(deltaTime, *m_EditorCamera);
				break;
			case SceneState::Simulate:
				m_ActiveScene->OnUpdateSimulation(deltaTime, *m_EditorCamera);
				break;
			case SceneState::Play:
				m_ActiveScene->OnUpdateRuntime(deltaTime);
				break;
			}
		}

		{
			KBR_TRACY_SCOPE("HandleMousePicking");

			HandleMousePicking();
		}

		m_CameraEntity = m_ActiveScene->GetPrimaryCameraEntity();
	}

	void EditorLayer::OnEvent(Event &event)
	{
        KBR_TRACY_FUNCTION();

		if (m_SceneState != SceneState::Play)
		{
			m_EditorCamera->OnEvent(event);
		}

		m_HierarchyPanel.OnEvent(event);
		m_AssetsPanel->OnEvent(event);
		m_ConsolePanel.OnEvent(event);

		EventDispatcher dispatcher(event);
		dispatcher.Dispatch<KeyPressedEvent>(KBR_BIND_FN(EditorLayer::OnKeyPressed));
		dispatcher.Dispatch<MouseButtonPressedEvent>(KBR_BIND_FN(EditorLayer::OnMouseButtonPressed));
		dispatcher.Dispatch<WindowDropEvent>(KBR_BIND_FN(EditorLayer::OnWindowDrop));
	}

	void EditorLayer::OnImGuiRender()
	{
        KBR_TRACY_FUNCTION();

		static bool dockspaceOpen = true;
		static ImGuiDockNodeFlags dockspaceFlags = ImGuiDockNodeFlags_None;

		ImGuiWindowFlags windowFlags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;

		if (m_IsFullScreenPersistent)
		{
			const ImGuiViewport *viewport = ImGui::GetMainViewport();
			ImGui::SetNextWindowPos(viewport->Pos);
			ImGui::SetNextWindowSize(viewport->Size);
			ImGui::SetNextWindowViewport(viewport->ID);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
			windowFlags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
			windowFlags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
		}

		if (dockspaceFlags & ImGuiDockNodeFlags_PassthruCentralNode)
			windowFlags |= ImGuiWindowFlags_NoBackground;

		// Keep window name constant to preserve dockspace layout across scene state changes
		const char *dockspaceWindowName = "EditorDockSpace";

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::Begin(dockspaceWindowName, &dockspaceOpen, windowFlags);
		ImGui::PopStyleVar();

		if (m_IsFullScreenPersistent)
			ImGui::PopStyleVar(2);

		const ImGuiIO &io = ImGui::GetIO();
		if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
		{
			const ImGuiID dockspaceId = ImGui::GetID("MyDockspace");
			ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), dockspaceFlags);
		}

		DrawMenuBar();

		m_HierarchyPanel.OnImGuiRender();
		m_AssetsPanel->OnImGuiRender();
		m_ConsolePanel.OnImGuiRender();

		m_InspectorPanel.SetContext(&m_HierarchyPanel, m_AssetsPanel.get());
		m_InspectorPanel.OnImGuiRender();

		DrawViewport();

		DrawDebugWindow();

		DrawUIToolbar();

		m_NotificationManager.RenderNotifications();

		ImGui::End();

		m_DoesImGuiWantInput = DoesImGuiWantInput();

		if (Input::IsKeyPressed(Key::RightControl))
		{
			ImGui::DebugStartItemPicker();
		}
	}

	void EditorLayer::OnScenePlay()
	{
		if (!m_CameraEntity)
		{
			m_NotificationManager.AddNotification("Cannot enter Play mode: No primary camera found in the scene!", Notification::Type::Error);
			return;
		}

		m_SceneState = SceneState::Play;

		ResetHoveredEntity();

		/// Runtime edits target the scene copy, so the editor history must not record them.
		m_HierarchyPanel.SetUndoRedoManager(nullptr);

		m_RuntimeScene = Scene::Copy(m_EditorScene);

		m_RuntimeScene->OnRuntimeStart();

		m_ActiveScene = m_RuntimeScene;

		const Entity selectedEntity = m_HierarchyPanel.GetSelectedEntity();
		m_HierarchyPanel.SetContext(m_ActiveScene);
		m_HierarchyPanel.SetSelectedEntity(selectedEntity);
	}

	void EditorLayer::OnSceneSimulate()
	{
		m_SceneState = SceneState::Simulate;

		ResetHoveredEntity();

		m_HierarchyPanel.SetUndoRedoManager(nullptr);

		m_RuntimeScene = Scene::Copy(m_EditorScene);

		m_RuntimeScene->OnSimulationStart();

		m_ActiveScene = m_RuntimeScene;

		const Entity selectedEntity = m_HierarchyPanel.GetSelectedEntity();
		m_HierarchyPanel.SetContext(m_ActiveScene);
		m_HierarchyPanel.SetSelectedEntity(selectedEntity);
	}

	void EditorLayer::OnSceneStop()
	{
		if (m_SceneState == SceneState::Play)
			m_ActiveScene->OnRuntimeStop();
		else if (m_SceneState == SceneState::Simulate)
			m_ActiveScene->OnSimulationStop();

		m_SceneState = SceneState::Edit;
		m_IsScenePaused = false;

		/// The runtime scene is released below, so the hover cannot keep a pointer into it.
        ResetHoveredEntity();

		m_RuntimeScene = nullptr;

		m_ActiveScene = m_EditorScene;
		m_HierarchyPanel.SetContext(m_ActiveScene);
		m_HierarchyPanel.SetUndoRedoManager(&m_UndoRedo);
	}

	void EditorLayer::HandleDragAndDrop()
	{
        KBR_TRACY_FUNCTION();

		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(assetBrowserItem))
			{
				const auto &path = static_cast<const char *>(payload->Data);
				Log::EditorInfo("Drag and drop payload: {0}", path);

				const std::string message = "Drag and drop payload: " + std::string(path);
				m_NotificationManager.AddNotification(message, Notification::Type::Info);

				const AssetHandle assetHandle = *static_cast<AssetHandle*>(payload->Data);
				const AssetType assetType = AssetManager::GetAssetType(assetHandle);
				if (assetType == AssetType::Scene)
				{
					const Ref<Scene> scene = AssetManager::GetAsset<Scene>(assetHandle);
					OpenScene(scene);
				}
			}

			if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(assetBrowserMesh))
			{
				const AssetHandle handle = ReadAssetDragPayload(payload->Data, payload->DataSize).Handle;
				const AssetType assetType = AssetManager::GetAssetType(handle);

				if (assetType != AssetType::Mesh) {
                    const std::filesystem::path assetPath =  Project::GetActive()->GetEditorAssetManager()->GetMetadata(handle).Filepath;
                    Log::EditorError("Asset is not a mesh: {0}", assetPath.string());
                    m_NotificationManager.AddNotification("Asset is not a mesh: " + assetPath.string(),
                                                          Notification::Type::Error);
                    return;
                }

				Entity meshEntity = m_ActiveScene->CreateEntity("Mesh");
				auto &smc = meshEntity.AddComponent<StaticMeshComponent>();
				smc.StaticMesh = AssetManager::ResolveMeshAsset(handle);
				m_HierarchyPanel.SetSelectedEntity(meshEntity);
			}

			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(assetBrowserPrefab)) 
			{
                const AssetHandle handle = ReadAssetDragPayload(payload->Data, payload->DataSize).Handle;
                const AssetType assetType = AssetManager::GetAssetType(handle);

				if (assetType != AssetType::Prefab)
				{
                    const std::filesystem::path assetPath = Project::GetActive()->GetEditorAssetManager()->GetMetadata(handle).Filepath;
                    Log::EditorError("Asset is not a prefab: {0}", assetPath.string());
                    m_NotificationManager.AddNotification("Asset is not a prefab: " + assetPath.string(),
                                                          Notification::Type::Error);
                    return;
                }

				if (m_SceneState == SceneState::Edit)
				{
					auto command = std::make_unique<InstantiatePrefabCommand>(m_ActiveScene, handle);
					command->Execute();

					// First checking if succeeded. only then adding it to the undo stack
					if (command->GetEntityID().IsValid())
					{
						if (const Entity prefabEntity = m_ActiveScene->TryGetEntityByUUID(command->GetEntityID()))
							m_HierarchyPanel.SetSelectedEntity(prefabEntity);
						m_UndoRedo.Record(std::move(command));
					}
				}
				else if (const Entity prefabEntity = m_ActiveScene->InstantiatePrefab(handle))
				{
					m_HierarchyPanel.SetSelectedEntity(prefabEntity);
				}
            }

			ImGui::EndDragDropTarget();
		}
	}

	void EditorLayer::HandleMousePicking()
	{
		KBR_TRACY_FUNCTION();

		if (m_HoveredEntityDeletedLastFrame)
		{
			m_HoveredEntityDeletedLastFrame = false;
			return;
		}

		auto [mx, my] = ImGui::GetMousePos();
		mx -= m_ViewportBounds[0].x;
		my -= m_ViewportBounds[0].y;
		const glm::vec2 viewportSize = m_ViewportBounds[1] - m_ViewportBounds[0];

		const int mouseX = static_cast<int>(mx);
		const int mouseY = static_cast<int>(my);

		if (mouseX >= 0 && mouseY >= 0 && mouseX <= static_cast<int>(viewportSize.x) && mouseY <= static_cast<int>(viewportSize.y))
		{
			Renderer::RequestMousePickingPixel(static_cast<uint32_t>(mouseX), static_cast<uint32_t>(mouseY));

			if (const auto pickedEntityID = Renderer::GetMousePickingEntityID())
			{
				constexpr uint32_t invalidEntityID = std::numeric_limits<uint32_t>::max();
				if (*pickedEntityID == invalidEntityID)
				{
					m_HoveredEntity = {};
				}
				else
				{
					const Entity pickedEntity{static_cast<entt::entity>(*pickedEntityID), m_ActiveScene.get()};
					m_HoveredEntity = m_ActiveScene->IsEntityValid(pickedEntity) ? pickedEntity : Entity{};
				}
			}
		}
		else
		{
			m_HoveredEntity = {};
		}
	}

	void EditorLayer::NewProject()
	{
        KBR_TRACY_FUNCTION();

		if (m_SceneState != SceneState::Edit)
			OnSceneStop();

		/// Ask the user where the new project should live. The selected folder
		/// becomes the project directory and its name becomes the project name.
		const std::string selectedFolder = FileDialog::SelectFolder(
			"Select an empty folder for the new project (the folder name becomes the project name)");
		if (selectedFolder.empty())
			return;

		std::error_code ec;
		const std::filesystem::path projectDirectory =
			std::filesystem::weakly_canonical(std::filesystem::path(selectedFolder), ec);
		if (ec || !std::filesystem::is_directory(projectDirectory))
		{
			m_NotificationManager.AddNotification(
				"The selected project location is not a valid directory.", Notification::Type::Error);
			return;
		}

		const std::string projectName = projectDirectory.filename().string();
		if (projectName.empty())
		{
			m_NotificationManager.AddNotification(
				"The selected folder cannot be used as a project name. Choose or create a named folder.",
				Notification::Type::Error);
			return;
		}

		/// Never create a project in a non-empty folder: the generated project
		/// file, asset registry, and scenes could otherwise overwrite user files.
		const std::filesystem::directory_iterator directoryIterator(projectDirectory, ec);
		if (ec)
		{
			m_NotificationManager.AddNotification(
				"Could not inspect the selected project folder: " + ec.message(), Notification::Type::Error);
			return;
		}
		if (directoryIterator != std::filesystem::directory_iterator())
		{
			m_NotificationManager.AddNotification(
				"The selected folder is not empty. Choose an empty folder or create a new one.",
				Notification::Type::Error);
			return;
		}

		/// Switch projects on the main-thread queue so the old project's GPU
		/// resources are not destroyed while the current frame is still in
		/// flight.
		Application::Get().SubmitToMainThreadQueue([this, projectDirectory, projectName]() {
			CreateProject(projectDirectory, projectName);
		});
	}

	void EditorLayer::CreateProject(const std::filesystem::path& projectDirectory, const std::string& projectName)
	{
        KBR_TRACY_FUNCTION();

		/// The previous project's meshes/textures may still be referenced by an
		/// in-flight frame. Wait for the device before releasing them.
		VulkanContext::Get().WaitIdle();

		std::error_code ec;
		const std::filesystem::path assetsDirectory = projectDirectory / "Assets";
		const std::filesystem::path scenesDirectory = assetsDirectory / "Scenes";
		std::filesystem::create_directories(scenesDirectory, ec);
		if (ec)
		{
			m_NotificationManager.AddNotification(
				"Failed to create the project directories: " + ec.message(), Notification::Type::Error);
			return;
		}

		const std::string assemblyName = Project::ToPascalCaseName(projectName);
		WriteUserScriptProject(projectDirectory, assemblyName);

		const std::filesystem::path scenePath =
			LookForNameForNewScene(scenesDirectory, "Untitled Scene.kerberos");

		/// Build and persist the default scene before activating the project so
		/// a failure leaves the currently open project untouched.
		const Ref<Scene> defaultScene = CreateRef<Scene>();
		defaultScene->OnViewportResize(static_cast<uint32_t>(m_ViewportSize.x),
			static_cast<uint32_t>(m_ViewportSize.y));

		const SceneSerializer serializer(defaultScene);
		serializer.Serialize(scenePath);

		if (!std::filesystem::exists(scenePath, ec) || ec)
		{
			m_NotificationManager.AddNotification("Failed to create the default project scene.",
				Notification::Type::Error);
			return;
		}

		const auto newProject = Project::New(projectDirectory);

		ProjectInfo projInfo;
		projInfo.Name = projectName;
		projInfo.AssetDirectory = "Assets";
		projInfo.StartScenePath = std::filesystem::path("Scenes") / scenePath.filename();
		newProject->SetInfo(projInfo);

		/// Register the start scene immediately so the asset browser and the
		/// persisted registry know about it before the watcher fires.
		if (const Ref<EditorAssetManager> editorAssetManager = newProject->GetEditorAssetManager())
			editorAssetManager->ImportAsset(scenePath);

		m_ActiveScene = defaultScene;
		m_EditorScene = m_ActiveScene;
		m_ActiveScenePath = scenePath;
		m_HierarchyPanel.SetContext(m_ActiveScene);

		m_AssetsPanel = CreateOwner<AssetsPanel>(m_NotificationManager);

		ResetHoveredEntity();
		m_UndoRedo.Clear();

		if (!Project::SaveActive())
		{
			m_NotificationManager.AddNotification(
				"The project was created, but saving the project file failed.", Notification::Type::Error);
			return;
		}

		ScriptEngine::OnProjectOpened(projectDirectory, assemblyName);

		m_NotificationManager.AddNotification(
			"Created project '" + projectName + "' at " + projectDirectory.string(), Notification::Type::Info);
	}

	void EditorLayer::OpenProject(const std::filesystem::path& filepath)
	{
        KBR_TRACY_FUNCTION();

		/// The previous project's GPU resources may still be in use by an in-flight frame.
		VulkanContext::Get().WaitIdle();

		if (const auto project = Project::Load(filepath))
		{
			ScriptEngine::OnProjectOpened(Project::GetProjectDirectory(), Project::ToPascalCaseName(project->GetInfo().Name));

			const auto startScenePath = Project::GetAssetDirectory() / project->GetInfo().StartScenePath;
			OpenScene(startScenePath);

			m_AssetsPanel = CreateOwner<AssetsPanel>(m_NotificationManager);
		}
		else
		{
			Log::EditorError("Failed to open project: {0}", filepath.string());
			m_NotificationManager.AddNotification("Failed to open project.", Notification::Type::Warning);
		}
	}

	void EditorLayer::RequestOpenProject()
	{
        KBR_TRACY_FUNCTION();

		const std::string filepathString = FileDialog::OpenFile("Kerberos Project (*.kbrproj)\0*.kbrproj\0");
		if (filepathString.empty())
			return;

		const std::filesystem::path filepath = filepathString;

		/// Defer the project switch to the start of the next frame so the old
		/// project's GPU resources are released after the device is idle.
		Application::Get().SubmitToMainThreadQueue([this, filepath]() {
			OpenProject(filepath);
		});
	}

	bool EditorLayer::OpenProject()
	{
        KBR_TRACY_FUNCTION();

		const std::string filepathString = FileDialog::OpenFile("Kerberos Project (*.kbrproj)\0*.kbrproj\0");

		if (filepathString.empty())
			return false;

		OpenProject(filepathString);
		return true;
	}

	bool EditorLayer::CanSaveScene()
	{
		if (!m_ActiveScene)
		{
			m_NotificationManager.AddNotification("No active scene to save!", Notification::Type::Error);
			return false;
		}

		if (m_SceneState == SceneState::Play)
		{
			m_NotificationManager.AddNotification("Cannot save scene while in Play mode!", Notification::Type::Error);
			return false;
		}
		if (m_SceneState == SceneState::Simulate)
		{
			m_NotificationManager.AddNotification("Cannot save scene while in Simulate mode!", Notification::Type::Error);
			return false;
		}
		return true;
	}

	void EditorLayer::SaveScene()
	{
        KBR_TRACY_FUNCTION();

		const bool canSave = CanSaveScene();
		if (!canSave)
			return;

		const SceneSerializer serializer(m_ActiveScene);
		serializer.Serialize(m_ActiveScenePath.string());

		m_NotificationManager.AddNotification("Scene saved to " + m_ActiveScenePath.string(), Notification::Type::Info);

		Project::SaveActive();
	}

	void EditorLayer::SaveSceneAs()
	{
		KBR_TRACY_FUNCTION();

		const bool canSave = CanSaveScene();
		if (!canSave)
			return;

		const std::string filepath = FileDialog::SaveFile("Kerberos Scene (*.kerberos)\0*.kerberos\0");
		if (filepath.empty())
			return;

		const SceneSerializer serializer(m_ActiveScene);
		serializer.Serialize(filepath);

		// When saving a scene as a new file, we update the active scene path to the new file path
		m_ActiveScenePath = filepath;

		m_NotificationManager.AddNotification("Scene saved to " + filepath, Notification::Type::Info);

		Project::SaveActive();
	}

	void EditorLayer::LoadScene()
	{
		KBR_TRACY_FUNCTION();

		const std::string filepathString = FileDialog::OpenFile("Kerberos Scene (*.kerberos)\0*.kerberos\0");

		if (filepathString.empty())
			return;

		OpenScene(filepathString);
	}

	void EditorLayer::OpenScene(const std::filesystem::path& filepath)
	{
		KBR_TRACY_FUNCTION();

		if (m_SceneState != SceneState::Edit)
		{
			OnSceneStop();
		}

		ResetHoveredEntity();

		/// TODO: Prompt to save the current scene if there are unsaved changes

		const Ref<Scene> newScene = CreateRef<Scene>();
		const SceneSerializer serializer(newScene);

		if (!serializer.Deserialize(filepath))
		{
            Log::EditorInfo("Failed to load scene from {0}", filepath.string());
			return;
		}

		m_ActiveScenePath = filepath;

		m_EditorScene = newScene;
		m_EditorScene->OnViewportResize(static_cast<uint32_t>(m_ViewportSize.x), static_cast<uint32_t>(m_ViewportSize.y));
		m_HierarchyPanel.SetContext(m_EditorScene);

		m_UndoRedo.Clear();

		m_ActiveScene = m_EditorScene;
	}

	void EditorLayer::OpenScene(const Ref<Scene> &scene)
	{
		KBR_TRACY_FUNCTION();

        const auto& metadata = Project::GetActive()->GetEditorAssetManager()->GetMetadata(scene->GetHandle());

		OpenScene(Project::GetAssetFileSystemPath(metadata.Filepath));
	}

	void EditorLayer::NewScene()
	{
		KBR_TRACY_FUNCTION();

		m_ActiveScene = CreateRef<Scene>();
		m_ActiveScene->OnViewportResize(static_cast<uint32_t>(m_ViewportSize.x), static_cast<uint32_t>(m_ViewportSize.y));
		m_EditorScene = m_ActiveScene;
		m_ActiveScenePath.clear();

		ResetHoveredEntity();

		m_HierarchyPanel.SetContext(m_ActiveScene);
		m_UndoRedo.Clear();
	}

	void EditorLayer::DrawViewport()
	{
		KBR_TRACY_FUNCTION();

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::Begin("Viewport");
		ImGui::PopStyleVar();

		m_ViewportFocused = ImGui::IsWindowFocused();
		m_ViewportHovered = ImGui::IsWindowHovered();

		Application::Get().BlockEvents(!m_ViewportHovered);

		m_ViewportSize = {ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y};

		const auto viewportImage = Renderer::GetCompositedOutputImageID();
		ImGui::Image(viewportImage, ImVec2(m_ViewportSize.x, m_ViewportSize.y));
		const ImVec2 viewportMin = ImGui::GetItemRectMin();
		const ImVec2 viewportMax = ImGui::GetItemRectMax();

		HandleDragAndDrop();

		/// Set the bounds of the viewport to the actual image item.
		m_ViewportBounds[0] = {viewportMin.x, viewportMin.y};
		m_ViewportBounds[1] = {viewportMax.x, viewportMax.y};

		/// Gizmos
		const bool gizmosEnabled = m_SceneState == SceneState::Edit || m_SceneState == SceneState::Simulate;
		if (const Entity selectedEntity = m_HierarchyPanel.GetSelectedEntity(); selectedEntity && m_GizmoType != GizmoType::None && gizmosEnabled)
		{
			ImGuizmo::SetOrthographic(false);
			ImGuizmo::SetDrawlist();

			ImGuizmo::SetRect(viewportMin.x, viewportMin.y, m_ViewportSize.x, m_ViewportSize.y);

			/// Will be used for the Runtime camera
			/*const Entity cameraEntity = m_ActiveScene->GetPrimaryCameraEntity();
			const auto& camera = cameraEntity.GetComponent<CameraComponent>().Camera;

			const glm::mat4 cameraProjection = camera.GetProjection();
			const glm::mat4 cameraView = glm::inverse(cameraEntity.GetComponent<TransformComponent>().GetTransform());*/

			glm::mat4 cameraProjection = m_EditorCamera->GetProjectionMatrix();
			cameraProjection[1][1] *= -1.0f;
			const glm::mat4 cameraView = m_EditorCamera->GetViewMatrix();

			/// Entity transform
			auto &tc = selectedEntity.GetComponent<TransformComponent>();
			auto transform = tc.WorldTransform;

			/// Snapping
			const bool snap = Input::IsKeyPressed(Key::LeftControl);
			float snapValue = 0.5f;
			if (m_GizmoType == GizmoType::Rotate)
				snapValue = 45.0f;

			const float snapValues[3] = {snap ? snapValue : 0.0f, snap ? snapValue : 0.0f, snap ? snapValue : 0.0f};

			ImGuizmo::Manipulate(glm::value_ptr(cameraView), glm::value_ptr(cameraProjection),
													 static_cast<ImGuizmo::OPERATION>(m_GizmoType), ImGuizmo::WORLD, glm::value_ptr(transform), nullptr, snapValues);

			if (ImGuizmo::IsUsing())
			{
                glm::mat4 parentWorld = glm::mat4(1.0f);
                if (const Entity parent = m_ActiveScene->GetParent(selectedEntity); parent)
                    parentWorld = parent.GetComponent<TransformComponent>().WorldTransform;

                const glm::mat4 localTransform = glm::inverse(parentWorld) * transform;

				glm::vec3 translation, rotationDegrees, scale;
                ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(localTransform),
                                                      glm::value_ptr(translation),
                                                      glm::value_ptr(rotationDegrees),
                                                      glm::value_ptr(scale));

				tc.Translation = translation;
				tc.Rotation = glm::radians(rotationDegrees);
				tc.Scale = scale;

				CalculateEntityTransform(selectedEntity);
			}
		}

		ImGui::End();
	}

	void EditorLayer::DrawUIToolbar()
	{
		KBR_TRACY_FUNCTION();

		constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
		const auto &colors = ImGui::GetStyle().Colors;
		const auto &buttonHovered = colors[ImGuiCol_ButtonHovered];
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(buttonHovered.x, buttonHovered.y, buttonHovered.z, 0.5f));
		const auto &buttonActive = colors[ImGuiCol_ButtonActive];
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(buttonActive.x, buttonActive.y, buttonActive.z, 0.5f));

		ImGui::Begin("Toolbar", nullptr, flags);

		const float size = ImGui::GetWindowHeight() - 4.0f;

		if (m_SceneState == SceneState::Edit)
		{
			constexpr int columns = 2;
			ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(2.0f, 2.0f));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 2.0f));
			constexpr ImGuiTableFlags tableFlags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Resizable;
			ImGui::BeginTable("##ToolbarButtonTable", columns, tableFlags, ImVec2(ImGui::GetWindowWidth(), size));
			ImGui::PopStyleVar(2);

			ImGui::TableNextColumn();

			if (ImGui::ImageButton("PlayButton", VulkanContext::Get().GetImGuiRendererID(m_IconPlay), ImVec2(size, size), ImVec2(0, 0), ImVec2(1, 1)))
			{
				OnScenePlay();
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Play (Ctrl + P)");
			}

			ImGui::TableNextColumn();

			if (ImGui::ImageButton("SimulateButton", VulkanContext::Get().GetImGuiRendererID(m_IconPlay), ImVec2(size, size), ImVec2(0, 0), ImVec2(1, 1)))
			{
				OnSceneSimulate();
			}
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			{
				ImGui::SetTooltip("Simulate (Ctrl + L)");
			}

			ImGui::EndTable();
		}
		else
		{
			ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(0.0f, 0.0f));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 2.0f));

			constexpr int columns = 2;
			constexpr ImGuiTableFlags tableFlags = ImGuiTableFlags_None;
			ImGui::BeginTable("##ToolbarButtonTable", columns, tableFlags);

			// ImGui::SetCursorPosX((ImGui::GetWindowContentRegionMax().x * 0.5f) - (size * 0.5f));

			ImGui::TableNextColumn();

			if (m_IsScenePaused)
			{
				if (ImGui::ImageButton("ResumeButton", VulkanContext::Get().GetImGuiRendererID(m_IconResume), ImVec2(size, size), ImVec2(0, 0), ImVec2(1, 1)))
				{
					m_IsScenePaused = false;
					m_ActiveScene->SetScenePaused(m_IsScenePaused);
				}
				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("Resume");
				}
			}
			else
			{
				if (ImGui::ImageButton("PauseButton", VulkanContext::Get().GetImGuiRendererID(m_IconPause), ImVec2(size, size), ImVec2(0, 0), ImVec2(1, 1)))
				{
					m_IsScenePaused = true;
					m_ActiveScene->SetScenePaused(m_IsScenePaused);
				}
				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("Pause");
				}
			}

			ImGui::TableNextColumn();

			if (ImGui::ImageButton("StopButton", VulkanContext::Get().GetImGuiRendererID(m_IconStop), ImVec2(size, size), ImVec2(0, 0), ImVec2(1, 1)))
			{
				OnSceneStop();
			}

			if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			{
				if (m_SceneState == SceneState::Play)
					ImGui::SetTooltip("Stop (Ctrl + P)");
				else if (m_SceneState == SceneState::Simulate)
					ImGui::SetTooltip("Stop Simulation (Ctrl + L)");
			}

			ImGui::EndTable();

			ImGui::PopStyleVar(2);
		}

		ImGui::PopStyleColor(3);

		ImGui::End();
	}

	void EditorLayer::DrawMenuBar()
	{
		KBR_TRACY_FUNCTION();

		if (ImGui::BeginMenuBar())
		{
			if (ImGui::BeginMenu("File"))
			{
				if (ImGui::MenuItem("Exit"))
				{
					/// TODO: Show a confirmation dialog and whether to save the scene if there are unsaved changes
					Application::Get().Close();
				}

				ImGui::MenuItem("Fullscreen", nullptr, &m_IsFullScreenPersistent);

				if (ImGui::MenuItem("Open Project...", "Ctrl+Shift+O"))
                {
                    RequestOpenProject();
                }

				if (ImGui::MenuItem("New Project...", "Ctrl+Shift+N"))
                {
                    NewProject();
                }

				if (ImGui::MenuItem("New Scene", "Ctrl+N"))
				{
					NewScene();
				}

				if (ImGui::MenuItem("Save", "Ctrl+S"))
				{
					SaveScene();
				}

				if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S"))
				{
					SaveSceneAs();
				}

				if (ImGui::MenuItem("Load...", "Ctrl+O"))
				{
					LoadScene();
				}

				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Edit"))
			{
				const bool canEdit = m_SceneState == SceneState::Edit;
				const bool canUndo = canEdit && m_UndoRedo.CanUndo();
				const bool canRedo = canEdit && m_UndoRedo.CanRedo();

				const std::string undoName = m_UndoRedo.GetUndoName();
				const std::string redoName = m_UndoRedo.GetRedoName();
				const std::string undoLabel = (undoName.empty() ? "Undo" : "Undo " + undoName) + "##Undo";
				const std::string redoLabel = (redoName.empty() ? "Redo" : "Redo " + redoName) + "##Redo";

				if (ImGui::MenuItem(undoLabel.c_str(), "Ctrl+Z", false, canUndo))
				{
					Undo();
				}
				if (ImGui::MenuItem(redoLabel.c_str(), "Ctrl+Y", false, canRedo))
				{
					Redo();
				}

				ImGui::Separator();

				if (ImGui::MenuItem("Cut", "Ctrl+X", false, false))
				{
				}
				if (ImGui::MenuItem("Copy", "Ctrl+C", false, false))
				{
				}
				if (ImGui::MenuItem("Paste", "Ctrl+V", false, false))
				{
				}
				if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, false))
				{
				}
				ImGui::Separator();
				if (ImGui::MenuItem("Delete", "Del", false, false))
				{
				}
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("View"))
			{
				if (ImGui::MenuItem("Show Wireframe"))
				{
					/*m_ShowWireframe = !m_ShowWireframe;
					Renderer3D::SetShowWireframe(m_ShowWireframe);*/
				}
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Debug"))
			{
				if (ImGui::MenuItem("Reload C# assemblies", "", nullptr, m_SceneState == SceneState::Edit))
				{
					ScriptEngine::ReloadAssembly();
				}

				if (ImGui::MenuItem("Recompile shaders", "", nullptr, m_SceneState == SceneState::Edit))
				{
					Renderer::RecompileShaders();
				}

				ImGui::EndMenu();
			}

			ImGui::EndMenuBar();
		}
	}

	void EditorLayer::Undo()
	{
		m_UndoRedo.Undo();
		RefreshSelectionAfterHistoryChange();
	}

	void EditorLayer::Redo()
	{
		m_UndoRedo.Redo();
		RefreshSelectionAfterHistoryChange();
	}

	void EditorLayer::RefreshSelectionAfterHistoryChange()
	{
		const Entity selected = m_HierarchyPanel.GetSelectedEntity();
		if (selected && m_ActiveScene && !m_ActiveScene->IsEntityValid(selected))
			m_HierarchyPanel.SetSelectedEntity({});

		/// Undo/redo can destroy the hovered entity without going through the
		/// hierarchy's deletion callback, so its handle has to be invalidated here.
		if (m_HoveredEntity && m_ActiveScene && !m_ActiveScene->IsEntityValid(m_HoveredEntity))
		{
            ResetHoveredEntity();
		}
	}

	void EditorLayer::DrawDebugWindow() const
	{
		KBR_TRACY_FUNCTION();

		ImGui::Begin("Debug");

		ImGui::Text("Time: %.2f seconds", m_Time);
		ImGui::Text("FPS: %.2f", m_Fps);

		ImGui::SeparatorText("GPU Timings");
		const GPUTimings gpuTimings = Renderer::GetLatestGPUTimings();
		if (gpuTimings.IsValid)
		{
			ImGui::Text("Frame: %.3f ms", gpuTimings.FrameMilliseconds);
			ImGui::Text("Depth Pre-Pass: %.3f ms", gpuTimings.DepthPrePassMilliseconds);
			ImGui::Text("Shadow: %.3f ms", gpuTimings.ShadowPassMilliseconds);
			ImGui::Text("Opaque: %.3f ms", gpuTimings.OpaquePassMilliseconds);
			ImGui::Text("Grass: %.3f ms", gpuTimings.GrassPassMilliseconds);
			ImGui::Text("Particles Simulate: %.3f ms", gpuTimings.ParticlesSimulateMilliseconds);
			ImGui::Text("Particles Draw: %.3f ms", gpuTimings.ParticlesDrawMilliseconds);
			ImGui::Text("Transparent: %.3f ms", gpuTimings.TransparentPassMilliseconds);
			ImGui::Text("Transparency Resolve: %.3f ms", gpuTimings.TransparencyResolvePassMilliseconds);
			ImGui::Text("Bloom: %.3f ms", gpuTimings.BloomPassMilliseconds);
			ImGui::Text("Ambient Occlusion: %.3f ms", gpuTimings.AmbientOcclusionPassMilliseconds);
			ImGui::Text("Antialiasing: %.3f ms", gpuTimings.AntialiasingPassMilliseconds);
			ImGui::Text("Tonemapping: %.3f ms", gpuTimings.TonemappingPassMilliseconds);
			ImGui::Text("Upscaling: %.3f ms", gpuTimings.UpscalingPassMilliseconds);
		}
		else
		{
			ImGui::Text("GPU timings: unavailable");
		}

		ImGui::SeparatorText("Renderer Statistics");
		const RenderStatistics renderStatistics = Renderer::GetLatestRenderStatistics();
		if (renderStatistics.IsValid)
		{
			ImGui::Text("Rendered objects: %u", renderStatistics.RenderObjectCount);
			ImGui::Text("Unique materials: %u", renderStatistics.UniqueMaterialCount);
			ImGui::Text("Vertices: %u", renderStatistics.VertexCount);
			ImGui::Text("Indices: %u", renderStatistics.IndexCount);
			ImGui::Text("Faces: %u", renderStatistics.FaceCount);
			ImGui::Text("Collider line vertices: %u", renderStatistics.ColliderLineVertexCount);
		}
		else
		{
			ImGui::Text("Render statistics: unavailable");
		}

		ImGui::SeparatorText("Pipeline Statistics");
		const PipelineStatistics pipelineStatistics = Renderer::GetLatestPipelineStatistics();
		if (true /*pipelineStatistics.IsValid*/)
		{
			ImGui::Text("Input Assembly Vertices: %u", pipelineStatistics.InputAssemblyVertices);
			ImGui::Text("Input Assembly Primitives: %u", pipelineStatistics.InputAssemblyPrimitives);
			ImGui::Text("Vertex Shader Invocations: %u", pipelineStatistics.VertexShaderInvocations);
			ImGui::Text("Task Shader Invocations: %u", pipelineStatistics.TaskShaderInvocations);
			ImGui::Text("Mesh Shader Invocations: %u", pipelineStatistics.MeshShaderInvocations);
			ImGui::Text("Fragment Shader Invocations: %u", pipelineStatistics.FragmentShaderInvocations);
		}
		else
		{
			ImGui::Text("Pipeline statistics: unavailable");
		}

		ImGui::Separator();

		ImGui::Text("EditorCamera");
		const auto &camPos = m_EditorCamera->GetPosition();
		ImGui::Text("Position: (%.2f, %.2f, %.2f)", camPos.x, camPos.y, camPos.z);
		ImGui::Text("Rotation: (Pitch: %.2f, Yaw: %.2f)", m_EditorCamera->GetPitch(), m_EditorCamera->GetYaw());
		ImGui::Text("Distance: %.2f", m_EditorCamera->GetDistance());

		float nearClip = m_EditorCamera->GetNearClip();
		if (ImGui::DragFloat("Near clip", &nearClip, 0.1f, 0.01f, 100.0f))
		{
			m_EditorCamera->SetNearClip(nearClip);
		}

		float farClip = m_EditorCamera->GetFarClip();
		if (ImGui::DragFloat("Far clip", &farClip, 1.0f, 1.0f))
		{
			m_EditorCamera->SetFarClip(farClip);
		}

		float moveSpeed = m_EditorCamera->GetMoveSpeed();
		if (ImGui::DragFloat("Camera speed", &moveSpeed, 0.1f, 0.1f, 100.0f))
		{
			m_EditorCamera->SetMoveSpeed(moveSpeed);
		}

		glm::vec3 focalPoint = m_EditorCamera->GetFocalPoint();
		ImGui::DragFloat3("Focal point", glm::value_ptr(focalPoint), 0.1f, 0.0f, 0.0f, "%.3f", ImGuiSliderFlags_NoInput);

		ImGui::Separator();

		const std::string activeGizmoTypeString = GetActiveGizmoTypeString();
		ImGui::Text("Gizmo Type: %s", activeGizmoTypeString.c_str());
        ImGui::Text("Viewport size: %.2f x %.2f", m_ViewportSize.x, m_ViewportSize.y);
		ImGui::Text("Viewport Focused: %s", m_ViewportFocused ? "Yes" : "No");
		ImGui::Text("Viewport Hovered: %s", m_ViewportHovered ? "Yes" : "No");
		ImGui::Text("ImGui Capturing Input: %s", m_DoesImGuiWantInput ? "Yes" : "No");
        const glm::vec2 renderImageSize = Renderer::GetRenderImageSize();
        ImGui::Text("Render image size: %.2f x %.2f", renderImageSize.x, renderImageSize.y);

		std::string hoveredEntityName = "None";
		if (m_HoveredEntity && m_ActiveScene && m_ActiveScene->IsEntityValid(m_HoveredEntity))
		{
			hoveredEntityName = m_HoveredEntity.GetComponent<TagComponent>().Tag;
		}
		ImGui::Text("Hovered entity: %s", hoveredEntityName.c_str());

		ImGui::Separator();

		ImGui::Text("Frustum culling");

		ImGui::Checkbox("Use Frustum Culling", &Renderer::GetUseFrustumCulling());
		ImGui::Checkbox("Freeze Frustum", &Renderer::GetFreezeFrustum());
		ImGui::Text("All objects: %u", Renderer::GetAllObjectCount());
		ImGui::Text("Visible objects: %u", Renderer::GetVisibleObjectCount());
		ImGui::Text("Culled objects: %u", Renderer::GetCulledObjectCount());

		ImGui::Separator();

		// Light controls
		/*ImGui::Text("Light directions");
		ImGui::DragFloat3("Light 1 Direction", glm::value_ptr(m_UniformDataParams.lights[0]), 0.1f);
		ImGui::Text("Light 2: (%.2f, %.2f, %.2f)", m_UniformDataParams.lights[1].x, m_UniformDataParams.lights[1].y, m_UniformDataParams.lights[1].z);
		ImGui::Text("Light 2: (%.2f, %.2f, %.2f)", m_UniformDataParams.lights[2].x, m_UniformDataParams.lights[2].y, m_UniformDataParams.lights[2].z);
		ImGui::DragFloat3("Light 4 Direction", glm::value_ptr(m_UniformDataParams.lights[3]), 0.1f);*/
		ImGui::Text("Lighting settings");
		float &exposure = Renderer::GetExposure();
		ImGui::DragFloat("Exposure", &exposure, 0.1f, 0.1f, 10.0f);
		float &gamma = Renderer::GetGamma();
		ImGui::DragFloat("Gamma", &gamma, 0.1f, 0.1f, 10.0f);
		// ImGui::DragFloat3("Ambient Light Color", glm::value_ptr(m_SceneUniformData.ambientLightColor), 0.01f, 0.0f, 1.0f);

		ImGui::Separator();

		// Display shadow map
		ImGui::Text("Shadow Map");
		static int shadowMapCascadeIndex = 0;
		ImGui::DragInt("Shadow Map Cascade Index", &shadowMapCascadeIndex, 0.1f, 0, static_cast<int>(Renderer::GetShadowMapCascadeCount()) - 1);
		const auto shadowMapImage = Renderer::GetShadowMapDepthImageID(shadowMapCascadeIndex);
		ImGui::Image(shadowMapImage, ImVec2(256.0f, 256.0f));

		/*const glm::vec3 lightPosForShadowMapCalculation = Renderer::GetLightPositionForShadowMapCalculation();
		ImGui::Text("Light position for shadow map calculation:");
		ImGui::Text("(%.2f, %.2f, %.2f)",
					lightPosForShadowMapCalculation.x,
					lightPosForShadowMapCalculation.y,
					lightPosForShadowMapCalculation.z);*/

		ImGui::Text("Depth Bias");
		auto &[ConstantFactor, SlopeFactor, Clamp] = Renderer::GetShadowMapDepthBiasSettings();
		ImGui::DragFloat("Constant Factor", &ConstantFactor, 0.001f, 0.0f, 5.0f);
		ImGui::DragFloat("Clamp", &Clamp, 0.001f, 0.0f, 1.0f);
		ImGui::DragFloat("Slope Factor", &SlopeFactor, 0.01f, 0.0f, 10.0f);

		ImGui::Separator();

		ImGui::Text("Font Atlas");
		const uint64_t fontAtlasTextureID = VulkanContext::Get().GetImGuiRendererID(m_BasicFont->GetAtlasTexture());
		ImGui::Image(fontAtlasTextureID, ImVec2{256, 256}, ImVec2{0, 1}, ImVec2{1, 0});

		ImGui::Separator();

		// Scene settings
		ImGui::Text("Settings");
		ImGui::Checkbox("Display Skybox", &Renderer::GetDisplaySkybox());
		ImGui::Checkbox("Display normals", &Renderer::GetDisplayDebugNormals());
		ImGui::Checkbox("Display physics colliders", &Renderer::GetDisplayPhysicsColliders());

		bool &useRayQueryBasedShadows = Renderer::GetUseRayQueryBasedShadows();
		ImGui::Checkbox("Ray query based shadows", &useRayQueryBasedShadows);
		if (useRayQueryBasedShadows)
		{
			ImGui::Indent();
			ImGui::Checkbox("Ray query soft shadows", &Renderer::GetUseRayQueryBasedSoftShadows());
			ImGui::Unindent();
		}
		else
		{
			ImGui::Checkbox("Enable PCF", &Renderer::GetIsPCFEnabledForShadowMap());
		}

		UpscalerType upscalingMode = Renderer::GetUpscalingMode();
		const char *upscalingModeItems[] = {"Disabled", "FSR3"};
		if (ImGui::Combo("Upscaling mode", reinterpret_cast<int*>(&upscalingMode), upscalingModeItems, IM_ARRAYSIZE(upscalingModeItems)))
		{
            Renderer::SetUpscalingMode(static_cast<UpscalerType>(upscalingMode));
		}
        if (upscalingMode != UpscalerType::Native) 
		{
            ImGui::Indent();

            UpscalerQuality upscalingQuality = Renderer::GetUpscalingQuality();
            const char* upscalingModeQualityItems[] = { "Ultra Quality", "Quality", "Balanced", "Performance", "Ultra Performance" };
            if (ImGui::Combo("Quality",
                             reinterpret_cast<int*>(&upscalingQuality),
                             upscalingModeQualityItems,
                             IM_ARRAYSIZE(upscalingModeQualityItems))) {
                Renderer::SetUpscalingQuality(static_cast<UpscalerQuality>(upscalingQuality));
            }

			ImGui::Unindent();
        }
        /*if (upscalingMode == UpscalerType::FSR3) {
            ImGui::Indent();
            ImGui::Checkbox("Use FSR3 Auto Exposure", &Renderer::GetUseFSR3AutoExposure());
            ImGui::Unindent();
        }*/

		bool& useGTAO = Renderer::GetUseGTAO();
		ImGui::Checkbox("Use GTAO", &useGTAO);
		if (useGTAO)
		{
			ImGui::Indent();

			ImGui::Checkbox("Blur GTAO image", &Renderer::GetUseBlurForGTAO());

			GTAOConstants &gtaoConstants = Renderer::GetGTAOConstants();
			ImGui::DragFloat("Radius", &gtaoConstants.radius, 0.01f, 0.01f, 10.0f);
			ImGui::DragFloat("Falloff", &gtaoConstants.falloff, 0.01f, 0.01f, 10.0f);
			ImGui::DragFloat("Sample Count", &gtaoConstants.sampleCount, 0.1f, 1.0f, 16.0f);
			ImGui::DragFloat("Direction Count", &gtaoConstants.directionCount, 0.1f, 1.0f, 8.0f);

			ImGui::Unindent();
		}

		AntiAliasingMode &aaMode = Renderer::GetAntiAliasingMode();
		const char *aaModeItems[] = {"None", "FXAA", "SMAA", "TAA"};
		if (ImGui::Combo("Anti-aliasing mode", reinterpret_cast<int *>(&aaMode), aaModeItems, IM_ARRAYSIZE(aaModeItems)))
		{
			Renderer::GetAntiAliasingMode() = static_cast<AntiAliasingMode>(aaMode);
		}

		TonemappingOperator &tonemappingOperator = Renderer::GetTonemappingOperator();
		const char *tonemappingOperatorItems[] = {"Uncharted 2", "Reinhard", "ACES", "ACES Fitted"};
		if (ImGui::Combo("Tonemapping operator", reinterpret_cast<int *>(&tonemappingOperator), tonemappingOperatorItems, IM_ARRAYSIZE(tonemappingOperatorItems)))
		{
			Renderer::GetTonemappingOperator() = static_cast<TonemappingOperator>(tonemappingOperator);
		}

		if (ImGui::CollapsingHeader("Bloom", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Indent();

			ImGui::DragFloat("Bloom Intensity", &Renderer::GetBloomIntensity(), 0.01f, 0.0f, 1.0f);

			BloomMode &bloomMode = Renderer::GetBloomMode();
			const char *bloomModeItems[] = {"Legacy", "Bright-pass prefilter"};
			if (ImGui::Combo("Bloom mode", reinterpret_cast<int *>(&bloomMode), bloomModeItems, IM_ARRAYSIZE(bloomModeItems)))
			{
				Renderer::GetBloomMode() = static_cast<BloomMode>(bloomMode);
			}

			if (Renderer::GetBloomMode() == BloomMode::BrightPassPrefilter)
			{
				ImGui::DragFloat("Bloom Threshold", &Renderer::GetBloomThreshold(), 0.01f, 0.0f, 20.0f);
				ImGui::DragFloat("Bloom Knee", &Renderer::GetBloomKnee(), 0.005f, 0.0f, 5.0f);
				ImGui::DragFloat("Max Bloom Brightness", &Renderer::GetBloomMaxBrightness(), 0.5f, 0.0f);
			}

			ImGui::Unindent();
		}

		ImGui::Separator();

		const auto memoryBudgetInfo = VulkanContext::Get().GetMemoryBudgetInfo();

		auto convertedMemory = MemoryBudget::ConvertBytes(memoryBudgetInfo.DeviceMemoryTotalUsage);
		ImGui::Text("Total memory usage: %.2f %s", convertedMemory.data, convertedMemory.units.c_str());

		convertedMemory = MemoryBudget::ConvertBytes(memoryBudgetInfo.DeviceMemoryTotalBudget);
		ImGui::Text("Total memory budget: %.2f %s", convertedMemory.data, convertedMemory.units.c_str());
		if (ImGui::CollapsingHeader("Detailed Memory Usage"))
		{
			ImGui::Indent();

			for (uint32_t i = 0; i < static_cast<uint32_t>(memoryBudgetInfo.DeviceMemoryHeapCount); i++)
			{
				std::string header = "Memory Heap Index: " + std::to_string(i);
				if (ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
				{
					convertedMemory = MemoryBudget::ConvertBytes(memoryBudgetInfo.MemoryBudgetProps.heapUsage[i]);
					ImGui::Text("Usage: %.2f %s", convertedMemory.data, convertedMemory.units.c_str());

					convertedMemory = MemoryBudget::ConvertBytes(memoryBudgetInfo.MemoryBudgetProps.heapBudget[i]);
					ImGui::Text("Budget: %.2f %s", convertedMemory.data, convertedMemory.units.c_str());
					ImGui::Text("Heap Flag: %s", MemoryBudget::ReadMemoryHeapFlags(memoryBudgetInfo.DeviceMemoryProps.memoryHeaps[i].flags).c_str());
				}
			}

			ImGui::Unindent();
		}

		ImGui::End();
	}

	void EditorLayer::CalculateEntityTransform(const Entity &entity) const
	{
		KBR_TRACY_FUNCTION();

		m_ActiveScene->CalculateEntityTransform(entity);
	}

	bool EditorLayer::OnKeyPressed(const KeyPressedEvent &event)
	{
		/// Shortcuts
		if (event.GetRepeatCount() > 0)
			return false;

		const bool ctrl = Input::IsKeyPressed(Key::LeftControl) || Input::IsKeyPressed(Key::RightControl);
		const bool shift = Input::IsKeyPressed(Key::LeftShift) || Input::IsKeyPressed(Key::RightShift);

		const bool isInputCaptured = ImGui::GetIO().WantTextInput || ImGui::IsAnyItemActive();
        const bool isInEditMode = m_SceneState == SceneState::Edit;

		switch (event.GetKeyCode())
		{
		case Key::S:
			if (ctrl && shift)
			{
				SaveSceneAs();
			}
			else if (ctrl)
			{
				SaveScene();
			}
			break;
		case Key::N:
			if (ctrl && shift)
			{
				NewProject();
			}
			else if (ctrl)
			{
				NewScene();
			}
			break;
		case Key::O:
			if (ctrl && shift)
			{
				RequestOpenProject();
			}
			else if (ctrl)
			{
				LoadScene();
			}
			break;
		case Key::Z:
			if (ctrl && isInEditMode && !ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive())
			{
				Undo();
			}
			break;
		case Key::Y:
			if (ctrl && isInEditMode && !ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive())
			{
				Redo();
			}
			break;

			/// Gizmos
		case Key::Q: 
		    if (!isInputCaptured)
		    {
			    m_GizmoType = GizmoType::None;
		    }
			break;
		case Key::W:
            if (!isInputCaptured)
            {
			    m_GizmoType = GizmoType::Translate;
            }
            break;
		case Key::E:
            if (!isInputCaptured)
            {
			    m_GizmoType = GizmoType::Scale;
            }
            break;
		case Key::R:
            if (!isInputCaptured)
            {
			    m_GizmoType = GizmoType::Rotate;
            }
            break;
		case Key::F:
		{
			if (const Entity &entity = m_HierarchyPanel.GetSelectedEntity())
			{
				m_EditorCamera->Focus(entity.GetComponent<TransformComponent>().Translation);
			}
			break;
		}
		default:
			break;
		}

		return false;
	}

	bool EditorLayer::OnMouseButtonPressed(const MouseButtonPressedEvent &event)
	{
		/// Handle mouse picking
		/// Only select the entity if we are not using the gizmos or the camera
		/// TODO: Check if isViewportHovered is needed, Application::BlockEvents should prevent this from firing
		if (event.GetButton() == Mouse::ButtonLeft && !ImGuizmo::IsOver() && !Input::IsKeyPressed(Key::LeftAlt) && m_ViewportHovered)
		{
			if (m_HoveredEntity && m_ActiveScene && m_ActiveScene->IsEntityValid(m_HoveredEntity))
				m_HierarchyPanel.SetSelectedEntity(m_HoveredEntity);
			else
				m_HierarchyPanel.SetSelectedEntity({});
		}
		return false;
	}

	bool EditorLayer::OnWindowDrop([[maybe_unused]] const WindowDropEvent &event)
	{
		/// TODO: Implement file dropping to load scenes or import models
		throw std::logic_error("Not implemented");
	}

	std::string EditorLayer::GetActiveGizmoTypeString() const
	{
		switch (m_GizmoType)
		{
		case GizmoType::None:
			return "None";
		case GizmoType::Translate:
			return "Translate";
		case GizmoType::Rotate:
			return "Rotate";
		case GizmoType::Scale:
			return "Scale";
		}

		KBRAssert(false, "Invalid gizmo type");
		return "";
	}

	bool EditorLayer::DoesImGuiWantInput()
	{
		// Check if any ImGui item is actively being interacted with
		return ImGui::IsAnyItemActive() || ImGui::IsAnyItemFocused();
	}

    std::filesystem::path EditorLayer::LookForNameForNewScene(const std::filesystem::path& startingPath, const std::string& defaultName)
    {
        const std::filesystem::path defaultPath = startingPath / defaultName;
        if (!std::filesystem::exists(defaultPath))
        {
            return defaultPath;
        }

        // If the default name is taken, try to find a unique name
        int counter = 1;
        while (true)
        {
            const std::filesystem::path newPath = startingPath / (defaultName + std::to_string(counter));
            if (!std::filesystem::exists(newPath))
            {
                return newPath;
            }
            counter++;
        }
    }

    void EditorLayer::ResetHoveredEntity()
    {
        m_HoveredEntity = {};
        m_HoveredEntityDeletedLastFrame = true;
    }
}
