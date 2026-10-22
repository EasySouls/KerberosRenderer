#pragma once

#include "ICommand.hpp"

#include "Core/Core.hpp"
#include "Core/UUID.hpp"
#include "Scene/Scene.hpp"

#include <string>

namespace Kerberos {

	class CreateEntityCommand final : public ICommand
	{
	public:
		/// Creates the named entity when first executed. Redo recreates it with the
		/// same UUID so later commands still resolve the entity.
		CreateEntityCommand(Ref<Scene> scene, std::string name);

		void Execute() override;
		void Undo() override;

		const std::string& GetName() const override;

	private:
		Ref<Scene> m_Scene;
		std::string m_EntityName;
		std::string m_CommandName;
		UUID m_EntityID = UUID::Invalid();
        UUID m_ParentID = UUID::Invalid();
        std::string m_SerializedSubtree;
	};

}
