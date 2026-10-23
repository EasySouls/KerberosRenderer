#pragma once

#include "ICommand.hpp"

#include "Core/Core.hpp"
#include "Core/UUID.hpp"
#include "Scene/Scene.hpp"

#include <string>

namespace Kerberos {

	/**
	 * Duplicates an entity (and optionally its children). The duplicated subtree
	 * is captured the first time the command runs so redo restores the same UUIDs.
	 */
	class DuplicateEntityCommand final : public ICommand
	{
	public:
		DuplicateEntityCommand(Ref<Scene> scene, const Entity& entity, bool duplicateChildren);

		void Execute() override;
		void Undo() override;

		const std::string& GetName() const override;

		/// Returns the duplicated root, or an invalid UUID before the first execute.
		UUID GetEntityID() const;

	private:
		Ref<Scene> m_Scene;
		UUID m_SourceID = UUID::Invalid();
		UUID m_DuplicateID = UUID::Invalid();
		UUID m_ParentID = UUID::Invalid();
		std::string m_SerializedSubtree;
		std::string m_CommandName;
		bool m_DuplicateChildren = true;
	};

}
