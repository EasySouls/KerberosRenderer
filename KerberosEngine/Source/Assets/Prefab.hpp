#pragma once

#include "Assets/Formats/PrefabAsset.hpp"

namespace Kerberos
{
	using PrefabEntityData = PrefabEntityTemplate;

	class Prefab : public Asset
	{
	public:
		AssetType GetType() override { return AssetType::Prefab; }

		const std::string& GetName() const { return Name; }
		void SetName(const std::string& name) { Name = name; }

		std::string Name;
		PrefabLocalIndex RootLocalIndex = InvalidPrefabLocalIndex;
		UUID RootEntityID = UUID::Invalid(); // legacy prefab reference
		std::vector<PrefabEntityData> Entities;
	};
}
