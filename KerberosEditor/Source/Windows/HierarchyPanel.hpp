#pragma once

#include "EditorWindow.hpp"
#include "Core/Core.hpp"
#include "Scene/Scene.hpp"
#include "Scene/Entity.hpp"
#include "Renderer/Textures/Texture2D.hpp"
#include "Renderer/Material.hpp"
#include "Renderer/Mesh.hpp"
#include "Events/KeyPressedEvent.hpp"
#include "../Notification/NotificationManager.hpp"

#include <vector>
#include <functional>
#include <string>
#include <unordered_map>

namespace Kerberos
{
	class UndoRedoManager;

	class HierarchyPanel : public EditorWindow
	{
	public:
		HierarchyPanel() = default;
		explicit HierarchyPanel(const Ref<Scene>& context);
		~HierarchyPanel() override = default;

		void SetContext(const Ref<Scene>& context);

		/// Routes scene edits through the given history. Pass nullptr while the
		/// editor is running a runtime copy so edits are not recorded.
		void SetUndoRedoManager(UndoRedoManager* manager);

		void OnImGuiRender() override;
		void DrawComponents(Entity entity);

		Entity GetSelectedEntity() const { return m_SelectedEntity; }
		void SetSelectedEntity(Entity entity);

		void OnEvent(Event& event) override;

		void SetOnEntityDeleted(const std::function<void(Entity)>& callback);

		/**
		 * Called whenever the selected entity changes through user interaction or
		 * SetSelectedEntity(), so other panels can react to the new selection.
		 */
		void SetOnEntitySelected(const std::function<void(Entity)>& callback);

		void AddComponentPopup(Entity entity);

	private:
		void DrawEntityNode(const Entity& entity);
		void DrawEntityNodeContextMenu(const Entity& entity, bool& entityDeleted) const;
		void DrawEntityNodeChildren(const Entity& entity);
		void QueueEntityDeletion(const Entity& entity);

		bool OnKeyPressed(const KeyPressedEvent& event);

		bool HandleHierarchyPanelDragAndDrop();

		void ExecuteCreateEntity(const std::string& name);

		void DrawVec3Control(const std::string& label,
			UUID entityID,
			glm::vec3& values,
			float resetValue = 0.0f,
			float columnWidth = 80.0f,
			const std::function<void()>& onValueChanged = nullptr,
			const std::function<void(const glm::vec3&)>& onEditCommitted = nullptr);

		template<typename T>
		void AddComponentWithCheck(Entity entity)
		{
			if (entity.HasComponent<T>())
			{
				m_NotificationManager.AddNotification("Entity already has a " + std::string(typeid(T).name()) + " component!", Notification::Type::Warning);
				return;
			}

			entity.AddComponent<T>();
		}

	private:
		Ref<Scene> m_Context;

		Entity m_SelectedEntity;

		/// Set on mouse-down over an entity node and applied on mouse release.
		/// This keeps the inspector on the current entity while another entity is
		/// being dragged onto its script fields.
		Entity m_PendingSelectedEntity;

		std::vector<Entity> m_DeletionQueue;

		std::function<void(Entity)> m_OnEntityDeleted;
		std::function<void(Entity)> m_OnEntitySelected;

		UndoRedoManager* m_UndoRedo = nullptr;

		struct Vec3EditSnapshot
		{
			UUID EntityID = UUID::Invalid();
			glm::vec3 Value{ 0.0f };
		};

		/// Snapshot of a vector field taken when an ImGui drag starts, keyed by the
		/// widget ID so Position, Rotation, and Scale stay independent.
		std::unordered_map<unsigned int, Vec3EditSnapshot> m_Vec3EditSnapshots;

		std::string m_TagBeforeEdit;
		UUID m_TagEditEntity = UUID::Invalid();
		bool m_TagEditActive = false;

		// Examples
		Ref<Texture2D> m_WhiteTexture;

		Ref<Mesh> m_CubeMesh;
		Ref<Mesh> m_SphereMesh;

		Ref<Material> m_WhiteMaterial;

		NotificationManager m_NotificationManager;
	};
}