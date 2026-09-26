#include "kbrpch.hpp"
#include "RuntimeAssetManager.hpp"
#include "Runtime/RuntimeAssetLoader.hpp"

#include <stdexcept>
#include <utility>

namespace Kerberos {

RuntimeAssetManager::RuntimeAssetManager(const AssetRegistry& registry,
	std::filesystem::path assetRoot, std::filesystem::path libraryRoot)
{
	KBR_TRACY_FUNCTION();

	Configure(registry, std::move(assetRoot), std::move(libraryRoot));
}

void RuntimeAssetManager::Configure(const AssetRegistry& registry,
	std::filesystem::path assetRoot, std::filesystem::path libraryRoot)
{
	m_Registry = registry;
	m_Configured = true;
	m_Resolver.Configure(&m_Registry, std::move(assetRoot), std::move(libraryRoot));
	m_LoadedAssets.clear();
}

Ref<Asset> RuntimeAssetManager::GetAsset(AssetHandle handle)
{
	KBR_TRACY_FUNCTION();

	if (!IsAssetHandleValid(handle))
		return nullptr;
	if (const auto it = m_LoadedAssets.find(handle); it != m_LoadedAssets.end())
		return it->second;
	const auto location = m_Resolver.Resolve(handle);
	if (!location)
		return nullptr;
	auto asset = RuntimeAssetLoader::Load(handle, location->Metadata, location->Path);
	if (asset)
		m_LoadedAssets.emplace(handle, asset);
	return asset;
}

bool RuntimeAssetManager::IsAssetHandleValid(const AssetHandle handle) const
{
	return m_Configured && handle.IsValid() && m_Registry.Contains(handle);
}

bool RuntimeAssetManager::IsAssetLoaded(const AssetHandle handle) const
{
	return handle.IsValid() && m_LoadedAssets.contains(handle);
}

AssetType RuntimeAssetManager::GetAssetType(const AssetHandle handle) const
{
	if (!IsAssetHandleValid(handle))
	{
		throw std::runtime_error("Invalid runtime asset handle");
	}

	return m_Registry.Get(handle).Type;
}

}
