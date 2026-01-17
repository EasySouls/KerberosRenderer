#pragma once

#include "Mesh.hpp"

#include <filesystem>

namespace kbr
{
	class ModelLoader
	{
	public:
		static Mesh LoadModel(const std::filesystem::path& path);
	};
}