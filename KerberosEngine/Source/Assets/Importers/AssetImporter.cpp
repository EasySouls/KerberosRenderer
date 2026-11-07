#include "AssetImporter.hpp"

#include "CubemapImporter.hpp"
#include "TextureImporter.hpp"
#include "AssimpModelImporter.hpp"
#include "MaterialImporter.hpp"
#include "MeshImporter.hpp"
#include "PrefabImporter.hpp"
#include "SoundImporter.hpp"
#include "Assets/Asset.hpp"
#include "Profiling/Profilers.hpp"

import Kerberos;

namespace Kerberos
{
	void AssetImporter::Init()
	{
		// Initialize threadpool if needed
	}

	Ref<Asset> AssetImporter::ImportAsset(const AssetHandle handle, const AssetMetadata& metadata)
	{
		KBR_TRACY_FUNCTION();

		switch (metadata.Type)
		{
			case AssetType::Texture2D:
				return TextureImporter::ImportTexture(handle, metadata);
			case AssetType::TextureCube:
				return CubemapImporter::ImportCubemap(handle, metadata);
			case AssetType::Material:
				return MaterialImporter::ImportMaterial(handle, metadata);
			case AssetType::Scene:
				break;
			case AssetType::Sound:
				return SoundImporter::ImportSound(handle, metadata);
			case AssetType::Mesh:
				return MeshImporter::ImportMesh(handle, metadata);
			case AssetType::Prefab:
				return PrefabImporter::ImportPrefab(handle, metadata);
			case AssetType::Animation:
				// TODO: Implement AnimationImporter
				Log::CoreWarn("Animation import not yet implemented");
				break;
            case AssetType::Skeleton:
                // TODO: Implement SkeletonImporter
                Log::CoreWarn("Skeleton import not yet implemented");
                break;
            case AssetType::SkinnedMesh:
                // TODO: Implement SkinnedMeshImporter
                Log::CoreWarn("SkinnedMesh import not yet implemented");
                break;
            default:
                break;
        }

		KBRAssert("Unsupported asset type by AssetImporter!");
		return nullptr;
	}


	std::future<Ref<Asset>> AssetImporter::ImportAssetAsync(AssetHandle handle, const AssetMetadata& metadata) 
	{
		KBR_TRACY_FUNCTION();

		return std::async(std::launch::async, ImportAsset, handle, metadata);
	}
}