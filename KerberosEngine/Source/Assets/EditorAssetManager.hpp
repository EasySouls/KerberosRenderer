#pragma once

#include "Core/Core.hpp"
#include "Renderer/Font.hpp"
#include "AssetRegistry.hpp"
#include "AssetManagerBase.hpp"
#include "Pipeline/AssetMetaService.hpp"
#include "Pipeline/AssetBuildCoordinator.hpp"
#include "Importers/ImporterRegistry.hpp"
#include "Pipeline/AssetSourceScanner.hpp"
#include "Pipeline/AssetFileWatchService.hpp"
#include "Renderer/Mesh.hpp"

#include <filesystem>
#include <atomic>

namespace Kerberos
{
	class EditorAssetManager final : public AssetManagerBase
	{
	public:
        explicit EditorAssetManager(const std::filesystem::path& assetsRoot = {}, const std::filesystem::path& cacheRoot = {});
		~EditorAssetManager() override;

		Ref<Asset> GetAsset(AssetHandle handle) override;

		bool IsAssetHandleValid(AssetHandle handle) const override;
		bool IsAssetLoaded(AssetHandle handle) const override;

		AssetType GetAssetType(AssetHandle handle) const override;

		/**
		 * Imports an asset from the given filepath.
		 * If the asset is already in the registry, it will return the existing handle,
		 * if not, it will create a new asset, assign it a handle, and add it to the registry.
		 *
		 * @param filepath The path to the asset file.
		 * @return The handle of the imported asset.
		 */
		AssetHandle ImportAsset(const std::filesystem::path& filepath);

		const AssetMetadata& GetMetadata(AssetHandle handle) const;

		/**
		 * Loads the persistent import metadata (.meta) for a source file.
		 * @return The meta, or nullopt if it is missing or invalid.
		 */
		std::optional<AssetMetaFile> GetMetaFile(const std::filesystem::path& sourcePath) const;

		/**
		 * Persists the texture sRGB import options for a source and re-imports it.
		 * @return The source's root handle on success, or AssetHandle::Invalid().
		 */
		AssetHandle SetTextureSrgbOptions(const std::filesystem::path& sourcePath,
			const std::map<std::string, bool>& textureSrgb);

		/**
		 * Force-rebuilds a single source and evicts cached loaded artifacts so
		 * consumers reload the regenerated outputs.
		 * @return The source's root handle on success, or AssetHandle::Invalid().
		 */
		AssetHandle ReimportAsset(const std::filesystem::path& sourcePath);

		Ref<Mesh> GetDefaultCubeMesh() const;
		Ref<Texture2D> GetDefaultColorTexture() const;
		Ref<Font> GetDefaultFont() const;

		void SerializeAssetRegistry();
		bool DeserializeAssetRegistry();

		void ConfigurePipeline(const std::filesystem::path& assetsRoot, const std::filesystem::path& cacheRoot = {});

		void StartWatching();
		void EnsureAssetMetas() const;
		std::vector<AssetBuildReport> BuildAssets(bool force = false) const;

		ImporterRegistry& GetImporterRegistry() { return m_ImporterRegistry; }
		const AssetRegistry& GetAssetRegistry() const { return m_AssetRegistry; }
		AssetBuildCoordinator* GetBuildCoordinator() const { return m_BuildCoordinator.get(); }
		const std::filesystem::path& GetAssetsRoot() const { return m_AssetsRoot; }
		const std::filesystem::path& GetCacheRoot() const { return m_CacheRoot; }

	private:
		void HandleAssetFileEvent(const AssetFileEvent& event);

		AssetMap m_LoadedAssets;
		AssetRegistry m_AssetRegistry;

		Ref<Mesh> m_DefaultCubeMesh;
		Ref<Texture2D> m_DefaultColorTexture;
		Ref<Font> m_DefaultFont;

		std::filesystem::path m_AssetsRoot;
		std::filesystem::path m_CacheRoot;
		Owner<AssetMetaService> m_MetaService;
		ImporterRegistry m_ImporterRegistry;
		Owner<AssetBuildCoordinator> m_BuildCoordinator;
		AssetFileWatchService m_FileWatch;
		Ref<std::atomic_bool> m_Lifetime = CreateRef<std::atomic_bool>(true);
	};
}