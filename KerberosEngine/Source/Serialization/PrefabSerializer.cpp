#include "kbrpch.hpp"
#include "PrefabSerializer.hpp"

#include <yaml-cpp/yaml.h>
#include <fstream>

namespace Kerberos
{
	bool PrefabSerializer::SerializePrefab(const Ref<Prefab>& prefab, const std::filesystem::path& filepath)
	{
		if (!prefab)
			return false;

		YAML::Emitter out;
		out << YAML::BeginMap;
		out << YAML::Key << "Prefab" << YAML::Value << YAML::BeginMap;
		out << YAML::Key << "Name" << YAML::Value << prefab->Name;
		out << YAML::Key << "RootEntityID" << YAML::Value << prefab->RootEntityID;
		out << YAML::EndMap; // Prefab

		out << YAML::Key << "Entities" << YAML::Value << YAML::BeginSeq;
		for (const auto& entity : prefab->Entities)
		{
			out << YAML::BeginMap;
			out << YAML::Key << "ID" << YAML::Value << entity.ID;
			out << YAML::Key << "Tag" << YAML::Value << entity.Tag;
			out << YAML::Key << "Translation" << YAML::Value << YAML::Flow << YAML::BeginSeq << entity.Translation.x << entity.Translation.y << entity.Translation.z << YAML::EndSeq;
			out << YAML::Key << "EulerRotation" << YAML::Value << YAML::Flow << YAML::BeginSeq << entity.EulerRotation.x << entity.EulerRotation.y << entity.EulerRotation.z << YAML::EndSeq;
			out << YAML::Key << "Scale" << YAML::Value << YAML::Flow << YAML::BeginSeq << entity.Scale.x << entity.Scale.y << entity.Scale.z << YAML::EndSeq;
			out << YAML::Key << "Parent" << YAML::Value << entity.Parent;
			out << YAML::Key << "Children" << YAML::Value << YAML::BeginSeq;
			for (const auto& child : entity.Children)
				out << child;
			out << YAML::EndSeq;
			out << YAML::Key << "HasStaticMesh" << YAML::Value << entity.HasStaticMesh;
			if (entity.HasStaticMesh)
			{
				out << YAML::Key << "MeshAssetPath" << YAML::Value << entity.MeshAssetPath;
				out << YAML::Key << "MaterialAssetPath" << YAML::Value << entity.MaterialAssetPath;
			}
			out << YAML::EndMap; // Entity
		}
		out << YAML::EndSeq; // Entities
		out << YAML::EndMap; // Root

		std::ofstream fout(filepath);
		if (!fout.is_open())
		{
			KBR_CORE_ERROR("PrefabSerializer: failed to open file for writing: {}", filepath.string());
			return false;
		}
		fout << out.c_str();
		fout.close();

		return true;
	}

	Ref<Prefab> PrefabSerializer::DeserializePrefab(const std::filesystem::path& filepath)
	{
		if (!std::filesystem::exists(filepath))
		{
			KBR_CORE_ERROR("PrefabSerializer: prefab file does not exist: {}", filepath.string());
			return nullptr;
		}

		YAML::Node root;
		try
		{
			root = YAML::LoadFile(filepath.string());
		}
		catch (const YAML::Exception& e)
		{
			KBR_CORE_ERROR("PrefabSerializer: failed to parse prefab YAML: {}", e.what());
			return nullptr;
		}

		if (!root["Prefab"] || !root["Entities"])
		{
			KBR_CORE_ERROR("PrefabSerializer: invalid prefab structure");
			return nullptr;
		}

		Ref<Prefab> prefab = CreateRef<Prefab>();
		prefab->Name = root["Prefab"]["Name"].as<std::string>("Untitled Prefab");
		prefab->RootEntityID = UUID(root["Prefab"]["RootEntityID"].as<uint64_t>(0));

		const YAML::Node& entitiesNode = root["Entities"];
		for (const auto& entityNode : entitiesNode)
		{
			PrefabEntityData entityData;
			entityData.ID = UUID(entityNode["ID"].as<uint64_t>(0));
			entityData.Tag = entityNode["Tag"].as<std::string>("Entity");

			if (entityNode["Translation"])
			{
				const auto translation = entityNode["Translation"].as<std::vector<float>>();
				if (translation.size() >= 3)
					entityData.Translation = glm::vec3(translation[0], translation[1], translation[2]);
			}

			if (entityNode["EulerRotation"])
			{
				const auto rotation = entityNode["EulerRotation"].as<std::vector<float>>();
				if (rotation.size() >= 3)
					entityData.EulerRotation = glm::vec3(rotation[0], rotation[1], rotation[2]);
			}

			if (entityNode["Scale"])
			{
				const auto scale = entityNode["Scale"].as<std::vector<float>>();
				if (scale.size() >= 3)
					entityData.Scale = glm::vec3(scale[0], scale[1], scale[2]);
			}

			entityData.Parent = UUID(entityNode["Parent"].as<uint64_t>(0));

			if (entityNode["Children"])
			{
				const auto children = entityNode["Children"].as<std::vector<uint64_t>>();
				for (const auto child : children)
					entityData.Children.emplace_back(child);
			}

			entityData.HasStaticMesh = entityNode["HasStaticMesh"].as<bool>(false);
			if (entityData.HasStaticMesh)
			{
				entityData.MeshAssetPath = entityNode["MeshAssetPath"].as<std::string>("");
				entityData.MaterialAssetPath = entityNode["MaterialAssetPath"].as<std::string>("");
			}

			prefab->Entities.push_back(entityData);
		}

		return prefab;
	}
}
