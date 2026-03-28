#include "kbrpch.hpp"
#include "AssetImporter.hpp"

#include "CubemapImporter.hpp"
#include "TextureImporter.hpp"
#include "AssimpModelImporter.hpp"
#include "GLTFModelImporter.hpp"
#include "MaterialImporter.hpp"
#include "SoundImporter.hpp"
#include "Assets/Asset.hpp"

namespace Kerberos
{
	void AssetImporter::Init()
	{
		// Initialize threadpool if needed
	}

	Ref<Asset> AssetImporter::ImportAsset(const AssetHandle handle, const AssetMetadata& metadata)
	{
		switch (metadata.Type)
		{
			case AssetType::Texture2D:
				return TextureImporter::ImportTexture(handle, metadata);
			case AssetType::TextureCube:
				return CubemapImporter::ImportCubemap(handle, metadata);
			case AssetType::Material:
				return MaterialImporter::ImportMaterial(handle, metadata);
				break;
			case AssetType::Model:
			{
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
				KBR_CORE_ASSERT(false, "Mesh should not be imported directly, it should be imported as part of a model!");
				break;
		}

		KBR_CORE_ASSERT(false, "Unsupported asset type by AssetImporter!");
		return nullptr;
	}

	std::future<Ref<Asset>> AssetImporter::ImportAssetAsync(AssetHandle handle, const AssetMetadata& metadata) 
	{
		return std::async(std::launch::async, ImportAsset, handle, metadata);
	}
}
