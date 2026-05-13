#include "kbrpch.hpp"
#include "AssetManager.hpp"

namespace Kerberos
{
	Ref<Mesh> AssetManager::ResolveMeshAsset(const AssetHandle handle)
	{
		if (!handle.IsValid() || !IsAssetHandleValid(handle))
			return nullptr;

		switch (GetAssetType(handle))
		{
			case AssetType::Mesh:
				return GetAsset<Mesh>(handle);
			case AssetType::Model:
			{
				const Ref<Model> model = GetAsset<Model>(handle);
				if (!model)
					return nullptr;

				const Ref<Mesh> mesh = model->GetPrimaryMesh();
				if (mesh && !mesh->GetHandle().IsValid())
					mesh->GetHandle() = handle;

				return mesh;
			}
			default:
				return nullptr;
		}
	}

	Ref<Texture2D> AssetManager::GetDefaultTexture2D()
	{
		return Project::GetActive()->GetEditorAssetManager()->GetDefaultColorTexture();
	}

	Ref<Mesh> AssetManager::GetDefaultCubeMesh() 
	{
		return Project::GetActive()->GetEditorAssetManager()->GetDefaultCubeMesh();
	}

	Ref<Font> AssetManager::GetDefaultFont() 
	{
		return Project::GetActive()->GetEditorAssetManager()->GetDefaultFont();
	}
}
