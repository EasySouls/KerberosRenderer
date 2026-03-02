#pragma once

#include "Asset.hpp"
#include "Core/Core.hpp"
#include "Renderer/Mesh.hpp"
#include "Renderer/Texture.hpp"
#include "Project/Project.hpp"

#include <type_traits>
#include <memory>

namespace Kerberos
{
	class Mesh;
	class Texture2D;

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

		static Ref<Texture2D> GetDefaultTexture2D();
		static Ref<Mesh> GetDefaultCubeMesh();
	};
}
