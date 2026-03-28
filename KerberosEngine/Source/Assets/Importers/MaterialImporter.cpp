#include "kbrpch.hpp"
#include "MaterialImporter.hpp"
#include "TextureImporter.hpp"
#include "Assets/AssetManager.hpp"
#include "Assets/EditorAssetManager.hpp"
#include "Project/Project.hpp"
#include "Serialization/SerializationUtils.hpp"

#include <yaml-cpp/yaml.h>

namespace Kerberos
{
	Ref<Material> MaterialImporter::ImportMaterial(AssetHandle, const AssetMetadata& metadata) 
	{
		return ImportMaterial(metadata.Filepath);
	}

	Ref<Material> MaterialImporter::ImportMaterial(const std::filesystem::path& filepath)
	{
		const auto& absolutePath = std::filesystem::absolute(filepath);

		YAML::Node node;
		try
		{
			node = YAML::LoadFile(absolutePath.string());
		}
		catch (const YAML::Exception& e)
		{
			KBR_CORE_ERROR("MaterialImporter::ImportMaterial - Failed to load yaml file: {}", e.what());
			return nullptr;
		}

		const std::string name = node["Name"].as<std::string>();
		const glm::vec3 albedo = node["Albedo"].as<glm::vec3>();
		const float roughness = node["Roughness"].as<float>();
		const float metallic = node["Metallic"].as<float>();

		const std::string albedoTexPath = node["AlbedoTexture"].as<std::string>("");
		const std::string normalTexPath = node["NormalTexture"].as<std::string>("");
		const std::string metallicTexPath = node["MetallicTexture"].as<std::string>("");
		const std::string roughnessTexPath = node["RoughnessTexture"].as<std::string>("");
		const std::string aoTexPath = node["AOTexture"].as<std::string>("");


		const Ref<EditorAssetManager> assetManager = Project::GetActive()->GetEditorAssetManager();

		Material material;
		material.name = name;
		material.Params = { .albedo = albedo, .roughness = roughness, .metallic = metallic };

		material.AlbedoTexture = albedoTexPath.empty() ? nullptr : AssetManager::GetAsset<Texture2D>(assetManager->ImportAsset(albedoTexPath));
		material.NormalTexture = normalTexPath.empty() ? nullptr : AssetManager::GetAsset<Texture2D>(assetManager->ImportAsset(normalTexPath));
		material.MetallicTexture = metallicTexPath.empty() ? nullptr : AssetManager::GetAsset<Texture2D>(assetManager->ImportAsset(metallicTexPath));
		material.RoughnessTexture = roughnessTexPath.empty() ? nullptr : AssetManager::GetAsset<Texture2D>(assetManager->ImportAsset(roughnessTexPath));
		material.AOTexture = aoTexPath.empty() ? nullptr : AssetManager::GetAsset<Texture2D>(assetManager->ImportAsset(aoTexPath));

		return CreateRef<Material>(material);
	}
}
