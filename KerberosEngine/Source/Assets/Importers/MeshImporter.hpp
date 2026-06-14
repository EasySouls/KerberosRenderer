#pragma once

#include "Core/Core.hpp"
#include "Assets/AssetMetadata.hpp"
#include "Renderer/Mesh.hpp"

namespace Kerberos
{
	class MeshImporter
	{
	public:
		static Ref<Mesh> ImportMesh(AssetHandle handle, const AssetMetadata& metadata);
		static Ref<Mesh> ImportMesh(const std::filesystem::path& filepath);
	};
}
