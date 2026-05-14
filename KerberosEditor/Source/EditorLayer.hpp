#pragma once

#include "Layer.hpp"
#include "Vulkan.hpp"

#include "Windows/AssetsPanel.hpp"
#include "Windows/HierarchyPanel.hpp"
#include "Windows/ConsolePanel.hpp"

#include "Renderer/Mesh.hpp"
#include "Renderer/Textures/Texture2D.hpp"
#include "Renderer/Textures/TextureCube.hpp"
#include "Buffer.hpp"
#include "Renderer/Material.hpp"
#include "Core/Core.hpp"
#include "Scene/Node.hpp"
#include "Scene/Camera/Camera.hpp"
#include "Renderer/MaterialRegistry.hpp"
#include "Events/MouseButtonPressedEvent.hpp"
#include "Events/WindowDropEvent.hpp"

#include <string>
#include <vector>
#include <array>
#include <memory>
#include <optional>
#include <unordered_map>



namespace Kerberos
{
	class EditorLayer : public Layer
	{
	public:
		EditorLayer();
		~EditorLayer() override;
		void OnAttach() override;
		void OnDetach() override;

		void OnUpdate(float deltaTime) override;
		void OnEvent(Event& event) override;
		void OnImGuiRender() override;

	private:
		void OnScenePlay();
		void OnSceneSimulate();
		void OnSceneStop();

		void HandleDragAndDrop();
		void HandleMousePicking();

		void NewProject();
		void OpenProject(const std::filesystem::path& filepath);
		[[nodiscard]] bool OpenProject();

		bool CanSaveScene();
		void SaveScene();
		void SaveSceneAs();

		/**
		 * Opens the file system dialog to select a scene file to open.
		 */
		void LoadScene();

		/**
		 * Loads a scene from the specified file path.
		 * @param filepath The path to the scene file to open.
		 */
		void OpenScene(const std::filesystem::path& filepath);
		void OpenScene(const Ref<Scene>& scene);
		void NewScene();

		void DrawViewport();
		void DrawUIToolbar();
		void DrawMenuBar();
		void DrawDebugWindow() const;

		void CalculateEntityTransform(const Entity& entity) const;

		bool OnKeyPressed(const KeyPressedEvent& event);
		bool OnMouseButtonPressed(const MouseButtonPressedEvent& event);
		bool OnWindowDrop(const WindowDropEvent& event);

		std::string GetActiveGizmoTypeString() const;

		static bool DoesImGuiWantInput();

	private:
		float m_Time = 0.0f;
		float m_Fps = 0.0f;

		HierarchyPanel m_HierarchyPanel;
		Owner<AssetsPanel> m_AssetsPanel;
		ConsolePanel m_ConsolePanel;

		NotificationManager m_NotificationManager;

		Ref<Scene> m_ActiveScene;
		Ref<Scene> m_EditorScene;
		Ref<Scene> m_RuntimeScene;

		Entity m_CameraEntity;

		Entity m_HoveredEntity;

		Ref<Font> m_BasicFont;

		enum class SceneState : uint8_t
		{
			Edit,
			Play,
			Simulate
		};
		SceneState m_SceneState = SceneState::Edit;
		bool m_IsScenePaused = false;

		bool m_ViewportFocused = false;
		bool m_ViewportHovered = false;
		std::array<glm::vec2, 2> m_ViewportBounds;

		bool m_DoesImGuiWantInput = false;

		// From ImGuizmo.h
		enum class GizmoType : std::uint16_t
		{
			None = 0,
			Translate = 7,
			Rotate = 896,
			Scale = 120
		};
		GizmoType m_GizmoType = GizmoType::None;

		Ref<Texture2D> m_IconPlay;
		Ref<Texture2D> m_IconStop;
		Ref<Texture2D> m_IconPause;
		Ref<Texture2D> m_IconResume;

		bool m_IsFullScreenPersistent = true;

		Owner<Camera> m_EditorCamera = nullptr;
		// Size of the ImGui viewport.
		glm::vec2 m_ViewportSize{ 0.f };

		std::unordered_map<std::string, Ref<Mesh>> m_Meshes;
		std::optional<Mesh> m_SkyboxMesh;
		std::vector<Ref<Texture2D>> m_Textures;

		std::vector<Owner<Node>> m_SceneNodes;

		MaterialRegistry m_MaterialRegistry;
	};

}
