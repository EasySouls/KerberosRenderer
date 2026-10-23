#include "DestroyEntityCommand.hpp"

#include "Scene/Entity.hpp"
#include "Serialization/SceneSerializer.hpp"

#include <utility>

namespace Kerberos {

	DestroyEntityCommand::DestroyEntityCommand(Ref<Scene> scene, const Entity& entity)
		: m_Scene(std::move(scene)),
		m_EntityID(entity ? entity.GetUUID() : UUID::Invalid()),
		m_CommandName("Delete Entity")
	{
	}

	void DestroyEntityCommand::Execute()
	{
		if (!m_Scene || !m_EntityID.IsValid())
			return;

		const Entity entity = m_Scene->TryGetEntityByUUID(m_EntityID);
		if (!entity)
			return;

		if (!m_HasSnapshot)
		{
			m_ParentID = entity.GetComponent<HierarchyComponent>().Parent;
			m_SerializedSubtree = SceneSerializer::SerializeEntitySubtree(m_Scene, entity);
			m_HasSnapshot = true;
		}

		m_Scene->DestroyEntity(entity);
	}

	void DestroyEntityCommand::Undo()
	{
		if (!m_Scene || m_SerializedSubtree.empty())
			return;

		const Entity restoredRoot = SceneSerializer::DeserializeEntitySubtree(m_Scene, m_SerializedSubtree);
		if (!restoredRoot)
			return;

		/// The parent is outside the captured subtree, so its child list has to be rebuilt.
		if (m_ParentID.IsValid())
		{
			if (const Entity parent = m_Scene->TryGetEntityByUUID(m_ParentID))
				m_Scene->SetParent(restoredRoot, parent, false);
		}
	}

	const std::string& DestroyEntityCommand::GetName() const
	{
		return m_CommandName;
	}

}
