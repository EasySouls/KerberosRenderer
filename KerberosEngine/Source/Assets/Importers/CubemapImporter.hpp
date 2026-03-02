#pragma once

#include "Assets/AssetMetadata.hpp"
#include "Renderer/TextureCube.hpp"

#include <filesystem>

namespace Kerberos
{
	class TextureCube;

	struct CubemapDescriptor
	{
		std::string Name;

		std::filesystem::path RightPath;
		std::filesystem::path LeftPath;
		std::filesystem::path TopPath;
		std::filesystem::path BottomPath;
		std::filesystem::path FrontPath;
		std::filesystem::path BackPath;

		bool IsSRGB;
	};

	class CubemapImporter
	{
	public:
		static Ref<TextureCube> ImportCubemap(AssetHandle handle, const AssetMetadata& metadata);
		static Ref<TextureCube> ImportCubemap(const std::filesystem::path& filepath);
	};
}