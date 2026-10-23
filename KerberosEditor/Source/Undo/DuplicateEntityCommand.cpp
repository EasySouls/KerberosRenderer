#include "DuplicateEntityCommand.hpp"

#include "Scene/Entity.hpp"
#include "Serialization/SceneSerializer.hpp"

#include <utility>

namespace Kerberos {

	DuplicateEntityCommand::DuplicateEntityCommand(Ref<Scene> scene, const Entity& entity, const bool duplicateChildren)
		: m_Scene(std::move(scene)),
		m_SourceID(entity ? entity.GetUUID() : UUID::Invalid()),
		m_CommandName("Duplicate Entity"),
		m_DuplicateChildren(duplicateChildren)
	{
	}

	void DuplicateEntityCommand::Execute()
	{
		if (!m_Scene || !m_SourceID.IsValid())
			return;

		if (m_DuplicateID.IsValid() && m_Scene->TryGetEntityByUUID(m_DuplicateID))
			return;

		if (m_DuplicateID.IsValid() && !m_SerializedSubtree.empty())
		{
			const Entity restoredRoot = SceneSerializer::DeserializeEntitySubtree(m_Scene, m_SerializedSubtree);
			if (restoredRoot && m_ParentID.IsValid())
			{
				if (const Entity parent = m_Scene->TryGetEntityByUUID(m_ParentID))
					m_Scene->SetParent(restoredRoot, parent, false);
			}
			return;
		}

		const Entity source = m_Scene->TryGetEntityByUUID(m_SourceID);
		if (!source)
			return;

		const Entity duplicate = m_Scene->DuplicateEntity(source, m_DuplicateChildren);
		if (!duplicate)
			return;

		m_DuplicateID = duplicate.GetUUID();
		m_ParentID = duplicate.GetComponent<HierarchyComponent>().Parent;
		m_SerializedSubtree = SceneSerializer::SerializeEntitySubtree(m_Scene, duplicate);
	}

	void DuplicateEntityCommand::Undo()
	{
		if (!m_Scene || !m_DuplicateID.IsValid())
			return;

		if (const Entity duplicate = m_Scene->TryGetEntityByUUID(m_DuplicateID))
			m_Scene->DestroyEntity(duplicate);
	}

	const std::string& DuplicateEntityCommand::GetName() const
	{
		return m_CommandName;
	}

	UUID DuplicateEntityCommand::GetEntityID() const
	{
		return m_DuplicateID;
	}

}
