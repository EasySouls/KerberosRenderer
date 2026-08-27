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

		Ref<Mesh> GetDefaultCubeMesh() const;
		Ref<Texture2D> GetDefaultColorTexture() const;
		Ref<Font> GetDefaultFont() const;

		void SerializeAssetRegistry();
		bool DeserializeAssetRegistry();

		const AssetRegistry& GetAssetRegistry() const { return m_AssetRegistry; }
		ImporterRegistry& GetImporterRegistry() { return m_ImporterRegistry; }
		void ConfigurePipeline(const std::filesystem::path& assetsRoot, const std::filesystem::path& cacheRoot = {});
		void EnsureAssetMetas() const;
		std::vector<AssetBuildReport> BuildAssets(bool force = false) const;
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
		std::unique_ptr<AssetMetaService> m_MetaService;
		ImporterRegistry m_ImporterRegistry;
		std::unique_ptr<AssetBuildCoordinator> m_BuildCoordinator;
		AssetFileWatchService m_FileWatch;
		std::shared_ptr<std::atomic_bool> m_Lifetime = std::make_shared<std::atomic_bool>(true);
	};
}