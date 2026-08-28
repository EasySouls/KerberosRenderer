#include "kbrpch.hpp"
#include "EditorAssetManager.hpp"

#include "Assets/Importers/AssetImporter.hpp"
#include "Assets/Importers/IAssetImporter.hpp"
#include "Assets/Importers/GltfSceneImporter.hpp"
#include "Assets/Formats/NativeAssetSerializer.hpp"
#include "Project/Project.hpp"
#include "Application.hpp"
#include "ModelLoader.hpp"

#include <yaml-cpp/yaml.h>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <ranges>

namespace Kerberos
{
	namespace
	{
		class GltfPipelineImporter final : public IAssetImporter
		{
		public:
			bool SupportsExtension(const std::string_view extension) const override
			{
				return extension == ".gltf" || extension == ".glb";
			}
			ImportResult Import(const ImportContext& context) override
			{
				GltfSceneManifest manifest;
				if (!GltfSceneImporter::Import(context.SourceAbsolutePath, context.CacheRootAbsolutePath, &manifest))
					throw std::runtime_error("Failed to build glTF scene");
				ImportResult result;
				result.SourceHandle = context.Meta.SourceHandle.IsValid() ? context.Meta.SourceHandle : AssetHandle();
				if (!result.SourceHandle.IsValid())
					result.SourceHandle = AssetHandle();
				std::error_code ec;
				for (const auto& entry : std::filesystem::recursive_directory_iterator(context.CacheRootAbsolutePath, ec))
				{
					if (ec || !entry.is_regular_file() || entry.path().extension() != ".kbrmesh" && entry.path().extension() != ".kbrmaterial" && entry.path().extension() != ".kbrtexture" && entry.path().extension() != ".kbrskeleton" && entry.path().extension() != ".kbranimation" && entry.path().extension() != ".kbrprefab")
						continue;
					NativeAssetRecord record;
					AssetType type = AssetType::Prefab;
					if (entry.path().extension() == ".kbrmesh") {
						type = AssetType::Mesh;
						record.LocalKey = "mesh:" + entry.path().stem().string().substr(5);
					} else {
						if (!NativeAssetSerializer::DeserializeRecord(entry.path(), record))
							continue;
						if (record.Kind == "material") type = AssetType::Material;
						else if (record.Kind == "texture") type = AssetType::Texture2D;
						else if (record.Kind == "skeleton") type = AssetType::Skin;
						else if (record.Kind == "animation") type = AssetType::Animation;
					}
					AssetHandle handle = AssetHandle::Invalid();
					for (const auto& old : context.Meta.SubAssets)
						if (old.LocalKey == record.LocalKey) { handle = old.Handle; break; }
					if (!handle.IsValid()) handle = AssetHandle();
					result.Outputs.push_back({ handle, type, std::filesystem::relative(entry.path(), context.CacheRootAbsolutePath, ec), record.LocalKey, {} });
				}
				for (auto& output : result.Outputs)
				{
					if (output.Type == AssetType::Material)
					{
						for (const auto& candidate : result.Outputs)
							if (candidate.Type == AssetType::Texture2D && candidate.Handle.IsValid())
								output.Dependencies.push_back(candidate.Handle);
					}
					else if (output.Type == AssetType::Prefab)
					{
						for (const auto& candidate : result.Outputs)
							if (candidate.Type == AssetType::Mesh || candidate.Type == AssetType::Material || candidate.Type == AssetType::Skin || candidate.Type == AssetType::Animation)
								if (candidate.Handle.IsValid()) output.Dependencies.push_back(candidate.Handle);
					}
				}
				return result;
			}
			ImporterType Type() const override { return ImporterType::GLTFScene; }
		};
	}
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
		std::string extension = filepath.extension().string();
		std::ranges::transform(extension, extension.begin(), [](unsigned char c) {
			return static_cast<char>(std::tolower(c));
		});
		const std::string_view extensionView(extension);

		if (assetExtensionMap.contains(extensionView))
		{
			return assetExtensionMap.at(extensionView);
		}

		KBR_CORE_WARN("Unknown asset type for file: {0}", filepath.string());
		return AssetType::Texture2D;
	}

	EditorAssetManager::EditorAssetManager(const std::filesystem::path& assetsRoot, const std::filesystem::path& cacheRoot)
	{
		AssetImporter::Init();

		if (!assetsRoot.empty())
		{
			ConfigurePipeline(assetsRoot, cacheRoot);
		}

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

	void EditorAssetManager::ConfigurePipeline(const std::filesystem::path& assetsRoot, const std::filesystem::path& cacheRoot)
	{
		m_AssetsRoot = std::filesystem::absolute(assetsRoot);
		m_CacheRoot = cacheRoot.empty() ? m_AssetsRoot / "Cache" : std::filesystem::absolute(cacheRoot);
		std::error_code ec;
		std::filesystem::create_directories(m_AssetsRoot, ec);
		std::filesystem::create_directories(m_CacheRoot, ec);
		m_MetaService = CreateOwner<AssetMetaService>(m_AssetsRoot);
		m_ImporterRegistry.Register(CreateRef<GltfPipelineImporter>());
		m_BuildCoordinator = CreateOwner<AssetBuildCoordinator>(m_AssetsRoot, m_CacheRoot, *m_MetaService, m_ImporterRegistry, &m_AssetRegistry);

		std::unordered_set<std::string> extensions;
		for (const auto& extension : assetExtensionMap | std::views::keys)
			extensions.emplace(extension);

		const auto lifetime = m_Lifetime;

		m_FileWatch.Start(m_AssetsRoot, extensions, [this, lifetime](const AssetFileEvent& event) {
			Application::Get().SubmitToMainThreadQueue([this, lifetime, event] {
				if (lifetime->load())
					HandleAssetFileEvent(event);
			});
		});
	}

	void EditorAssetManager::EnsureAssetMetas() const
    {
        KBR_PROFILE_FUNCTION();

		if (!m_MetaService || m_AssetsRoot.empty())
			return;

        constexpr AssetSourceScanner scanner;
		const auto files = scanner.ScanForAssets(m_AssetsRoot, { ".gltf", ".glb" });
		for (const auto& file : files)
		{
			auto meta = m_MetaService->EnsureMetaForSource(file);
			const auto relativeFile = std::filesystem::relative(file, m_AssetsRoot);
			if (!meta.SourceHandle.IsValid() &&
				(m_AssetRegistry.ContainsPath(file) || m_AssetRegistry.ContainsPath(relativeFile)))
			{
				meta.SourceHandle = m_AssetRegistry.ContainsPath(file)
					? m_AssetRegistry.GetHandle(file) : m_AssetRegistry.GetHandle(relativeFile);
				m_MetaService->SaveMeta(file, meta);
			}
		}
	}

	std::vector<AssetBuildReport> EditorAssetManager::BuildAssets(const bool force) const
    {
        KBR_PROFILE_FUNCTION();

		if (!m_BuildCoordinator || m_AssetsRoot.empty())
			return {};
        constexpr AssetSourceScanner scanner;
		const auto files = scanner.ScanForAssets(m_AssetsRoot, { ".gltf", ".glb" });
		return m_BuildCoordinator->BuildAll(files, force);
	}

	EditorAssetManager::~EditorAssetManager() 
	{
		m_Lifetime->store(false);
		m_FileWatch.Stop();
		KBR_CORE_TRACE("EditorAssetManager destructed");
	}

	void EditorAssetManager::HandleAssetFileEvent(const AssetFileEvent& event)
	{
        KBR_PROFILE_FUNCTION();

		if (!m_BuildCoordinator)
			return;

		const auto relative = std::filesystem::relative(event.Path, m_AssetsRoot);
		if (event.Type == AssetFileEventType::Removed)
		{
			if (m_AssetRegistry.ContainsPath(relative))
			{
				const auto handle = m_AssetRegistry.GetHandle(relative);
				m_LoadedAssets.erase(handle);
				m_AssetRegistry.Remove(handle);
				SerializeAssetRegistry();
			}
			return;
		}

		if (event.Type == AssetFileEventType::Renamed &&
			m_AssetRegistry.ContainsPath(std::filesystem::relative(event.OldPath, m_AssetsRoot)))
		{
			const auto handle = m_AssetRegistry.GetHandle(
				std::filesystem::relative(event.OldPath, m_AssetsRoot));
			m_AssetRegistry.Get(handle).Filepath = relative;
		}

		const auto report = m_BuildCoordinator->Build(event.Path, event.Type == AssetFileEventType::Modified);
		if (report.Built)
			SerializeAssetRegistry();
	}

	Ref<Asset> EditorAssetManager::GetAsset(const AssetHandle handle)
	{
        KBR_PROFILE_FUNCTION();

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
        KBR_PROFILE_FUNCTION();

		if (!IsAssetHandleValid(handle))
		{
			KBR_CORE_ERROR("Invalid asset handle: {0}", handle);
			throw std::runtime_error("Invalid asset handle when getting asset type!");
		}
		return GetMetadata(handle).Type;
	}

	AssetHandle EditorAssetManager::ImportAsset(const std::filesystem::path& filepath)
	{
        KBR_PROFILE_FUNCTION();

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
		m_AssetRegistry.Add(handle, metadata);
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
        KBR_PROFILE_FUNCTION();

		const std::filesystem::path assetDirectoryPath = m_AssetsRoot.empty()
			? (Project::GetProjectDirectory() / Project::GetAssetDirectory())
			: m_AssetsRoot;
		const std::filesystem::path assetRegistryPath = assetDirectoryPath / "AssetRegistry.kbrar";

		YAML::Emitter out;
		{
			out << YAML::BeginMap;
			out << YAML::Key << "SchemaVersion" << YAML::Value << 1;
			out << YAML::Key << "AssetRegistry" << YAML::Value << YAML::BeginSeq;

			for (const auto& [handle, metadata] : m_AssetRegistry)
			{
				out << YAML::BeginMap;
				out << YAML::Key << "Handle" << YAML::Value << handle;
				out << YAML::Key << "Type" << YAML::Value << AssetTypeToString(metadata.Type);
				out << YAML::Key << "Path" << YAML::Value << metadata.Filepath.string();
				if (!metadata.LibraryPath.empty())
					out << YAML::Key << "LibraryPath" << YAML::Value << metadata.LibraryPath.string();
				out << YAML::Key << "RootSourceHandle" << YAML::Value << static_cast<uint64_t>(metadata.RootSourceHandle);
				out << YAML::Key << "ParentHandle" << YAML::Value << static_cast<uint64_t>(metadata.ParentHandle);
				out << YAML::Key << "SubAssetKey" << YAML::Value << metadata.SubAssetKey;
				out << YAML::Key << "ImportVersion" << YAML::Value << metadata.ImportVersion;
				if (!metadata.Dependencies.empty()) {
					out << YAML::Key << "Dependencies" << YAML::Value << YAML::BeginSeq;
					for (const auto dependency : metadata.Dependencies) out << static_cast<uint64_t>(dependency);
					out << YAML::EndSeq;
				}
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
        KBR_PROFILE_FUNCTION();

		const std::filesystem::path assetDirectoryPath = m_AssetsRoot.empty()
			? (Project::GetProjectDirectory() / Project::GetAssetDirectory())
			: m_AssetsRoot;
		const std::filesystem::path assetRegistryPath = assetDirectoryPath / "AssetRegistry.kbrar";
		if (!std::filesystem::exists(assetRegistryPath))
		{
			KBR_CORE_INFO("No asset registry found; it will be created as assets are imported.");
			return false;
		}

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

			AssetMetadata metadata{ .Type = type, .Filepath = filepath };
			if (assetNode["LibraryPath"]) metadata.LibraryPath = assetNode["LibraryPath"].as<std::string>();
			if (assetNode["RootSourceHandle"]) metadata.RootSourceHandle = AssetHandle(assetNode["RootSourceHandle"].as<uint64_t>());
			if (assetNode["ParentHandle"]) metadata.ParentHandle = AssetHandle(assetNode["ParentHandle"].as<uint64_t>());
			if (assetNode["SubAssetKey"]) metadata.SubAssetKey = assetNode["SubAssetKey"].as<std::string>();
			if (assetNode["ImportVersion"]) metadata.ImportVersion = assetNode["ImportVersion"].as<uint64_t>();
			if (assetNode["Dependencies"])
				for (const auto& dependency : assetNode["Dependencies"])
					metadata.Dependencies.push_back(AssetHandle(dependency.as<uint64_t>()));

			m_AssetRegistry.Add(handle, metadata);

		}

		KBR_CORE_INFO("Asset registry loaded from {0}", assetRegistryPath.string());

		return true;
	}
}
