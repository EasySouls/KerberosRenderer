#include "kbrpch.hpp"
#include "EditorAssetManager.hpp"

#include "Assets/Importers/AssetImporter.hpp"
#include "Project/Project.hpp"
#include "ModelLoader.hpp"

#include <yaml-cpp/yaml.h>
#include <fstream>
#include <map>

namespace Kerberos
{
	static const std::map<std::string_view, AssetType> assetExtensionMap = {
		{ ".png", AssetType::Texture2D },
		{ ".jpg", AssetType::Texture2D },
		{ ".jpeg", AssetType::Texture2D },
		{ ".ktx", AssetType::Texture2D },
		{ ".ktx2", AssetType::Texture2D },
		{ ".kbrcubemap", AssetType::TextureCube },
		{ ".fbx", AssetType::Mesh },
		{ ".obj", AssetType::Mesh },
		{ ".kbrmesh", AssetType::Mesh },
		{ ".gltf", AssetType::Model },
		{ ".glb", AssetType::Model },
		{ ".kerberos", AssetType::Scene },
		{ ".wav", AssetType::Sound }, // TODO: Add more audio file types when supported
		{ ".kbrmat", AssetType::Material },
		{ ".kbrprefab", AssetType::Prefab },
		{ ".kbranim", AssetType::Animation }
	};

	static AssetType AssetTypeFromFileExtension(const std::filesystem::path& filepath)
	{
		const std::string extension = filepath.extension().string();
		const std::string_view extensionView(extension);

		if (assetExtensionMap.contains(extensionView))
		{
			return assetExtensionMap.at(extensionView);
		}

		KBR_CORE_WARN("Unknown asset type for file: {0}", filepath.string());
		return AssetType::Texture2D;
	}

	EditorAssetManager::EditorAssetManager()
	{
		AssetImporter::Init();

		m_DefaultFont = CreateRef<Font>("InterRegular", "Assets/Fonts/Inter/Inter_18pt-Regular.ttf");

		constexpr std::array<uint8_t, 4> albedoBuffer = { 255, 255, 255, 255 };
		TextureSpecification albedoSpec{};
		albedoSpec.Width = 1;
		albedoSpec.Height = 1;
		albedoSpec.Format = ImageFormat::RGBA8;
		const Buffer albedoBufferStruct{ sizeof(uint8_t) * albedoBuffer.size() };
		std::memcpy(albedoBufferStruct.Data, albedoBuffer.data(), albedoBufferStruct.Size);
		m_DefaultColorTexture = Texture2D::FromBuffer(albedoSpec, albedoBufferStruct);

		m_DefaultCubeMesh = CreateRef<Mesh>(ModelLoader::LoadModel("Assets/Models/cube.gltf", None));
	}

	EditorAssetManager::~EditorAssetManager() 
	{
		KBR_CORE_TRACE("EditorAssetManager destructed");
	}

	Ref<Asset> EditorAssetManager::GetAsset(const AssetHandle handle)
	{
		if (!IsAssetHandleValid(handle))
			return nullptr;

		Ref<Asset> asset = nullptr;
		if (IsAssetLoaded(handle))
		{
			asset = m_LoadedAssets.at(handle);
		}
		else
		{
			const AssetMetadata& metadata = GetMetadata(handle);
			asset = AssetImporter::ImportAsset(handle, metadata);
			if (!asset)
			{
				KBR_CORE_ERROR("Asset import failed!");
				return nullptr;
			}

			/// Assign the handle to the asset, since a random one was generated when creating the asset
			asset->GetHandle() = handle;

			/// Save the loaded asset
			m_LoadedAssets[handle] = asset;
		}

		return asset;
	}

	bool EditorAssetManager::IsAssetHandleValid(const AssetHandle handle) const
	{
		if (!handle.IsValid())
		{
			KBR_CORE_WARN("Asset handle is not valid: {}", handle);
			return false;
		}

		const bool isInAssetRegistry = m_AssetRegistry.Contains(handle);
		if (!isInAssetRegistry)
		{
			// TODO: Uncomment this when done with GLTF loading
			//KBR_CORE_WARN("Asset handle is not in asset registry: {}", handle);
			return false;
		}

		return true;
	}

	bool EditorAssetManager::IsAssetLoaded(const AssetHandle handle) const
	{
		return m_LoadedAssets.contains(handle);
	}

	AssetType EditorAssetManager::GetAssetType(AssetHandle handle) const
	{
		if (!IsAssetHandleValid(handle))
		{
			KBR_CORE_ERROR("Invalid asset handle: {0}", handle);
			throw std::runtime_error("Invalid asset handle when getting asset type!");
		}
		const auto& [Type, Filepath] = GetMetadata(handle);
		return Type;
	}

	AssetHandle EditorAssetManager::ImportAsset(const std::filesystem::path& filepath)
	{
		/// If the asset is already in the registry, return its handle
		if (m_AssetRegistry.ContainsPath(filepath))
		{
			return m_AssetRegistry.GetHandle(filepath);
		}

		/// Generate new handle
		const AssetHandle handle;
		AssetMetadata metadata;
		metadata.Filepath = filepath;
		metadata.Type = AssetTypeFromFileExtension(filepath);

		const Ref<Asset> asset = AssetImporter::ImportAsset(handle, metadata);
		if (!asset)
		{
			KBR_CORE_ERROR("Failed to import asset: {0}", filepath.string());
			return AssetHandle::Invalid();
		}

		/// Assign generated handle to asset
		asset->GetHandle() = handle;
		m_AssetRegistry[handle] = metadata;
		m_LoadedAssets[handle] = asset;

		SerializeAssetRegistry();

		return handle;
	}

	const AssetMetadata& EditorAssetManager::GetMetadata(const AssetHandle handle) const
	{
		return m_AssetRegistry.Get(handle);
	}

	Ref<Mesh> EditorAssetManager::GetDefaultCubeMesh() const
	{
		KBR_CORE_ASSERT(m_DefaultCubeMesh, "Default cube mesh has not been loaded");

		// TODO: Package a cube.gltf alongside the editor or create a mesh programmatically
		return m_DefaultCubeMesh;
	}

	Ref<Texture2D> EditorAssetManager::GetDefaultColorTexture() const 
	{
		KBR_CORE_ASSERT(m_DefaultColorTexture, "Default color texture has not been loaded");

		return m_DefaultColorTexture;
	}

	Ref<Font> EditorAssetManager::GetDefaultFont() const 
	{
		KBR_CORE_ASSERT(m_DefaultFont, "Default font has not been loaded");

		return m_DefaultFont;
	}

	void EditorAssetManager::SerializeAssetRegistry()
	{
		const std::filesystem::path& assetDirectoryPath = Project::GetAssetDirectory();
		const std::filesystem::path assetRegistryPath = assetDirectoryPath / "AssetRegistry.kbrar";

		YAML::Emitter out;
		{
			out << YAML::BeginMap;
			out << YAML::Key << "AssetRegistry" << YAML::Value << YAML::BeginSeq;

			for (const auto& [handle, metadata] : m_AssetRegistry)
			{
				out << YAML::BeginMap;
				out << YAML::Key << "Handle" << YAML::Value << handle;
				out << YAML::Key << "Type" << YAML::Value << AssetTypeToString(metadata.Type);
				out << YAML::Key << "Path" << YAML::Value << metadata.Filepath.string();
				out << YAML::EndMap;
			}

			out << YAML::EndSeq;
			out << YAML::EndMap;
		}

		std::ofstream file(assetRegistryPath);
		if (!file.is_open())
		{
			KBR_CORE_ERROR("Could not open asset registry file for writing: {0}", assetRegistryPath.string());
			return;
		}
		file << out.c_str();
	}

	bool EditorAssetManager::DeserializeAssetRegistry()
	{
		const std::filesystem::path& assetDirectoryPath = Project::GetAssetDirectory();
		const std::filesystem::path assetRegistryPath = assetDirectoryPath / "AssetRegistry.kbrar";

		YAML::Node data;
		try
		{
			data = YAML::LoadFile(assetRegistryPath.string());
		}
		catch (const YAML::Exception& e)
		{
			KBR_CORE_ERROR("Failed to load asset registry: {0}", e.what());
			KBR_CORE_ASSERT(false, "Failed to load asset registry");
			return false;
		}

		YAML::Node registryNode;
		try
		{
			registryNode = data["AssetRegistry"];
		}
		catch (const YAML::BadSubscript& e)
		{
			KBR_CORE_WARN("Registry was empty: {0}", e.what());
			return false;
		}

		if (!registryNode)
		{
			KBR_CORE_ERROR("Invalid asset registry file: {0}", assetRegistryPath.string());
			return false;
		}

		for (const auto& assetNode : registryNode)
		{
			if (!assetNode["Handle"] || !assetNode["Type"] || !assetNode["Path"])
			{
				KBR_CORE_ERROR("Invalid asset entry in registry: {0}", assetRegistryPath.string());
				KBR_CORE_ASSERT(false, "Invalid asset entry in registry");
				continue;
			}
			const AssetHandle handle = AssetHandle(assetNode["Handle"].as<uint64_t>());
			const std::string typeStr = assetNode["Type"].as<std::string>();
			const std::filesystem::path filepath = assetNode["Path"].as<std::string>();

			const AssetType type = AssetTypeFromString(typeStr);
			// TODO: This check is not needed when every asset type is supported
			if (type == AssetType::Texture2D || type == AssetType::TextureCube || type == AssetType::Mesh || type == AssetType::Sound || type == AssetType::Model || type == AssetType::Material || type == AssetType::Prefab || type == AssetType::Animation)
			{
				m_AssetRegistry.Add(handle, { .Type = type, .Filepath = filepath });
			}
			else
			{
				KBR_CORE_WARN("Unsupported asset type: {0}", typeStr);
				KBR_CORE_ASSERT(false, "Unsupported asset type");
			}
		}

		KBR_CORE_INFO("Asset registry loaded from {0}", assetRegistryPath.string());

		return true;
	}
}
