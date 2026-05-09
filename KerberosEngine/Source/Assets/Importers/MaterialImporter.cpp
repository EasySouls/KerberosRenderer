#include "kbrpch.hpp"
#include "MaterialImporter.hpp"
#include "TextureImporter.hpp"
#include "Assets/AssetManager.hpp"
#include "Assets/EditorAssetManager.hpp"
#include "Project/Project.hpp"
#include "Serialization/SerializationUtils.hpp"

#include <yaml-cpp/yaml.h>
#include <fstream>

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

        const std::string name = node["Name"].as<std::string>(filepath.stem().string());
		const glm::vec4 albedo = node["Albedo"].as<glm::vec4>(glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
		const float roughness = node["Roughness"].as<float>(1.0f);
		const float metallic = node["Metallic"].as<float>(0.0f);
		const glm::vec3 emissiveColor = node["EmissiveColor"].as<glm::vec3>(glm::vec3(0.0f, 0.0f, 0.0f));
		const float emissiveIntensity = node["EmissiveIntensity"].as<float>(0.0f);

		const std::string albedoTexPath = node["AlbedoTexture"].as<std::string>("");
		const std::string normalTexPath = node["NormalTexture"].as<std::string>("");
		const std::string metallicTexPath = node["MetallicTexture"].as<std::string>("");
		const std::string roughnessTexPath = node["RoughnessTexture"].as<std::string>("");
		const std::string aoTexPath = node["AOTexture"].as<std::string>("");
		const std::string emissiveTexPath = node["EmissiveTexture"].as<std::string>("");
		const Ref<EditorAssetManager> assetManager = Project::GetActive()->GetEditorAssetManager();

		Material material;
		material.Name = name;
		material.EmissiveColor = emissiveColor;
		material.EmissiveIntensity = emissiveIntensity;
		material.Params = { 
			.AlbedoFactor = albedo, 
			.Emissive = emissiveColor * emissiveIntensity, 
			.RoughnessFactor = roughness, 
			.MetallicFactor = metallic 
		};

		auto loadTexture = [&](const std::string& texturePath) -> Ref<Texture2D>
		{
			if (texturePath.empty())
				return nullptr;

			//std::filesystem::path resolvedPath = texturePath;
			//if (resolvedPath.is_relative())
			//{
			//	resolvedPath = absolutePath.parent_path() / resolvedPath;
			//}

			/*const AssetHandle textureHandle = assetManager->ImportAsset(resolvedPath);
			if (!textureHandle.IsValid())
			{
				KBR_CORE_WARN("MaterialImporter::ImportMaterial - Failed to import texture: {}", resolvedPath.string());
				return nullptr;
			}*/

			const AssetHandle textureHandle = assetManager->ImportAsset(texturePath);
			if (!textureHandle.IsValid())
			{
				KBR_CORE_WARN("MaterialImporter::ImportMaterial - Failed to import texture: {}", texturePath);
				return nullptr;
			}

			return AssetManager::GetAsset<Texture2D>(textureHandle);
		};

		material.AlbedoTexture = loadTexture(albedoTexPath);
		material.NormalTexture = loadTexture(normalTexPath);
		material.MetallicTexture = loadTexture(metallicTexPath);
		material.RoughnessTexture = loadTexture(roughnessTexPath);
		material.AOTexture = loadTexture(aoTexPath);
		material.EmissiveTexture = loadTexture(emissiveTexPath);
		return CreateRef<Material>(material);
	}

	bool MaterialImporter::SaveMaterial(const std::filesystem::path& filepath, const Material& material)
	{
		auto texturePathFor = [&](const Ref<Texture2D>& texture) -> std::string
		{
			if (!texture || !texture->GetHandle().IsValid())
				return "";

			const Ref<EditorAssetManager> assetManager = Project::GetActive()->GetEditorAssetManager();
			if (!assetManager || !assetManager->IsAssetHandleValid(texture->GetHandle()))
				return "";

			std::filesystem::path texturePath = assetManager->GetMetadata(texture->GetHandle()).Filepath;
			if (texturePath.is_absolute())
			{
				std::error_code ec;
				const std::filesystem::path relativePath = std::filesystem::relative(texturePath, filepath.parent_path(), ec);
				if (!ec)
					texturePath = relativePath;
			}

			return texturePath.generic_string();
		};

		YAML::Emitter out;
		out << YAML::BeginMap;
		out << YAML::Key << "Name" << YAML::Value << material.Name;
		out << YAML::Key << "Albedo" << YAML::Value << material.Params.AlbedoFactor;
		out << YAML::Key << "Roughness" << YAML::Value << material.Params.RoughnessFactor;
		out << YAML::Key << "Metallic" << YAML::Value << material.Params.MetallicFactor;
		out << YAML::Key << "EmissiveColor" << YAML::Value << material.EmissiveColor;
		out << YAML::Key << "EmissiveIntensity" << YAML::Value << material.EmissiveIntensity;
		out << YAML::Key << "AlbedoTexture" << YAML::Value << texturePathFor(material.AlbedoTexture);
		out << YAML::Key << "NormalTexture" << YAML::Value << texturePathFor(material.NormalTexture);
		out << YAML::Key << "MetallicTexture" << YAML::Value << texturePathFor(material.MetallicTexture);
		out << YAML::Key << "RoughnessTexture" << YAML::Value << texturePathFor(material.RoughnessTexture);
		out << YAML::Key << "AOTexture" << YAML::Value << texturePathFor(material.AOTexture);
		out << YAML::Key << "EmissiveTexture" << YAML::Value << texturePathFor(material.EmissiveTexture);
		out << YAML::EndMap;

		std::ofstream file(filepath);
		if (!file.is_open())
		{
			KBR_CORE_ERROR("MaterialImporter::SaveMaterial - Failed to open file for writing: {}", filepath.string());
			return false;
		}

		file << out.c_str();
		return true;
	}
}
