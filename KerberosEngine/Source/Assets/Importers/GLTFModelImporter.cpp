#include "kbrpch.hpp"
#include "GLTFModelImporter.hpp"

namespace Kerberos
{
	Ref<Mesh> GLTFModelImporter::ImportModel(AssetHandle handle, const AssetMetadata& metadata)
	{
		return ImportModel(metadata.Filepath);
	}

	Ref<Mesh> GLTFModelImporter::ImportModel(const std::filesystem::path& filepath)
	{
		KBR_CORE_ERROR("GLTF model importing not implemented yet! File: {}", filepath.string());
		return nullptr;
	}
}