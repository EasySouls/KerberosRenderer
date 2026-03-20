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
		// TODO: Package a cube.gltf alongside the editor or create a mesh programmatically
		throw std::logic_error("Not implemented");
	}

	Ref<Font> AssetManager::GetDefaultFont() 
	{
		return Project::GetActive()->GetEditorAssetManager()->GetDefaultFont();
	}
}
