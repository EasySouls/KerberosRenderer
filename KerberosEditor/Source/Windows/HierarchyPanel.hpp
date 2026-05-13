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

namespace Kerberos
{
	class HierarchyPanel : public EditorWindow
	{
	public:
		HierarchyPanel() = default;
		explicit HierarchyPanel(const Ref<Scene>& context);
		~HierarchyPanel() override = default;

		void SetContext(const Ref<Scene>& context);

		void OnImGuiRender() override;
		void DrawComponents(Entity entity);

		Entity GetSelectedEntity() const { return m_SelectedEntity; }
		void SetSelectedEntity(Entity entity);

		void OnEvent(Event& event) override;

	private:
		void DrawEntityNode(const Entity& entity);
		void DrawEntityNodeContextMenu(const Entity& entity, bool& entityDeleted) const;
		void DrawEntityNodeChildren(const Entity& entity);
		void QueueEntityDeletion(const Entity& entity);
		void AddComponentPopup(Entity entity);

		bool OnKeyPressed(const KeyPressedEvent& event);

		bool HandleHierarchyPanelDragAndDrop();

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

		std::vector<Entity> m_DeletionQueue;

		// Examples
		Ref<Texture2D> m_WhiteTexture;

		Ref<Mesh> m_CubeMesh;
		Ref<Mesh> m_SphereMesh;

		Ref<Material> m_WhiteMaterial;

		NotificationManager m_NotificationManager;
	};
}