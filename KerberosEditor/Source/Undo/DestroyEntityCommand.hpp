#pragma once

#include "ICommand.hpp"

#include "Core/Core.hpp"
#include "Core/UUID.hpp"
#include "Scene/Scene.hpp"

#include <string>

namespace Kerberos {

	/**
	 * Deletes an entity and its children. The subtree is captured as YAML the
	 * first time the command runs, so undo restores the exact same entities.
	 */
	class DestroyEntityCommand final : public ICommand
	{
	public:
		DestroyEntityCommand(Ref<Scene> scene, const Entity& entity);

		void Execute() override;
		void Undo() override;

		const std::string& GetName() const override;

	private:
		Ref<Scene> m_Scene;
		UUID m_EntityID = UUID::Invalid();
		UUID m_ParentID = UUID::Invalid();
		std::string m_SerializedSubtree;
		std::string m_CommandName;
		bool m_HasSnapshot = false;
	};

}
