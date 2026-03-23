#include "kbrpch.hpp"
#include "AssetManager.hpp"

namespace Kerberos
{
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
