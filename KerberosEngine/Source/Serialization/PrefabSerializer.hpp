#pragma once

#include "Assets/Prefab.hpp"
#include "Assets/AssetMetadata.hpp"

#include <filesystem>
#include <memory>

namespace Kerberos
{
	class PrefabSerializer
	{
	public:
		static bool SerializePrefab(const Ref<Prefab>& prefab, const std::filesystem::path& filepath);
		static Ref<Prefab> DeserializePrefab(const std::filesystem::path& filepath);
	};
}
