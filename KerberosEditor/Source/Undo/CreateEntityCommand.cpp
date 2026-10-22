#include "CreateEntityCommand.hpp"

#include "Scene/Entity.hpp"

#include <utility>

namespace Kerberos {

	CreateEntityCommand::CreateEntityCommand(Ref<Scene> scene, std::string name)
		: m_Scene(std::move(scene)),
		m_EntityName(std::move(name)),
		m_CommandName("Create Entity")
	{
	}

	void CreateEntityCommand::Execute()
	{
		if (!m_Scene)
			return;

		if (m_EntityID.IsValid() && m_Scene->TryGetEntityByUUID(m_EntityID))
			return;

		if (m_EntityID.IsValid())
		{
			m_Scene->CreateEntityWithUUID(m_EntityName, m_EntityID);
			return;
		}

		const Entity entity = m_Scene->CreateEntity(m_EntityName);
		m_EntityID = entity.GetUUID();
	}

	void CreateEntityCommand::Undo()
	{
		if (!m_Scene || !m_EntityID.IsValid())
			return;

		if (const Entity entity = m_Scene->TryGetEntityByUUID(m_EntityID))
			m_Scene->DestroyEntity(entity);
	}

	const std::string& CreateEntityCommand::GetName() const
	{
		return m_CommandName;
	}

}
