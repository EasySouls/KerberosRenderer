#pragma once

#include "Core/Core.hpp"
#include "AssetRegistry.hpp"
#include "AssetManagerBase.hpp"

namespace Kerberos
{

	class EditorAssetManager final : public AssetManagerBase
	{
	public:
		EditorAssetManager();

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

		void SerializeAssetRegistry();
		bool DeserializeAssetRegistry();

		const AssetRegistry& GetAssetRegistry() const { return m_AssetRegistry; }

	private:
		AssetMap m_LoadedAssets;
		AssetRegistry m_AssetRegistry;
	};
}