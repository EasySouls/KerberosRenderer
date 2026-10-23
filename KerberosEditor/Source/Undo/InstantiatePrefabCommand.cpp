#include "InstantiatePrefabCommand.hpp"

#include "Scene/Entity.hpp"
#include "Serialization/SceneSerializer.hpp"

#include <utility>

namespace Kerberos {

	InstantiatePrefabCommand::InstantiatePrefabCommand(Ref<Scene> scene, const AssetHandle prefabAsset, std::string rootName)
		: m_Scene(std::move(scene)),
		m_PrefabAsset(prefabAsset),
		m_RootName(std::move(rootName)),
		m_CommandName("Instantiate Prefab")
	{
	}

	void InstantiatePrefabCommand::Execute()
	{
		if (!m_Scene || !m_PrefabAsset.IsValid())
			return;

		if (m_EntityID.IsValid() && m_Scene->TryGetEntityByUUID(m_EntityID))
			return;

		if (m_EntityID.IsValid() && !m_SerializedSubtree.empty())
		{
			const Entity restoredRoot = SceneSerializer::DeserializeEntitySubtree(m_Scene, m_SerializedSubtree);
			if (restoredRoot && m_ParentID.IsValid())
			{
				if (const Entity parent = m_Scene->TryGetEntityByUUID(m_ParentID))
					m_Scene->SetParent(restoredRoot, parent, false);
			}
			return;
		}

		const Entity root = m_Scene->InstantiatePrefab(m_PrefabAsset, m_RootName);
		if (!root)
			return;

		m_EntityID = root.GetUUID();
		m_ParentID = root.GetComponent<HierarchyComponent>().Parent;
		m_SerializedSubtree = SceneSerializer::SerializeEntitySubtree(m_Scene, root);
	}

	void InstantiatePrefabCommand::Undo()
	{
		if (!m_Scene || !m_EntityID.IsValid())
			return;

		if (const Entity root = m_Scene->TryGetEntityByUUID(m_EntityID))
			m_Scene->DestroyEntity(root);
	}

	const std::string& InstantiatePrefabCommand::GetName() const
	{
		return m_CommandName;
	}

	UUID InstantiatePrefabCommand::GetEntityID() const
	{
		return m_EntityID;
	}

}
