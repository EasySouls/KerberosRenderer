#pragma once

#include "Core/Core.hpp"
#include "AssetManagerBase.hpp"
#include "AssetRegistry.hpp"
#include "Runtime/RuntimeAssetResolver.hpp"

#include <filesystem>

namespace Kerberos {

class RuntimeAssetManager final : public AssetManagerBase
{
public:
	RuntimeAssetManager() = default;
	explicit RuntimeAssetManager(const AssetRegistry& registry,
		std::filesystem::path assetRoot = {}, std::filesystem::path libraryRoot = {});

	void Configure(const AssetRegistry& registry,
		std::filesystem::path assetRoot = {}, std::filesystem::path libraryRoot = {});
	Ref<Asset> GetAsset(AssetHandle handle) override;

	bool IsAssetHandleValid(AssetHandle handle) const override;
	bool IsAssetLoaded(AssetHandle handle) const override;

	AssetType GetAssetType(AssetHandle handle) const override;

	template<typename T>
	Ref<T> GetAsset(const AssetHandle handle)
	{
		const auto asset = GetAsset(handle);
		return asset ? std::dynamic_pointer_cast<T>(asset) : nullptr;
	}

	const AssetRegistry* GetAssetRegistry() const { return m_Registry; }

private:
	const AssetRegistry* m_Registry = nullptr;
	RuntimeAssetResolver m_Resolver;
	AssetMap m_LoadedAssets;
};

}
