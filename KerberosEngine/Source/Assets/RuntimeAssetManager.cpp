#include "kbrpch.hpp"
#include "RuntimeAssetManager.hpp"

#include <stdexcept>

namespace Kerberos
{
	Ref<Asset> RuntimeAssetManager::GetAsset(AssetHandle handle)
	{
		throw std::runtime_error("RuntimeAssetManager::GetAsset is not implemented yet!");
	}

	bool RuntimeAssetManager::IsAssetHandleValid(AssetHandle handle) const
	{
		throw std::runtime_error("RuntimeAssetManager::IsAssetHandleValid is not implemented yet!");
	}

	bool RuntimeAssetManager::IsAssetLoaded(AssetHandle handle) const
	{
		throw std::runtime_error("RuntimeAssetManager::IsAssetLoaded is not implemented yet!");
	}

	AssetType RuntimeAssetManager::GetAssetType(AssetHandle handle) const
	{
		throw std::runtime_error("RuntimeAssetManager::GetAssetyType is not implemented yet!");
	}
}
