#pragma once

#include "Assets/Asset.hpp"

namespace Kerberos
{
	struct AssetDragPayload
	{
		AssetHandle Handle = AssetHandle::Invalid();
		AssetHandle RootHandle = AssetHandle::Invalid();
		int32_t PrimitiveIndex = -1;
	};

	inline AssetDragPayload ReadAssetDragPayload(const void* data, const size_t size)
	{
		if (size == sizeof(AssetDragPayload))
			return *static_cast<const AssetDragPayload*>(data);
		if (size == sizeof(AssetHandle))
			return { .Handle = *static_cast<const AssetHandle*>(data) };
		return {};
	}

	constexpr const char* assetBrowserItem = "ASSET_BROWSER_ITEM";
	constexpr const char* assetBrowserTexture = "ASSET_BROWSER_TEXTURE";
	constexpr const char* assetBrowserTextureCube = "ASSET_BROWSER_TEXTURE_CUBE";
	constexpr const char* assetBrowserMesh = "ASSET_BROWSER_MESH";
	constexpr const char* assetBrowserScene = "ASSET_BROWSER_SCENE";
	constexpr const char* assetBrowserAudio = "ASSET_BROWSER_AUDIO";
	constexpr const char* assetBrowserFont = "ASSET_BROWSER_FONT";
	constexpr const char* assetBrowserMaterial = "ASSET_BROWSER_MATERIAL";
}