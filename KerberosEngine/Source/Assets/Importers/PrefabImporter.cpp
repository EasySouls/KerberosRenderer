#include "PrefabImporter.hpp"
#include "Serialization/PrefabSerializer.hpp"

import Kerberos;

namespace Kerberos
{
	Ref<Prefab> PrefabImporter::ImportPrefab(const AssetHandle /*handle*/, const AssetMetadata& metadata)
	{
		return ImportPrefab(metadata.Filepath);
	}

	Ref<Prefab> PrefabImporter::ImportPrefab(const std::filesystem::path& filepath)
	{
		Ref<Prefab> prefab = PrefabSerializer::DeserializePrefab(filepath);
		if (!prefab)
		{
			Log::CoreError("PrefabImporter: failed to deserialize prefab from {}", filepath.string());
			return nullptr;
		}

		prefab->SetName(filepath.stem().string());
		return prefab;
	}
}