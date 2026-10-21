#include "MaterialImporter.hpp"
#include "Assets/AssetManagerBase.hpp"
#include "Assets/AssetRegistry.hpp"
#include "Project/Project.hpp"
#include "Serialization/SerializationUtils.hpp"
#include "Profiling/Profilers.hpp"

#include <yaml-cpp/yaml.h>
#include <algorithm>
#include <charconv>
#include <cctype>
#include <fstream>
#include <optional>
#include <string_view>
#include <system_error>

import Kerberos;

namespace Kerberos
{
	namespace
	{
		std::optional<AssetHandle> ParseAssetHandle(std::string_view text)
		{
			while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
				text.remove_prefix(1);
			while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
				text.remove_suffix(1);

			if (text.empty())
				return std::nullopt;

			uint64_t value = 0;
			const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
			if (error != std::errc{} || end != text.data() + text.size())
				return std::nullopt;

			return AssetHandle(value);
		}

		Ref<Texture2D> LoadTextureByHandle(const std::string& reference,
			const std::filesystem::path& materialPath)
		{
			if (reference.empty())
				return nullptr;

			const std::optional<AssetHandle> handle = ParseAssetHandle(reference);
			if (!handle)
			{
				Log::CoreWarn("MaterialImporter::ImportMaterial - material '{}' references texture '{}' by path; "
					"texture slots must use imported asset handles. Reselect the texture in the material editor.",
					materialPath.string(), reference);
				return nullptr;
			}

			if (!handle->IsValid())
				return nullptr;

			const Ref<Project> project = Project::GetActive();
			const Ref<AssetManagerBase> manager = project ? project->GetAssetManager() : nullptr;
			if (!manager)
			{
				Log::CoreWarn("MaterialImporter::ImportMaterial - cannot resolve texture handle {} without an active asset manager",
					static_cast<uint64_t>(*handle));
				return nullptr;
			}

			const AssetRegistry& registry = manager->GetAssetRegistry();
			if (!registry.Contains(*handle))
			{
				Log::CoreWarn("MaterialImporter::ImportMaterial - material '{}' references unknown texture handle {}",
					materialPath.string(), static_cast<uint64_t>(*handle));
				return nullptr;
			}

			if (registry.Get(*handle).Type != AssetType::Texture2D)
			{
				Log::CoreWarn("MaterialImporter::ImportMaterial - material '{}' references asset {} which is not a Texture2D",
					materialPath.string(), static_cast<uint64_t>(*handle));
				return nullptr;
			}

			if (!manager->IsAssetHandleValid(*handle))
			{
				Log::CoreWarn("MaterialImporter::ImportMaterial - material '{}' references texture {} with a missing imported artifact",
					materialPath.string(), static_cast<uint64_t>(*handle));
				return nullptr;
			}

			try
			{
				const Ref<Texture2D> texture = std::dynamic_pointer_cast<Texture2D>(manager->GetAsset(*handle));
				if (!texture)
					Log::CoreWarn("MaterialImporter::ImportMaterial - failed to load texture {} for material '{}'",
						static_cast<uint64_t>(*handle), materialPath.string());
				return texture;
			}
			catch (const std::exception& exception)
			{
				Log::CoreWarn("MaterialImporter::ImportMaterial - failed to load texture {} for material '{}': {}",
					static_cast<uint64_t>(*handle), materialPath.string(), exception.what());
				return nullptr;
			}
		}
	}

	Ref<Material> MaterialImporter::ImportMaterial(AssetHandle, const AssetMetadata& metadata) 
	{
		return ImportMaterial(metadata.Filepath);
	}

	Ref<Material> MaterialImporter::ImportMaterial(const std::filesystem::path& filepath)
	{
		KBR_TRACY_FUNCTION();

		const auto& absolutePath = std::filesystem::absolute(filepath);

		YAML::Node node;
		try
		{
			node = YAML::LoadFile(absolutePath.string());
		}
		catch (const YAML::Exception& e)
		{
			Log::CoreError("MaterialImporter::ImportMaterial - Failed to load yaml file: {}", e.what());
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

		material.AlbedoTexture = LoadTextureByHandle(albedoTexPath, absolutePath);
		material.NormalTexture = LoadTextureByHandle(normalTexPath, absolutePath);
		material.MetallicTexture = LoadTextureByHandle(metallicTexPath, absolutePath);
		material.RoughnessTexture = LoadTextureByHandle(roughnessTexPath, absolutePath);
		material.AOTexture = LoadTextureByHandle(aoTexPath, absolutePath);
		material.EmissiveTexture = LoadTextureByHandle(emissiveTexPath, absolutePath);
		return CreateRef<Material>(material);
	}

	bool MaterialImporter::SaveMaterial(const std::filesystem::path& filepath, const Material& material)
	{
		KBR_TRACY_FUNCTION();

		const Ref<Project> project = Project::GetActive();
		const Ref<AssetManagerBase> assetManager = project ? project->GetAssetManager() : nullptr;
		const AssetRegistry* registry = assetManager ? &assetManager->GetAssetRegistry() : nullptr;

		auto textureHandleFor = [&](const Ref<Texture2D>& texture) -> uint64_t
		{
			if (!texture)
				return 0;

			const AssetHandle handle = texture->GetHandle();
			if (!handle.IsValid())
				return 0;

			if (!registry || !registry->Contains(handle) || registry->Get(handle).Type != AssetType::Texture2D)
			{
				Log::CoreWarn("MaterialImporter::SaveMaterial - ignoring texture reference {} that is not a registered Texture2D asset",
					static_cast<uint64_t>(handle));
				return 0;
			}

			return static_cast<uint64_t>(handle);
		};

		YAML::Emitter out;
		out << YAML::BeginMap;
		out << YAML::Key << "Name" << YAML::Value << material.Name;
		out << YAML::Key << "Albedo" << YAML::Value << material.Params.AlbedoFactor;
		out << YAML::Key << "Roughness" << YAML::Value << material.Params.RoughnessFactor;
		out << YAML::Key << "Metallic" << YAML::Value << material.Params.MetallicFactor;
		out << YAML::Key << "EmissiveColor" << YAML::Value << material.EmissiveColor;
		out << YAML::Key << "EmissiveIntensity" << YAML::Value << material.EmissiveIntensity;
		out << YAML::Key << "AlbedoTexture" << YAML::Value << textureHandleFor(material.AlbedoTexture);
		out << YAML::Key << "NormalTexture" << YAML::Value << textureHandleFor(material.NormalTexture);
		out << YAML::Key << "MetallicTexture" << YAML::Value << textureHandleFor(material.MetallicTexture);
		out << YAML::Key << "RoughnessTexture" << YAML::Value << textureHandleFor(material.RoughnessTexture);
		out << YAML::Key << "AOTexture" << YAML::Value << textureHandleFor(material.AOTexture);
		out << YAML::Key << "EmissiveTexture" << YAML::Value << textureHandleFor(material.EmissiveTexture);
		out << YAML::EndMap;

		const std::filesystem::path absolutePath = std::filesystem::absolute(filepath);
		std::ofstream file(absolutePath);
		if (!file.is_open())
		{
			Log::CoreError("MaterialImporter::SaveMaterial - Failed to open file for writing: {}", absolutePath.string());
			return false;
		}

		file << out.c_str();
		return true;
	}
}