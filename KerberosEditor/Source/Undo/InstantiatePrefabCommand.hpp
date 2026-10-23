#pragma once

#include "ICommand.hpp"

#include "Core/Core.hpp"
#include "Core/UUID.hpp"
#include "Scene/Scene.hpp"

#include <string>

namespace Kerberos {

	/**
	 * Instantiates a prefab into the scene. The created subtree is captured the
	 * first time the command runs so redo restores the same UUIDs.
	 */
	class InstantiatePrefabCommand final : public ICommand
	{
	public:
		InstantiatePrefabCommand(Ref<Scene> scene, AssetHandle prefabAsset, std::string rootName = {});

		void Execute() override;
		void Undo() override;

		const std::string& GetName() const override;

		/// Returns the instantiated root, or an invalid UUID before the first execute.
		UUID GetEntityID() const;

	private:
		Ref<Scene> m_Scene;
		AssetHandle m_PrefabAsset = AssetHandle::Invalid();
		std::string m_RootName;
		std::string m_CommandName;
		UUID m_EntityID = UUID::Invalid();
		UUID m_ParentID = UUID::Invalid();
		std::string m_SerializedSubtree;
	};

}
