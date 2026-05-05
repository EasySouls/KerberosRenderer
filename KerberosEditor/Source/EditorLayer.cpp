#include "EditorLayer.hpp"

#include <ranges>

#include "VulkanContext.hpp"
#include "IO.hpp"
#include "Renderer/Renderer.hpp"
#include "Application.hpp"
#include "AssetConstants.hpp"
#include "Assets/AssetManager.hpp"
#include "Assets/Importers/TextureImporter.hpp"
#include "Debug/Instrumentor.hpp"
#include "ImGuizmo/ImGuizmo.h"
#include "Input/InputSystem.hpp"
#include "Utils/SystemOperations.hpp"
#include "ModelLoader.hpp"
#include "Scene/Camera/FirstPersonCamera.hpp"
#include "Serialization/SceneSerializer.hpp"
#include "Input/KeyCodes.hpp"
#include "Logging/Log.hpp"
#include "Scripting/ScriptEngine.hpp"

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui/imgui.h>
#include <limits>


namespace Kerberos
{
	EditorLayer::EditorLayer() 
		: Layer("EditorLayer")
	{
	}

	EditorLayer::~EditorLayer() 
	{
		m_SceneNodes.clear();
	}

	void EditorLayer::OnAttach() 
	{
		KBR_CORE_INFO("EditorLayer attached!");

		constexpr bool isTesting = true;
		if (isTesting)
		{
			OpenProject("TestProject.kbrproj");
		}
		else
		{
			/// If there is a command line argument, try to open the project specified in it
			const auto& [Count, Args] = Application::Get().GetSpecification().CommandLineArgs;
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

		m_EditorCamera = std::make_unique<FirstPersonCamera>(45.0f, 16.0f / 9.0f, 0.1f, 1000.0f);
		m_EditorCamera->SetFlipY(true);
		m_EditorCamera->SetPosition(glm::vec3(0.0f, 15.0f, -45.0f));
		m_EditorCamera->SetRotation(glm::vec3(0.0f, 90.0f, 0.0f));

		m_ViewportSize = { 1280.0f, 720.0f };

		constexpr GLTFLoadingFlags loadingFlags = GLTFLoadingFlags::None;

		// Load models
		m_Meshes["avocado"] = CreateRef<Mesh>(ModelLoader::LoadModel("assets/models/avocado/Avocado.gltf", loadingFlags));
		m_Meshes["cube"] = CreateRef<Mesh>(ModelLoader::LoadModel("assets/models/cube.gltf", loadingFlags));
		m_Meshes["sphere"] = CreateRef<Mesh>(ModelLoader::LoadModel("assets/models/sphere.gltf", loadingFlags));
		m_Meshes["cerberus"] = CreateRef<Mesh>(ModelLoader::LoadModel("assets/models/cerberus/cerberus.gltf", loadingFlags));

		KBR_CORE_INFO("Loaded {} mesh(es)!", m_Meshes.size());

		const std::vector<std::pair<std::string, vk::Format>> textureFiles = {
			{ "assets/models/avocado/Avocado_baseColor.ktx2", vk::Format::eR8G8B8A8Srgb },
			{ "assets/models/avocado/Avocado_normal.ktx2", vk::Format::eR8G8B8A8Unorm },
			{ "assets/textures/stonefloor01_color_rgba.ktx", vk::Format::eR8G8B8A8Srgb },
			{ "assets/textures/stonefloor01_normal_rgba.ktx", vk::Format::eR8G8B8A8Unorm },
			{ "assets/textures/stonefloor02_color_rgba.ktx", vk::Format::eR8G8B8A8Srgb },
			{ "assets/textures/stonefloor02_normal_rgba.ktx", vk::Format::eR8G8B8A8Unorm },

			{ "assets/models/cerberus/albedo.ktx", vk::Format::eR8G8B8A8Unorm },
			{ "assets/models/cerberus/normal.ktx", vk::Format::eR8G8B8A8Unorm },
			{ "assets/models/cerberus/ao.ktx", vk::Format::eR8Unorm },
			{ "assets/models/cerberus/metallic.ktx", vk::Format::eR8Unorm },
			{ "assets/models/cerberus/roughness.ktx", vk::Format::eR8Unorm },
		};

		m_Textures.reserve(textureFiles.size());
		for (const auto& filepath : textureFiles | std::views::keys)
		{
			auto texture = Texture2D::FromFile(filepath);
			m_Textures.push_back(texture);
		}

		KBR_CORE_INFO("Loaded {} texture(s)!", m_Textures.size());

		const auto& avocadoMaterial = m_MaterialRegistry.AddAndRetrieve("Avocado", std::make_shared<Material>("Avocado", glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), 0.9f, 0.03f, m_Textures[0], m_Textures[1]));
		const auto& stoneFloorMaterial = m_MaterialRegistry.AddAndRetrieve("Stone Floor", std::make_shared<Material>("Stone Floor", glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), 0.8f, 0.05f, m_Textures[2], m_Textures[3]));
		const auto& stoneFloor2Material = m_MaterialRegistry.AddAndRetrieve("Stone Floor 2", std::make_shared<Material>("Stone Floor 2", glm::vec4(0.4f, 0.15f, 0.0f, 1.0f), 1.0f, 0.0f, m_Textures[4], m_Textures[5]));
		const auto& cerberusMaterial = m_MaterialRegistry.AddAndRetrieve("Cerberus", std::make_shared<Material>("Cerberus", glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), 1.0f, 0.0f, m_Textures[6], m_Textures[7]));

		m_SceneNodes.push_back(CreateOwner<Node>(Node{
			.Position = glm::vec3(6.0f, 9.5f, 0.0f),
			.Rotation = glm::vec3(0.0f),
			.Scale = glm::vec3(50.0f),
			.Mesh = m_Meshes["avocado"],
			.Material = avocadoMaterial,
			.Name = "Avocado"
		}));

		m_SceneNodes.push_back(CreateOwner<Node>(Node{
			.Position = glm::vec3(2.0f, 0.0f, 0.0f),
			.Rotation = glm::vec3(0.0f),
			.Scale = glm::vec3(1.0f),
			.Mesh = m_Meshes["cube"],
			.Material = stoneFloorMaterial,
			.Name = "Cube"
		}));

		m_SceneNodes.push_back(CreateOwner<Node>(Node{
			.Position = glm::vec3(2.0f, 6.0f, 3.0f),
			.Rotation = glm::vec3(0.0f),
			.Scale = glm::vec3(1.0f),
			.Mesh = m_Meshes["sphere"],
			.Material = m_MaterialRegistry.Get("Avocado"),
			.Name = "Sphere"
		}));

		m_SceneNodes.push_back(CreateOwner<Node>(Node{
			.Position = glm::vec3(2.0f, -10.0f, 3.0f),
			.Rotation = glm::vec3(0.0f),
			.Scale = glm::vec3(20.0f, 0.1f, 20.0f),
			.Mesh = m_Meshes["cube"],
			.Material = stoneFloor2Material,
			.Name = "Floor"
		}));

		m_SceneNodes.push_back( CreateOwner<Node>(Node{
			.Position = glm::vec3(-8.0f, 10.0f, 8.0f),
			.Rotation = glm::vec3(-1.6f, 1.4, 0.0),
			.Scale = glm::vec3(8.0f),
			.Mesh = m_Meshes["cerberus"],
			.Material = cerberusMaterial,
			.Name = "Revolver"
		}));

		m_AssetsPanel = CreateOwner<AssetsPanel>(m_NotificationManager);

		m_IconPlay = TextureImporter::ImportTexture("Assets/Editor/play_button.png");
		m_IconStop = TextureImporter::ImportTexture("Assets/Editor/stop_button.png");
		m_IconPause = TextureImporter::ImportTexture("Assets/Editor/pause_button.png");
		m_IconResume = TextureImporter::ImportTexture("Assets/Editor/outlined_play_button.png");
	}
	
	void EditorLayer::OnDetach() 
	{
		KBR_CORE_INFO("EditorLayer detached!");
	}

	void EditorLayer::OnUpdate(const float deltaTime)
	{
		KBR_PROFILE_FUNCTION();

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
			KBR_PROFILE_SCOPE("EditorCamera::OnUpdate");

			switch (m_SceneState)
			{
				case SceneState::Edit:
				case SceneState::Simulate:
					m_EditorCamera->OnUpdate(deltaTime);
					break;
				case SceneState::Play:
				{
					/// Only update the camera when the viewport is focused
					/*if (m_ViewportFocused)
						m_CameraController.OnUpdate(deltaTime);*/
					break;
				}
			}
		}

		{
			KBR_PROFILE_SCOPE("Scene::OnUpdate");

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
					m_ActiveScene->OnUpdateRuntime(deltaTime, *m_EditorCamera);
					break;
			}
		}

		{
			KBR_PROFILE_SCOPE("HandleMousePicking");

			HandleMousePicking();
		}

		m_CameraEntity = m_ActiveScene->GetPrimaryCameraEntity();
	}

	void EditorLayer::OnEvent(Event& event)
	{
		m_EditorCamera->OnEvent(event);

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
		static bool dockspaceOpen = true;
		static ImGuiDockNodeFlags dockspaceFlags = ImGuiDockNodeFlags_None;

		ImGuiWindowFlags windowFlags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;

		if (m_IsFullScreenPersistent)
		{
			const ImGuiViewport* viewport = ImGui::GetMainViewport();
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

		//TODO const std::string sceneName = m_ActiveScene ? m_ActiveScene->GetName() : "No Scene Loaded";
		const std::string sceneName = "Example Scene";

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::Begin(sceneName.c_str(), &dockspaceOpen, windowFlags);
		ImGui::PopStyleVar();

		if (m_IsFullScreenPersistent)
			ImGui::PopStyleVar(2);

		const ImGuiIO& io = ImGui::GetIO();
		if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
		{
			const ImGuiID dockspaceId = ImGui::GetID("MyDockspace");
			ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), dockspaceFlags);
		}

		DrawMenuBar();

		m_HierarchyPanel.OnImGuiRender();
		m_AssetsPanel->OnImGuiRender();
		m_ConsolePanel.OnImGuiRender();

		DrawViewport();

		DrawDebugWindow();

		DrawUIToolbar();

		m_NotificationManager.RenderNotifications();

		ImGui::End();

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

		m_RuntimeScene = Scene::Copy(m_EditorScene);

		m_RuntimeScene->OnRuntimeStart();

		m_ActiveScene = m_RuntimeScene;
		m_HierarchyPanel.SetContext(m_ActiveScene);
	}

	void EditorLayer::OnSceneSimulate()
	{
		m_SceneState = SceneState::Simulate;

		m_RuntimeScene = Scene::Copy(m_EditorScene);

		m_RuntimeScene->OnSimulationStart();

		m_ActiveScene = m_RuntimeScene;
		m_HierarchyPanel.SetContext(m_ActiveScene);
	}

	void EditorLayer::OnSceneStop()
	{
		m_SceneState = SceneState::Edit;
		m_IsScenePaused = false;

		if (m_SceneState == SceneState::Play)
			m_ActiveScene->OnRuntimeStop();
		else if (m_SceneState == SceneState::Simulate)
			m_ActiveScene->OnSimulationStop();

		m_RuntimeScene = nullptr;

		m_ActiveScene = m_EditorScene;
		m_HierarchyPanel.SetContext(m_ActiveScene);
	}

	void EditorLayer::HandleDragAndDrop() 
	{
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(assetBrowserItem))
			{
				const auto& path = static_cast<const char*>(payload->Data);
				KBR_EDITOR_INFO("Drag and drop payload: {0}", path);

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
			ImGui::EndDragDropTarget();
		}
	}

	void EditorLayer::HandleMousePicking() 
	{
		KBR_PROFILE_FUNCTION();

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
					m_HoveredEntity = Entity{ static_cast<entt::entity>(*pickedEntityID), m_ActiveScene.get() };
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
		/// Choose location for the new project
		//const std::string filepathString = FileDialog::SaveFile("Kerberos Project (*.kbrproj)\0*.kbrproj\0");

		const auto newProject = Project::New();

		NewScene();
		const std::string newSceneName = "Unnamed Scene.kerberos";
		const std::filesystem::path scenePath = Project::GetAssetDirectory() / "Scenes" / newSceneName;

		const SceneSerializer serializer(m_ActiveScene);
		serializer.Serialize(scenePath.string());

		m_NotificationManager.AddNotification("Scene saved to " + scenePath.string(), Notification::Type::Info);

		ProjectInfo projInfo;
		projInfo.Name = newProject->GetInfo().Name;
		projInfo.StartScenePath = "Scenes/" + newSceneName;
		projInfo.AssetDirectory = Project::GetAssetDirectory();
		newProject->SetInfo(projInfo);

		m_AssetsPanel = CreateOwner<AssetsPanel>(m_NotificationManager);
	}

	void EditorLayer::OpenProject(const std::filesystem::path& filepath) 
	{
		if (const auto project = Project::Load(filepath))
		{
			const auto startScenePath = Project::GetAssetDirectory() / project->GetInfo().StartScenePath;
			OpenScene(startScenePath);

			m_AssetsPanel = CreateOwner<AssetsPanel>(m_NotificationManager);
		}
	}

	bool EditorLayer::OpenProject()
	{
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
		const bool canSave = CanSaveScene();
		if (!canSave)
			return;

		// TODO: Store the current scene path in the project and save to that path instead of hardcoding it here
		const std::filesystem::path scenePath = "assets/scenes/Example.kerberos";

		const SceneSerializer serializer(m_ActiveScene);
		serializer.Serialize(scenePath.string());

		m_NotificationManager.AddNotification("Scene saved to " + scenePath.string(), Notification::Type::Info);

		Project::SaveActive();
	}

	void EditorLayer::SaveSceneAs()
	{
		const bool canSave = CanSaveScene();
		if (!canSave)
			return;

		const std::string filepath = FileDialog::SaveFile("Kerberos Scene (*.kerberos)\0*.kerberos\0");
		if (filepath.empty())
			return;

		const SceneSerializer serializer(m_ActiveScene);
		serializer.Serialize(filepath);

		m_NotificationManager.AddNotification("Scene saved to " + filepath, Notification::Type::Info);

		Project::SaveActive();
	}

	void EditorLayer::LoadScene()
	{
		const std::string filepathString = FileDialog::OpenFile("Kerberos Scene (*.kerberos)\0*.kerberos\0");

		if (filepathString.empty())
			return;

		OpenScene(filepathString);
	}

	void EditorLayer::OpenScene(const std::filesystem::path& filepath)
	{
		if (m_SceneState != SceneState::Edit)
		{
			OnSceneStop();
		}

		/// TODO: Prompt to save the current scene if there are unsaved changes

		const Ref<Scene> newScene = CreateRef<Scene>();
		const SceneSerializer serializer(newScene);

		if (!serializer.Deserialize(filepath))
		{
			KBR_EDITOR_ERROR("Failed to load scene from {0}", filepath.string());
			return;
		}

		m_EditorScene = newScene;
		m_EditorScene->OnViewportResize(static_cast<uint32_t>(m_ViewportSize.x), static_cast<uint32_t>(m_ViewportSize.y));
		m_HierarchyPanel.SetContext(m_EditorScene);

		m_ActiveScene = m_EditorScene;
	}

	void EditorLayer::OpenScene(const Ref<Scene>& scene) 
	{
		const auto& [Type, Filepath] = Project::GetActive()->GetEditorAssetManager()->GetMetadata(scene->GetHandle());

		OpenScene(Filepath);
	}

	void EditorLayer::NewScene() 
	{
		m_ActiveScene = CreateRef<Scene>();
		m_ActiveScene->OnViewportResize(static_cast<uint32_t>(m_ViewportSize.x), static_cast<uint32_t>(m_ViewportSize.y));
		m_HierarchyPanel.SetContext(m_ActiveScene);
	}

	void EditorLayer::DrawViewport()
	{
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::Begin("Viewport");
		ImGui::PopStyleVar();

		const auto viewportOffset = ImGui::GetCursorPos(); // Includes the tab bar

		m_ViewportFocused = ImGui::IsWindowFocused();
		m_ViewportHovered = ImGui::IsWindowHovered();

		Application::Get().BlockEvents(!m_ViewportHovered);

		m_ViewportSize = { ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y };

		const auto viewportImage = Renderer::GetCompositedOutputImageID();
		ImGui::Image(viewportImage, ImVec2(m_ViewportSize.x, m_ViewportSize.y));

		HandleDragAndDrop();

		/// Set the bounds of the viewport
		const auto windowSize = ImGui::GetWindowSize();
		ImVec2 minBound = ImGui::GetWindowPos();
		minBound.x += viewportOffset.x;
		minBound.y += viewportOffset.y;

		ImVec2 maxBound = { minBound.x + windowSize.x, minBound.y + windowSize.y };
		m_ViewportBounds[0] = { minBound.x, minBound.y };
		m_ViewportBounds[1] = { maxBound.x, maxBound.y };

		/// Gizmos
		const bool gizmosEnabled = m_SceneState == SceneState::Edit || m_SceneState == SceneState::Simulate;
		if (const Entity selectedEntity = m_HierarchyPanel.GetSelectedEntity(); selectedEntity && m_GizmoType != GizmoType::None && gizmosEnabled)
		{
			ImGuizmo::SetOrthographic(false);
			ImGuizmo::SetDrawlist();

			const float windowWidth = ImGui::GetWindowWidth();
			const float windowHeight = ImGui::GetWindowHeight();
			ImGuizmo::SetRect(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, windowWidth, windowHeight);

			/// Will be used for the Runtime camera
			/*const Entity cameraEntity = m_ActiveScene->GetPrimaryCameraEntity();
			const auto& camera = cameraEntity.GetComponent<CameraComponent>().Camera;

			const glm::mat4 cameraProjection = camera.GetProjection();
			const glm::mat4 cameraView = glm::inverse(cameraEntity.GetComponent<TransformComponent>().GetTransform());*/

			const glm::mat4 cameraProjection = m_EditorCamera->GetProjectionMatrix(Handedness::Left);
			const glm::mat4 cameraView = m_EditorCamera->GetViewMatrix(Handedness::Left);

			/// Entity transform
			auto& tc = selectedEntity.GetComponent<TransformComponent>();
			auto transform = tc.GetTransform();

			/// Snapping 
			const bool snap = Input::IsKeyPressed(Key::LeftControl);
			float snapValue = 0.5f;
			if (m_GizmoType == GizmoType::Rotate)
				snapValue = 45.0f;

			const float snapValues[3] = { snap ? snapValue : 0.0f, snap ? snapValue : 0.0f, snap ? snapValue : 0.0f };

			ImGuizmo::Manipulate(glm::value_ptr(cameraView), glm::value_ptr(cameraProjection),
								 static_cast<ImGuizmo::OPERATION>(m_GizmoType), ImGuizmo::WORLD, glm::value_ptr(transform), nullptr, snapValues);

			if (ImGuizmo::IsUsing())
			{
				glm::vec3 translation, rotationDegrees, scale;
				ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(transform), glm::value_ptr(translation), glm::value_ptr(rotationDegrees), glm::value_ptr(scale));

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
		constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
		const auto& colors = ImGui::GetStyle().Colors;
		const auto& buttonHovered = colors[ImGuiCol_ButtonHovered];
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(buttonHovered.x, buttonHovered.y, buttonHovered.z, 0.5f));
		const auto& buttonActive = colors[ImGuiCol_ButtonActive];
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

			//ImGui::SetCursorPosX((ImGui::GetWindowContentRegionMax().x * 0.5f) - (size * 0.5f));

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
				/// Todo: Implement undo/redo system
				if (ImGui::MenuItem("Undo", "Ctrl+Z", false, false)) {}
				if (ImGui::MenuItem("Redo", "Ctrl+Y", false, false)) {}
				ImGui::Separator();
				if (ImGui::MenuItem("Cut", "Ctrl+X", false, false)) {}
				if (ImGui::MenuItem("Copy", "Ctrl+C", false, false)) {}
				if (ImGui::MenuItem("Paste", "Ctrl+V", false, false)) {}
				if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, false)) {}
				ImGui::Separator();
				if (ImGui::MenuItem("Delete", "Del", false, false)) {}
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

	void EditorLayer::DrawDebugWindow() const
	{
		ImGui::Begin("Debug");

		ImGui::Text("Time: %.2f seconds", m_Time);
		ImGui::Text("FPS: %.2f", m_Fps);

		const GPUTimings gpuTimings = Renderer::GetLatestGPUTimings();
		if (gpuTimings.IsValid)
		{
			ImGui::Text("GPU Frame: %.3f ms", gpuTimings.FrameMilliseconds);
			ImGui::Text("GPU Depth Pre-Pass: %.3f ms", gpuTimings.DepthPrePassMilliseconds);
			ImGui::Text("GPU Shadow: %.3f ms", gpuTimings.ShadowPassMilliseconds);
			ImGui::Text("GPU Opaque: %.3f ms", gpuTimings.OpaquePassMilliseconds);
			ImGui::Text("GPU Transparent: %.3f ms", gpuTimings.TransparentPassMilliseconds);
		}
		else
		{
			ImGui::Text("GPU timings: unavailable");
		}

		ImGui::Separator();

		ImGui::Text("EditorCamera");
		const auto& camPos = m_EditorCamera->GetPosition();
		ImGui::Text("Position: (%.2f, %.2f, %.2f)", camPos.x, camPos.y, camPos.z);
		ImGui::Text("Rotation: (Pitch: %.2f, Yaw: %.2f)", m_EditorCamera->GetPitch(), m_EditorCamera->GetYaw());
		ImGui::Text("Distance: %.2f", m_EditorCamera->GetDistance());

		glm::vec3 focalPoint = m_EditorCamera->GetFocalPoint();
		ImGui::DragFloat3("Focal point", glm::value_ptr(focalPoint), 0.1f, 0.0f, 0.0f, "%.3f", ImGuiSliderFlags_NoInput);

		ImGui::Separator();

		const std::string activeGizmoTypeString = GetActiveGizmoTypeString();
		ImGui::Text("Gizmo Type: %s", activeGizmoTypeString.c_str());
		ImGui::Text("Viewport size: %.2f x %.2f", m_ViewportSize.x, m_ViewportSize.y);
		ImGui::Text("Viewport Focused: %s", m_ViewportFocused ? "Yes" : "No");
		ImGui::Text("Viewport Hovered: %s", m_ViewportHovered ? "Yes" : "No");

		std::string hoveredEntityName = "None";
		if (m_HoveredEntity)
		{
			hoveredEntityName = m_HoveredEntity.GetComponent<TagComponent>().Tag;
		}
		ImGui::Text("Hovered entity: %s", hoveredEntityName.c_str());

		ImGui::Separator();

		// Light controls
		/*ImGui::Text("Light directions");
		ImGui::DragFloat3("Light 1 Direction", glm::value_ptr(m_UniformDataParams.lights[0]), 0.1f);
		ImGui::Text("Light 2: (%.2f, %.2f, %.2f)", m_UniformDataParams.lights[1].x, m_UniformDataParams.lights[1].y, m_UniformDataParams.lights[1].z);
		ImGui::Text("Light 2: (%.2f, %.2f, %.2f)", m_UniformDataParams.lights[2].x, m_UniformDataParams.lights[2].y, m_UniformDataParams.lights[2].z);
		ImGui::DragFloat3("Light 4 Direction", glm::value_ptr(m_UniformDataParams.lights[3]), 0.1f);*/
		ImGui::Text("Lighting settings");
		float& exposure = Renderer::GetExposure();
		ImGui::DragFloat("Exposure", &exposure, 0.1f, 0.1f, 10.0f);
		float& gamma = Renderer::GetGamma();
		ImGui::DragFloat("Gamma", &gamma, 0.1f, 0.1f, 10.0f);
		//ImGui::DragFloat3("Ambient Light Color", glm::value_ptr(m_SceneUniformData.ambientLightColor), 0.01f, 0.0f, 1.0f);

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
		auto& [ConstantFactor, SlopeFactor, Clamp] = Renderer::GetShadowMapDepthBiasSettings();
		ImGui::DragFloat("Constant Factor", &ConstantFactor, 0.001f, 0.0f, 5.0f);
		ImGui::DragFloat("Clamp", &Clamp, 0.001f, 0.0f, 1.0f);
		ImGui::DragFloat("Slope Factor", &SlopeFactor, 0.01f, 0.0f, 10.0f);

		ImGui::Separator();

		ImGui::Text("Font Atlas");
		const uint64_t fontAtlasTextureID = VulkanContext::Get().GetImGuiRendererID(m_BasicFont->GetAtlasTexture());
		ImGui::Image(fontAtlasTextureID, ImVec2{ 256, 256 }, ImVec2{ 0, 1 }, ImVec2{ 1, 0 });

		ImGui::Separator();

		// Scene settings
		ImGui::Text("Settings");
		ImGui::Checkbox("Display Skybox", &Renderer::GetDisplaySkybox());
		ImGui::Checkbox("Display normals", &Renderer::GetDisplayDebugNormals());
		ImGui::Checkbox("Display physics colliders", &Renderer::GetDisplayPhysicsColliders());

		bool& useRayQueryBasedShadows = Renderer::GetUseRayQueryBasedShadows();
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

		ImGui::Checkbox("Use GTAO", &Renderer::GetUseGTAO());

		AntiAliasingMode& aaMode = Renderer::GetAntiAliasingMode();
		const char* aaModeItems[] = { "None", "FXAA", "TAA" };
		if (ImGui::Combo("Anti-aliasing mode", reinterpret_cast<int*>(&aaMode), aaModeItems, IM_ARRAYSIZE(aaModeItems)))
		{
			Renderer::GetAntiAliasingMode() = static_cast<AntiAliasingMode>(aaMode);
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

	void EditorLayer::CalculateEntityTransform(const Entity& entity) const 
	{
		m_ActiveScene->CalculateEntityTransform(entity);
	}

	bool EditorLayer::OnKeyPressed(const KeyPressedEvent& event)
	{
		/// Shortcuts
		if (event.GetRepeatCount() > 0)
			return false;

		const bool ctrl = Input::IsKeyPressed(Key::LeftControl) || Input::IsKeyPressed(Key::RightControl);
		const bool shift = Input::IsKeyPressed(Key::LeftShift) || Input::IsKeyPressed(Key::RightShift);

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
				if (ctrl)
				{
					NewScene();
				}
				break;
			case Key::O:
				if (ctrl)
				{
					LoadScene();
				}
				break;

				/// Gizmos
			case Key::Q:
				m_GizmoType = GizmoType::None;
				break;
			case Key::W:
				m_GizmoType = GizmoType::Translate;
				break;
			case Key::E:
				m_GizmoType = GizmoType::Scale;
				break;
			case Key::R:
				m_GizmoType = GizmoType::Rotate;
				break;
			case Key::F: {
				if (const Entity& entity = m_HierarchyPanel.GetSelectedEntity())
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

	bool EditorLayer::OnMouseButtonPressed(const MouseButtonPressedEvent& event)
	{
		/// Handle mouse picking
		/// Only select the entity if we are not using the gizmos or the camera
		/// TODO: Check if isViewportHovered is needed, Application::BlockEvents should prevent this from firing
		if (event.GetButton() == Mouse::ButtonLeft && !ImGuizmo::IsOver() && !Input::IsKeyPressed(Key::LeftAlt) && m_ViewportHovered)
		{
			m_HierarchyPanel.SetSelectedEntity(m_HoveredEntity);
		}
		return false;
	}

	bool EditorLayer::OnWindowDrop(const WindowDropEvent& event) 
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

		KBR_CORE_ASSERT(false, "Invalid gizmo type");
		return "";
	}
}
