#pragma once

#include "Asset.hpp"
#include "Model.hpp"
#include "Core/Core.hpp"
#include "Renderer/Mesh.hpp"
#include "Renderer/Font.hpp"
#include "Renderer/Textures/Texture2D.hpp"
#include "Project/Project.hpp"

#include <type_traits>
#include <memory>

namespace Kerberos {

class AssetManager
{
public:
	template<typename T>
		requires std::is_base_of_v<Asset, T>
	static Ref<T> GetAsset(const AssetHandle handle)
	{
		const Ref<Asset> asset = Project::GetActive()->GetAssetManager()->GetAsset(handle);
		return std::static_pointer_cast<T>(asset);
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
