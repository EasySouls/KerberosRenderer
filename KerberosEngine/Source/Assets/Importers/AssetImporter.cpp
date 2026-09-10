#include "AssetImporter.hpp"

#include "CubemapImporter.hpp"
#include "TextureImporter.hpp"
#include "AssimpModelImporter.hpp"
#include "GLTFModelImporter.hpp"
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
			case AssetType::Model:
			{
				// TODO: This is the deprecated way of loading a mesh, the engine-specific format should be used
				const auto extension = metadata.Filepath.extension().string();
				if (extension == ".gltf" || extension == ".glb")
					return GLTFModelImporter::ImportModel(handle, metadata);
				return AssimpModelImporter::ImportModel(metadata.Filepath);
			}
			case AssetType::Scene:
				break;
			case AssetType::Sound:
				return SoundImporter::ImportSound(handle, metadata);
			case AssetType::Mesh:
				return MeshImporter::ImportMesh(handle, metadata);
				break;
			case AssetType::Prefab:
				return PrefabImporter::ImportPrefab(handle, metadata);
				break;
			case AssetType::Animation:
				// TODO: Implement AnimationImporter
				Log::CoreWarn("Animation import not yet implemented");
				break;
            case AssetType::Skin:
                break;
        }

		KBRAssert(false, "Unsupported asset type by AssetImporter!");
		return nullptr;
	}


	std::future<Ref<Asset>> AssetImporter::ImportAssetAsync(AssetHandle handle, const AssetMetadata& metadata) 
	{
		KBR_TRACY_FUNCTION();

		return std::async(std::launch::async, ImportAsset, handle, metadata);
	}
}