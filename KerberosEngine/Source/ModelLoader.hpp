#pragma once

#include "Mesh.hpp"

#include <filesystem>

namespace Kerberos
{
	enum GLTFLoadingFlags : uint8_t
	{
		None = 0,
		FlipY = 1 << 0,
		GenerateNormals = 1 << 1,
		GenerateTangents = 1 << 2,
	};

	class ModelLoader
	{
	public:
		static Mesh LoadModel(const std::filesystem::path& path, GLTFLoadingFlags flags = GLTFLoadingFlags::None);
	};
}