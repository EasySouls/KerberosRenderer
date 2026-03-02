#pragma once

#include "Asset.hpp"

#include <filesystem>

namespace Kerberos
{
	struct AssetMetadata
	{
		AssetType Type;
		std::filesystem::path Filepath;
	};
}