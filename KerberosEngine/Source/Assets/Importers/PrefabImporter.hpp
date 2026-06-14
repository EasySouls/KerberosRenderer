#pragma once

#include "Core/Core.hpp"
#include "Assets/AssetMetadata.hpp"
#include "Assets/Prefab.hpp"

namespace Kerberos
{
	class PrefabImporter
	{
	public:
		static Ref<Prefab> ImportPrefab(AssetHandle handle, const AssetMetadata& metadata);
		static Ref<Prefab> ImportPrefab(const std::filesystem::path& filepath);
	};
}
