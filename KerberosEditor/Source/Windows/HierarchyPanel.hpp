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
		void AddComponentPopup(Entity entity);

		bool OnKeyPressed(const KeyPressedEvent& event);

	private:
		Ref<Scene> m_Context;

		Entity m_SelectedEntity;

		std::vector<Entity> m_DeletionQueue;

		// Examples
		Ref<Texture2D> m_IceTexture;
		Ref<Texture2D> m_SpriteSheetTexture;
		Ref<Texture2D> m_WhiteTexture;

		Ref<Mesh> m_CubeMesh;
		Ref<Mesh> m_SphereMesh;

		Ref<Material> m_WhiteMaterial;

		NotificationManager m_NotificationManager;
	};
}