#pragma once

#include <string>
#include <filesystem>
#include <algorithm>

namespace Kerberos
{
	inline bool IsKTXFormat(const std::filesystem::path& filepath)
	{
		std::string extension = filepath.extension().string();
		std::ranges::transform(extension, extension.begin(), ::tolower);
		return extension == ".ktx" || extension == ".ktx2";
	}

	inline bool NeedsConvertingToKTX2(const std::filesystem::path& filepath)
	{
		std::string extension = filepath.extension().string();
		std::ranges::transform(extension, extension.begin(), ::tolower);
		return extension == ".ktx";
	}
}