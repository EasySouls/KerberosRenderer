#pragma once

#include "Assets/Asset.hpp"
#include "Core/UUID.hpp"
#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace Kerberos
{
	struct PrefabEntityData
	{
		UUID ID;
		std::string Tag;

		glm::vec3 Translation = glm::vec3(0.0f);
		glm::vec3 EulerRotation = glm::vec3(0.0f);
		glm::vec3 Scale = glm::vec3(1.0f);

		UUID Parent = UUID::Invalid();
		std::vector<UUID> Children;

		// Optional: StaticMeshComponent reference
		std::string MeshAssetPath;
		std::string MaterialAssetPath;
		bool HasStaticMesh = false;
	};

	class Prefab : public Asset
	{
	public:
		Prefab() = default;
		~Prefab() override = default;

		AssetType GetType() override { return AssetType::Prefab; }

		const std::string& GetName() const { return Name; }
		void SetName(const std::string& name) { Name = name; }

		std::string Name;
		UUID RootEntityID = UUID::Invalid();
		std::vector<PrefabEntityData> Entities;
	};
}
