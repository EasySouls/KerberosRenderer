#pragma once

#include "Assets/Formats/PrefabAsset.hpp"

namespace Kerberos
{
	using PrefabEntityData = PrefabEntityTemplate;

	class Prefab : public Asset
	{
	public:
        static constexpr AssetType Type = AssetType::Prefab;
		AssetType GetType() override { return Type; }

		const std::string& GetName() const { return Name; }
		void SetName(const std::string& name) { Name = name; }

		std::string Name;
		PrefabLocalIndex RootLocalIndex = InvalidPrefabLocalIndex;
		UUID RootEntityID = UUID::Invalid(); // legacy prefab reference
		std::vector<PrefabEntityData> Entities;

		// TODO: Only for compatibility
		bool RequiresInstanceRoot = false;
	};
}
