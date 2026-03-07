#pragma once

#include "Renderer/Mesh.hpp"
#include "Renderer/Textures/Texture.hpp"
#include "Assets/Asset.hpp"
#include "Core/Core.hpp"

#include <map>

#include "Asset.hpp"

namespace Kerberos
{
	using AssetMap = std::map<AssetHandle, Ref<Asset>>;

	class AssetManagerBase
	{
	public:
		virtual ~AssetManagerBase() = default;
		
		AssetManagerBase(const AssetManagerBase& other) = delete;
		AssetManagerBase(AssetManagerBase&& other) noexcept = delete;
		AssetManagerBase& operator=(const AssetManagerBase& other) = delete;
		AssetManagerBase& operator=(AssetManagerBase&& other) noexcept = delete;

		virtual Ref<Asset> GetAsset(AssetHandle handle) = 0;

		virtual bool IsAssetHandleValid(AssetHandle handle) const = 0;
		virtual bool IsAssetLoaded(AssetHandle handle) const = 0;

		virtual AssetType GetAssetType(AssetHandle handle) const = 0;
	};
}
