#include "kbrpch.hpp"
#include "AssetImporter.hpp"

#include "CubemapImporter.hpp"
#include "TextureImporter.hpp"
#include "MeshImporter.hpp"
#include "SoundImporter.hpp"
#include "Assets/Asset.hpp"
#include "Renderer/Texture.h"

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
				break;
			case AssetType::Mesh:
			{
				MeshImporter meshImporter;
				return meshImporter.ImportMesh(metadata.Filepath);
			}
			case AssetType::Scene:
				break;
			case AssetType::Sound:
				return SoundImporter::ImportSound(handle, metadata);
		}

		KBR_CORE_ASSERT(false, "Unsupported asset type by AssetImporter!");
		return nullptr;
	}

	std::future<Ref<Asset>> AssetImporter::ImportAssetAsync(AssetHandle handle, const AssetMetadata& metadata) 
	{
		return std::async(std::launch::async, ImportAsset, handle, metadata);
	}
}
