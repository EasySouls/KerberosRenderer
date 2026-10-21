#pragma once

#include "Assets/Asset.hpp"
#include "Assets/AssetRegistry.hpp"
#include "Core/Core.hpp"

#include <map>
#include <span>

namespace Kerberos
{
	using AssetMap = std::map<AssetHandle, Ref<Asset>>;

	class AssetManagerBase
	{
	public:
		virtual ~AssetManagerBase() = default;
		
		/*AssetManagerBase(const AssetManagerBase& other) = delete;
		AssetManagerBase(AssetManagerBase&& other) noexcept = delete;
		AssetManagerBase& operator=(const AssetManagerBase& other) = delete;
		AssetManagerBase& operator=(AssetManagerBase&& other) noexcept = delete;*/

		virtual Ref<Asset> GetAsset(AssetHandle handle) = 0;

		virtual bool IsAssetHandleValid(AssetHandle handle) const = 0;
		virtual bool IsAssetLoaded(AssetHandle handle) const = 0;

		virtual AssetType GetAssetType(AssetHandle handle) const = 0;

		virtual const AssetRegistry& GetAssetRegistry() const = 0;

		/**
		 * Returns every registered asset of the given type.
		 *
		 * Backed by AssetRegistry's dense per-type index, so this does not scan the
		 * registry or touch AssetRegistry.kbrar. The returned view is valid until the
		 * registry is mutated.
		 */
		virtual std::span<const AssetHandle> GetAssetsOfType(AssetType type) const = 0;
	};
}
