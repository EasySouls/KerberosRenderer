#pragma once

#include "Assets/Asset.hpp"
#include "Assets/AssetMetadata.hpp"
#include <future>


namespace Kerberos
{
	class Asset;
	struct AssetMetadata;

	class AssetImporter
	{
	public:
		static void Init();

		static Ref<Asset> ImportAsset(AssetHandle handle, const AssetMetadata& metadata);

		static std::future<Ref<Asset>> ImportAssetAsync(AssetHandle handle, const AssetMetadata& metadata);
		
	};
}
