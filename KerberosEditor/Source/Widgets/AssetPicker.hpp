#pragma once

#include "Assets/Asset.hpp"
#include "Assets/AssetManager.hpp"

#include <imgui/imgui.h>

#include <type_traits>

namespace Kerberos
{
	/**
	 * Searchable, drag/drop-enabled asset picker used by editor property fields.
	 *
	 * The picker lists only assets of the requested AssetType, supports filtering
	 * by path/sub-asset key, and accepts the matching asset-browser drag payload.
	 */
	class AssetPicker
	{
	public:
		/// Draws a picker for an AssetHandle. Returns true when the handle changed.
		static bool DrawAssetHandle(const char* label, AssetType type, AssetHandle& handle,
			const char* hint = nullptr, bool allowNone = true);

		/// Draws a picker containing assets of every type. Returns true when the
		/// handle changed.
		static bool DrawAnyAssetHandle(const char* label, AssetHandle& handle,
			const char* hint = nullptr, bool allowNone = true);

		/// Draws a picker for a typed Ref and resolves the selected handle.
		template <typename T>
			requires std::is_base_of_v<Asset, T>
		static bool DrawAsset(const char* label, AssetType type, Ref<T>& asset,
			const char* hint = nullptr, bool allowNone = true)
		{
			AssetHandle handle = asset ? asset->GetHandle() : AssetHandle::Invalid();
			if (!DrawAssetHandle(label, type, handle, hint, allowNone))
				return false;

			asset = handle.IsValid() ? AssetManager::AsAsset<T>(handle) : nullptr;
			return true;
		}

		/// Clears the cached picker lists. Called when the asset browser refreshes.
		static void InvalidateCache();

		/// Returns the asset-browser drag payload type for an asset type.
		static const char* DragPayloadForType(AssetType type);

		/// Accepts a matching asset drag payload. Returns true when a handle was
		/// supplied.
		static bool AcceptDragDrop(AssetType type, AssetHandle& handle);
		static bool AcceptAnyDragDrop(AssetHandle& handle);
	};
}
