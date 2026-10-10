#pragma once

#include "Asset.hpp"
#include "Core/Core.hpp"
#include "Renderer/Mesh.hpp"
#include "Renderer/Font.hpp"
#include "Renderer/Textures/Texture2D.hpp"
#include "Project/Project.hpp"

#include <type_traits>
#include <memory>

namespace Kerberos {

template <typename T>
concept IsAssetSubclass = std::is_base_of_v<Asset, T> && requires {
    { T::Type } -> std::convertible_to<AssetType>;
};

class AssetManager
{
public:
    /**
     * Returns a reference to the asset of type T corresponding to the given handle.
     * Only safe to call if the asset is known to be of type T, otherwise using the returned pointer is UB.
	 * @tparam T The type of the asset to retrieve.
	 * @param handle The handle of the asset to retrieve.
	 * @return A reference to the asset.
	 */
	template<typename T>
		requires std::is_base_of_v<Asset, T>
	static Ref<T> GetAsset(const AssetHandle handle)
	{
		const Ref<Asset> asset = Project::GetActive()->GetAssetManager()->GetAsset(handle);
		return std::static_pointer_cast<T>(asset);
	}

	/**
     * Returns a reference to the asset of type T corresponding to the given handle, if it is of that type.
     * If the asset is not of type T, returns nullptr.
     * @tparam T The type of the asset to retrieve.
     * @param handle The handle of the asset to retrieve.
     * @return A reference to the asset, or nullptr if it is not of type T.
     */
	template <IsAssetSubclass T>
    static Ref<T> AsAsset(const AssetHandle handle)
	{
		// TODO: Maybe add an IsAssetHandleValid() call as well
        if (!handle.IsValid() || GetAssetType(handle) != T::Type) {
            return nullptr;
        }

        return GetAsset<T>(handle);
	}

	static AssetType GetAssetType(const AssetHandle handle)
	{
		return Project::GetActive()->GetAssetManager()->GetAssetType(handle);
	}

	static bool IsAssetHandleValid(const AssetHandle handle)
	{
		return Project::GetActive()->GetAssetManager()->IsAssetHandleValid(handle);
	}

	static Ref<Mesh> ResolveMeshAsset(AssetHandle handle);

	static Ref<Texture2D> GetDefaultTexture2D();
	static Ref<Mesh> GetDefaultCubeMesh();
	static Ref<Font> GetDefaultFont();
};

}
