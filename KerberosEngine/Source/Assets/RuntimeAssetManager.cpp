#include "kbrpch.hpp"
#include "RuntimeAssetManager.hpp"

#include "InvalidAssetHandleException.hpp"
#include "Runtime/RuntimeAssetLoader.hpp"

#include <stdexcept>
#include <utility>

namespace Kerberos {

RuntimeAssetManager::RuntimeAssetManager(const AssetRegistry& registry,
	std::filesystem::path assetRoot, std::filesystem::path libraryRoot,
	std::filesystem::path engineLibraryRoot)
{
	KBR_TRACY_FUNCTION();

	Configure(registry, std::move(assetRoot), std::move(libraryRoot), std::move(engineLibraryRoot));
}

void RuntimeAssetManager::Configure(const AssetRegistry& registry,
	std::filesystem::path assetRoot, std::filesystem::path libraryRoot,
	std::filesystem::path engineLibraryRoot)
{
	m_Registry = registry;
	m_Configured = true;
	m_LibraryRoot = std::move(libraryRoot);
	m_EngineLibraryRoot = std::move(engineLibraryRoot);
	m_Resolver.Configure(&m_Registry, std::move(assetRoot), m_LibraryRoot, m_EngineLibraryRoot);
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
	auto asset = RuntimeAssetLoader::Load(handle, location->Metadata, location->Path, &m_Registry, m_LibraryRoot);
	if (!asset)
		throw std::runtime_error("Runtime asset artifact failed to load");
	m_LoadedAssets.emplace(handle, asset);
	return asset;
}

bool RuntimeAssetManager::IsAssetHandleValid(const AssetHandle handle) const
{
	if (!m_Configured || !handle.IsValid() || !m_Registry.Contains(handle))
		return false;

	const auto& metadata = m_Registry.Get(handle);
	if (metadata.LibraryPath.empty())
		return false;

	const auto location = m_Resolver.Resolve(handle);
	return location.has_value() && std::filesystem::exists(location->Path);
}

bool RuntimeAssetManager::IsAssetLoaded(const AssetHandle handle) const
{
	return handle.IsValid() && m_LoadedAssets.contains(handle);
}

AssetType RuntimeAssetManager::GetAssetType(const AssetHandle handle) const
{
	if (!IsAssetHandleValid(handle))
	{
        throw InvalidAssetHandleException(handle);
	}

	return m_Registry.Get(handle).Type;
}

}
