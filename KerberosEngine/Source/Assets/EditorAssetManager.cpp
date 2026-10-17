#include "EditorAssetManager.hpp"
#include "Core/Core.hpp"

#include "Assets/Runtime/RuntimeAssetLoader.hpp"
#include "Assets/Importers/StandaloneAssetPipelineImporter.hpp"
#include "Assets/Importers/TexturePipelineImporter.hpp"
#include "Importers/GLTFPipelineImporter.hpp"
#include "Assets/Formats/NativeAssetSerializer.hpp"
#include "Profiling/Profilers.hpp"
#include "Project/Project.hpp"
#include "Application.hpp"
#include "InvalidAssetHandleException.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <ranges>
#include <system_error>

#ifdef KBR_PLATFORM_WINDOWS
#include <Windows.h>
#endif


import Kerberos;

namespace Kerberos
{
	namespace
	{
		Ref<Mesh> CreateEditorDefaultCube()
		{
            constexpr std::array positions = {
				glm::vec3(-0.5f, -0.5f, -0.5f), glm::vec3(0.5f, -0.5f, -0.5f),
				glm::vec3(0.5f, 0.5f, -0.5f), glm::vec3(-0.5f, 0.5f, -0.5f),
				glm::vec3(-0.5f, -0.5f, 0.5f), glm::vec3(0.5f, -0.5f, 0.5f),
				glm::vec3(0.5f, 0.5f, 0.5f), glm::vec3(-0.5f, 0.5f, 0.5f)
			};
			const std::vector<uint32_t> indices = {
				0, 1, 2, 2, 3, 0, 4, 6, 5, 6, 4, 7,
				0, 4, 5, 5, 1, 0, 3, 2, 6, 6, 7, 3,
				0, 3, 7, 7, 4, 0, 1, 5, 6, 6, 2, 1
			};
			std::vector<Vertex> vertices;
			vertices.reserve(positions.size());
			for (const auto& position : positions)
				vertices.push_back({ .Position = position, .Tangent = glm::vec4(0, 0, 0, 1) });
			return CreateRef<Mesh>("EditorDefaultCube", vertices, indices);
		}
	}

	static const std::map<std::string_view, AssetType> sourceAssetExtensionMap = {
		{ ".png", AssetType::Texture2D },
		{ ".jpg", AssetType::Texture2D },
		{ ".jpeg", AssetType::Texture2D },
		{ ".ktx", AssetType::Texture2D },
		{ ".ktx2", AssetType::Texture2D },
		{ ".kbrcubemap", AssetType::TextureCube },
		{ ".fbx", AssetType::Mesh },
		{ ".obj", AssetType::Mesh },
		{ ".gltf", AssetType::Prefab },
		{ ".glb", AssetType::Prefab },
		{ ".kerberos", AssetType::Scene },
		{ ".wav", AssetType::Sound }, // TODO: Add more audio file types when supported
		{ ".kbrmat", AssetType::Material },
		{ ".kbrprefab", AssetType::Prefab },
		{ ".kbranim", AssetType::Animation }
	};

	static std::vector<std::string> SourceAssetExtensions()
	{
		std::vector<std::string> extensions;
		extensions.reserve(sourceAssetExtensionMap.size());
		for (const auto& extension : sourceAssetExtensionMap | std::views::keys)
			extensions.emplace_back(extension);
		return extensions;
	}

	static AssetType AssetTypeFromFileExtension(const std::filesystem::path& filepath)
	{
		std::string extension = filepath.extension().string();
		std::ranges::transform(extension, extension.begin(), [](unsigned char c) {
			return static_cast<char>(std::tolower(c));
		});
		const std::string_view extensionView(extension);

		if (sourceAssetExtensionMap.contains(extensionView))
		{
			return sourceAssetExtensionMap.at(extensionView);
		}

		Log::CoreWarn("Unknown asset type for file: {0}", filepath.string());
		return AssetType::Texture2D;
	}

	EditorAssetManager::EditorAssetManager(const std::filesystem::path& assetsRoot, const std::filesystem::path& cacheRoot)
	{
		KBR_TRACY_FUNCTION();

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

		m_DefaultCubeMesh = CreateEditorDefaultCube();
	}

	void EditorAssetManager::ConfigurePipeline(const std::filesystem::path& assetsRoot, const std::filesystem::path& cacheRoot)
	{
		KBR_TRACY_FUNCTION();

		m_AssetsRoot = std::filesystem::absolute(assetsRoot);
		m_CacheRoot = cacheRoot.empty() ? m_AssetsRoot / "Cache" : std::filesystem::absolute(cacheRoot);
		std::error_code ec;
		std::filesystem::create_directories(m_AssetsRoot, ec);
		std::filesystem::create_directories(m_CacheRoot, ec);
		m_MetaService = CreateOwner<AssetMetaService>(m_AssetsRoot);
		m_ImporterRegistry.Register(CreateRef<GLTFPipelineImporter>());
		m_ImporterRegistry.Register(CreateRef<TexturePipelineImporter>());
		for (const auto& [extension, type] : sourceAssetExtensionMap)
		{
			if (extension == ".gltf" || extension == ".glb")
				continue;

			if (type == AssetType::Texture2D)
				continue;

			// Register a generic standalone importer for all other asset types
			m_ImporterRegistry.Register(CreateRef<StandaloneAssetPipelineImporter>(
				std::string(extension), type));
		}
		m_BuildCoordinator = CreateOwner<AssetBuildCoordinator>(m_AssetsRoot, m_CacheRoot, *m_MetaService, m_ImporterRegistry, &m_AssetRegistry);
	}

	void EditorAssetManager::StartWatching()
	{
		KBR_TRACY_FUNCTION();

		if (m_AssetsRoot.empty())
			return;

		std::unordered_set<std::string> extensions;
		for (const auto& extension : SourceAssetExtensions())
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
        KBR_TRACY_FUNCTION();

		if (!m_MetaService || m_AssetsRoot.empty())
			return;

        constexpr AssetSourceScanner scanner;
		const auto files = scanner.ScanForAssets(m_AssetsRoot, SourceAssetExtensions());
		for (const auto& file : files)
		{
			auto meta = m_MetaService->EnsureMetaForSource(file);
			const auto relativeFile = std::filesystem::relative(file, m_AssetsRoot);
			if (!meta.SourceHandle.IsValid() &&
				(m_AssetRegistry.ContainsPath(file) || m_AssetRegistry.ContainsPath(relativeFile)))
			{
				meta.SourceHandle = m_AssetRegistry.ContainsPath(file)
					? m_AssetRegistry.GetHandle(file) : m_AssetRegistry.GetHandle(relativeFile);

				if (!m_MetaService->SaveMeta(file, meta))
				{
					Log::CoreError("Failed to save asset metadata for {0}", file.string());
				}
			}
		}
	}

	std::vector<AssetBuildReport> EditorAssetManager::BuildAssets(const bool force) const
    {
        KBR_TRACY_FUNCTION();

		if (!m_BuildCoordinator || m_AssetsRoot.empty())
			return {};
        constexpr AssetSourceScanner scanner;
		const auto files = scanner.ScanForAssets(m_AssetsRoot, SourceAssetExtensions());
		return m_BuildCoordinator->BuildAll(files, force);
	}

	EditorAssetManager::~EditorAssetManager() 
	{
        KBR_TRACY_FUNCTION();

		m_Lifetime->store(false);
		m_FileWatch.Stop();
		Log::CoreTrace("EditorAssetManager destructed");
	}

	void EditorAssetManager::HandleAssetFileEvent(const AssetFileEvent& event)
	{
        KBR_TRACY_FUNCTION();

		if (!m_BuildCoordinator)
			return;

		const auto relative = std::filesystem::relative(event.Path, m_AssetsRoot);
		if (event.Type == AssetFileEventType::Removed)
		{
			if (m_AssetRegistry.ContainsPath(relative))
			{
				const auto rootHandle = m_AssetRegistry.GetHandle(relative);
				std::vector<AssetHandle> handlesToRemove;
				for (const auto& [handle, metadata] : m_AssetRegistry)
				{
					if (handle == rootHandle || metadata.RootSourceHandle == rootHandle ||
						metadata.ParentHandle == rootHandle)
						handlesToRemove.push_back(handle);
				}
				for (const auto handle : handlesToRemove)
				{
					m_LoadedAssets.erase(handle);
					m_AssetRegistry.Remove(handle);
				}
				SerializeAssetRegistry();
			}

			Log::CoreInfo("Asset removed: {0}", relative.string());

			return;
		}

		if (event.Type == AssetFileEventType::Renamed)
		{
			const auto oldRelative = std::filesystem::relative(event.OldPath, m_AssetsRoot);
			if (m_AssetRegistry.ContainsPath(oldRelative))
			{
				const auto handle = m_AssetRegistry.GetHandle(oldRelative);
				m_AssetRegistry.Get(handle).Filepath = relative;
			}
			if (m_MetaService->RebindMetaOnRename(event.OldPath, event.Path))
			{
				Log::CoreWarn("Could not rebind asset metadata from {0} to {1}",
                             event.OldPath.string(), event.Path.string());
			}

			Log::CoreInfo("Asset renamed: {0}", relative.string());
		}

		if (event.Type == AssetFileEventType::Added) {
            Log::CoreInfo("Asset added: {0}", relative.string());
        }

		const auto report = m_BuildCoordinator->Build(event.Path);
		if (report.Built && (report.Reason != AssetStaleReason::None || event.Type == AssetFileEventType::Renamed))
		{
			SerializeAssetRegistry();
		}

		if (event.Type == AssetFileEventType::Modified && report.Reason != AssetStaleReason::None)
		{
            Log::CoreInfo("Asset modified: {0}", relative.string());
        }

		if (report.Warnings.size() > 0) {
            Log::CoreWarn("Asset build completed with warnings: {}", report.Source.string());
            for (const auto& warning : report.Warnings) {
                Log::CoreWarn("  - {}", warning);
            }
        }
        else if (report.Errors.size() > 0) {
            Log::CoreError("Asset build failed with errors: {}", report.Source.string());
            for (const auto& error : report.Errors) {
                Log::CoreError("  - {}", error);
            }
        }
	}

	Ref<Asset> EditorAssetManager::GetAsset(const AssetHandle handle)
	{
        KBR_TRACY_FUNCTION();

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
			if (metadata.LibraryPath.empty())
			{
				Log::CoreError("Asset {0} has no imported library artifact", handle);
				return nullptr;
			}

			const auto artifactPath = metadata.LibraryPath.is_absolute()
				? metadata.LibraryPath
				: m_CacheRoot / metadata.LibraryPath;
			asset = RuntimeAssetLoader::Load(handle, metadata, artifactPath, &m_AssetRegistry, m_CacheRoot);
			if (!asset)
			{
				throw std::runtime_error(
					std::format("Imported artifact load failed for asset {}: {}", handle, artifactPath.string()));
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
			//Log::CoreWarn("Asset handle is not in asset registry: {}", handle);
			return false;
		}

		const AssetMetadata& metadata = m_AssetRegistry.Get(handle);
		if (metadata.LibraryPath.empty())
			return false;

		const std::filesystem::path artifactPath = metadata.LibraryPath.is_absolute()
			? metadata.LibraryPath
			: m_CacheRoot / metadata.LibraryPath;
		return std::filesystem::exists(artifactPath);
	}

	bool EditorAssetManager::IsAssetLoaded(const AssetHandle handle) const
	{
		return m_LoadedAssets.contains(handle);
	}

	AssetType EditorAssetManager::GetAssetType(AssetHandle handle) const
	{
        KBR_TRACY_FUNCTION();

		if (!IsAssetHandleValid(handle))
		{
			Log::CoreError("Invalid asset handle: {0}", handle);
            throw InvalidAssetHandleException(handle);
		}
		return GetMetadata(handle).Type;
	}

	AssetHandle EditorAssetManager::ImportAsset(const std::filesystem::path& filepath)
    {
        KBR_TRACY_FUNCTION();

		const auto absolutePath = std::filesystem::absolute(filepath);
		const auto registryPath = m_AssetsRoot.empty()
			? filepath
			: std::filesystem::relative(absolutePath, m_AssetsRoot);

		/// Keep the fast path for a registered asset whose imported artifact exists.
		if (m_AssetRegistry.ContainsPath(registryPath))
		{
			const AssetHandle existingHandle = m_AssetRegistry.GetHandle(registryPath);
			if (IsAssetHandleValid(existingHandle))
				return existingHandle;
		}

		AssetMetadata metadata;
		metadata.Filepath = registryPath;
		metadata.Type = AssetTypeFromFileExtension(registryPath);

		const auto report = m_BuildCoordinator->Build(absolutePath, true);
		if (!report.Built)
		{
			Log::CoreError("Failed to import asset: {}", absolutePath.string());
			for (const auto& error : report.Errors)
				Log::CoreError("  - {}", error);
			return AssetHandle::Invalid();
		}

		SerializeAssetRegistry();
		return m_AssetRegistry.GetHandle(registryPath);
	}

	const AssetMetadata& EditorAssetManager::GetMetadata(const AssetHandle handle) const
	{
		return m_AssetRegistry.Get(handle);
	}

	std::optional<AssetMetaFile> EditorAssetManager::GetMetaFile(const std::filesystem::path& sourcePath) const
	{
		if (!m_MetaService)
			return std::nullopt;
		return m_MetaService->LoadMeta(sourcePath);
	}

	AssetHandle EditorAssetManager::SetTextureSrgbOptions(const std::filesystem::path& sourcePath,
		const std::map<std::string, bool>& textureSrgb)
	{
		KBR_TRACY_FUNCTION();

		if (!m_MetaService)
			return AssetHandle::Invalid();

		auto meta = m_MetaService->LoadMeta(sourcePath);
		if (!meta.has_value())
		{
			Log::CoreWarn("Cannot set import options for a source with no metadata: {}", sourcePath.string());
			return AssetHandle::Invalid();
		}
		meta->TextureSrgb = textureSrgb;
		if (!m_MetaService->SaveMeta(sourcePath, *meta))
		{
			Log::CoreError("Failed to persist import options for {}", sourcePath.string());
			return AssetHandle::Invalid();
		}

		return ReimportAsset(sourcePath);
	}

	AssetHandle EditorAssetManager::ReimportAsset(const std::filesystem::path& sourcePath)
	{
		KBR_TRACY_FUNCTION();

		if (!m_BuildCoordinator)
			return AssetHandle::Invalid();

		const auto absolutePath = std::filesystem::absolute(sourcePath);
		const auto report = m_BuildCoordinator->Build(absolutePath, true);
		if (!report.Built)
		{
			for (const auto& error : report.Errors)
				Log::CoreError("  {}", error);
			return AssetHandle::Invalid();
		}

		const auto registryPath = m_AssetsRoot.empty()
			? sourcePath
			: std::filesystem::relative(absolutePath, m_AssetsRoot);
		const AssetHandle rootHandle = m_AssetRegistry.ContainsPath(registryPath)
			? m_AssetRegistry.GetHandle(registryPath)
			: AssetHandle::Invalid();

		if (rootHandle.IsValid())
		{
			/// Evict cached loaded artifacts so consumers reload the regenerated outputs.
			for (auto it = m_LoadedAssets.begin(); it != m_LoadedAssets.end();)
			{
				const AssetHandle handle = it->first;
				const bool isRoot = handle == rootHandle;
				const bool isChild = m_AssetRegistry.Contains(handle)
					&& m_AssetRegistry.Get(handle).RootSourceHandle == rootHandle;
				if (isRoot || isChild)
					it = m_LoadedAssets.erase(it);
				else
					++it;
			}
		}

		SerializeAssetRegistry();
		return rootHandle;
	}

	Ref<Mesh> EditorAssetManager::GetDefaultCubeMesh() const
	{
		KBRAssert(m_DefaultCubeMesh != nullptr, "Default cube mesh has not been loaded");

		// TODO: Package a cube.gltf alongside the editor or create a mesh programmatically
		return m_DefaultCubeMesh;
	}

	Ref<Texture2D> EditorAssetManager::GetDefaultColorTexture() const 
	{
		KBRAssert(m_DefaultColorTexture != nullptr, "Default color texture has not been loaded");

		return m_DefaultColorTexture;
	}

	Ref<Font> EditorAssetManager::GetDefaultFont() const 
	{
		KBRAssert(m_DefaultFont != nullptr, "Default font has not been loaded");

		return m_DefaultFont;
	}

	void EditorAssetManager::SerializeAssetRegistry()
	{
        KBR_TRACY_FUNCTION();

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

		const std::filesystem::path temporaryRegistryPath = assetRegistryPath.string() + ".tmp";
        try
		{
            std::ofstream file;
            file.exceptions(std::ios::failbit | std::ios::badbit);
            file.open(temporaryRegistryPath);

			file << out.c_str();
			file.close();
		}
		catch (const std::ios_base::failure& e)
		{
            const std::error_code ec = e.code();

			Log::CoreError("Could not open asset registry file for writing: {0}. Reason: {1} (Error code: {2})", assetRegistryPath.string(), ec.message(), ec.value());
			std::error_code cleanupError;
			std::filesystem::remove(temporaryRegistryPath, cleanupError);
			return;
		}

		try
		{
#ifdef KBR_PLATFORM_WINDOWS
			const BOOL replaced = MoveFileExW(
				temporaryRegistryPath.c_str(),
				assetRegistryPath.c_str(),
				MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
			if (!replaced)
			{
				throw std::filesystem::filesystem_error(
					"Could not replace asset registry file",
					temporaryRegistryPath,
					assetRegistryPath,
					std::error_code(static_cast<int>(GetLastError()), std::system_category()));
			}
#else
			std::filesystem::rename(temporaryRegistryPath, assetRegistryPath);
#endif
		}
		catch (const std::filesystem::filesystem_error& e)
		{
			const std::error_code ec = e.code();
			Log::CoreError("Could not replace asset registry file: {0}. Reason: {1} (Error code: {2})", assetRegistryPath.string(), ec.message(), ec.value());
			std::error_code cleanupError;
			std::filesystem::remove(temporaryRegistryPath, cleanupError);
		}
	}

	bool EditorAssetManager::DeserializeAssetRegistry()
	{
        KBR_TRACY_FUNCTION();

		const std::filesystem::path assetDirectoryPath = m_AssetsRoot.empty()
			? (Project::GetProjectDirectory() / Project::GetAssetDirectory())
			: m_AssetsRoot;
		const std::filesystem::path assetRegistryPath = assetDirectoryPath / "AssetRegistry.kbrar";
		if (!std::filesystem::exists(assetRegistryPath))
		{
			Log::CoreInfo("No asset registry found; it will be created as assets are imported.");
			return false;
		}

		YAML::Node data;
		try
		{
			data = YAML::LoadFile(assetRegistryPath.string());
		}
		catch (const YAML::Exception& e)
		{
			Log::CoreError("Failed to load asset registry: {0}", e.what());
			KBRAssert(false, "Failed to load asset registry");
			return false;
		}

		YAML::Node registryNode;
		try
		{
			registryNode = data["AssetRegistry"];
		}
		catch (const YAML::BadSubscript& e)
		{
			Log::CoreWarn("Registry was empty: {0}", e.what());
			return false;
		}

		if (!registryNode)
		{
			Log::CoreError("Invalid asset registry file: {0}", assetRegistryPath.string());
			return false;
		}

		for (const auto& assetNode : registryNode)
		{
			if (!assetNode["Handle"] || !assetNode["Type"] || !assetNode["Path"])
			{
				Log::CoreError("Invalid asset entry in registry: {0}", assetRegistryPath.string());
				KBRAssert(false, "Invalid asset entry in registry");
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

		Log::CoreInfo("Asset registry loaded from {0}", assetRegistryPath.string());

		return true;
	}
}