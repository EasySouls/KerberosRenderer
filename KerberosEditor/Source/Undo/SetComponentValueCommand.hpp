#pragma once

#include "ICommand.hpp"

#include "Core/Core.hpp"
#include "Core/UUID.hpp"
#include "Scene/Entity.hpp"
#include "Scene/Scene.hpp"

#include <functional>
#include <string>
#include <utility>

namespace Kerberos {

	/**
	 * Stores a component's before and after value. The entity is resolved by UUID
	 * on every apply so the command survives entity destruction and recreation.
	 */
	template<typename T>
	class SetComponentValueCommand final : public ICommand
	{
	public:
		/**
		 * Creates a value command. onApplied runs after the component is assigned,
		 * which lets transforms recalculate their derived world matrix.
		 */
		SetComponentValueCommand(Ref<Scene> scene,
			const UUID entityID,
			T before,
			T after,
			std::string commandName,
			std::function<void(Entity)> onApplied = nullptr)
			: m_Scene(std::move(scene)),
			m_EntityID(entityID),
			m_Before(std::move(before)),
			m_After(std::move(after)),
			m_CommandName(std::move(commandName)),
			m_OnApplied(std::move(onApplied))
		{
		}

		void Execute() override
		{
			Apply(m_After);
		}

		void Undo() override
		{
			Apply(m_Before);
		}

		const std::string& GetName() const override
		{
			return m_CommandName;
		}

	private:
		void Apply(const T& value)
		{
			if (!m_Scene)
				return;

			const Entity entity = m_Scene->TryGetEntityByUUID(m_EntityID);
			if (!entity || !entity.HasComponent<T>())
				return;

			entity.GetComponent<T>() = value;
			if (m_OnApplied)
				m_OnApplied(entity);
		}

		Ref<Scene> m_Scene;
		UUID m_EntityID;
		T m_Before;
		T m_After;
		std::string m_CommandName;
		std::function<void(Entity)> m_OnApplied;
	};

}
