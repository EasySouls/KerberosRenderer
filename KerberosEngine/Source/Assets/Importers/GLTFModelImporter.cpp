#include "kbrpch.hpp"
#include "GLTFModelImporter.hpp"

#include "ModelLoader.hpp"

namespace Kerberos
{
	Ref<Mesh> GLTFModelImporter::ImportModel(AssetHandle handle, const AssetMetadata& metadata)
	{
		return ImportModel(metadata.Filepath);
	}

	Ref<Mesh> GLTFModelImporter::ImportModel(const std::filesystem::path& filepath)
	{
		/*KBR_CORE_ERROR("GLTF model importing not implemented yet! File: {}", filepath.string());
		return nullptr;*/

		// TODO: Implement real model loading with submeshes
		KBR_CORE_WARN("Mesh loading is not complete, only the first submesh will be loaded!");

		return CreateRef<Mesh>(ModelLoader::LoadModel(filepath));
	}
}
