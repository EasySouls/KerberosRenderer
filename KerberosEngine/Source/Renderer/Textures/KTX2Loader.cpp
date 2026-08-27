#include "kbrpch.hpp"
#include "KTX2Loader.hpp"

#include <ktx.h>

namespace Kerberos::KTX2Loader
{
	bool IsKTX2(const std::filesystem::path& filepath)
	{
		std::string extension = filepath.extension().string();
		std::ranges::transform(extension, extension.begin(), [](unsigned char c) {
			return static_cast<char>(std::tolower(c));
		});
		return extension == ".ktx2";
	}

	bool Load(const std::filesystem::path& filepath, ktxTexture2** texture)
	{
		if (!texture || !IsKTX2(filepath))
			return false;
		*texture = nullptr;
		return ktxTexture2_CreateFromNamedFile(filepath.string().c_str(),
			KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, texture) == KTX_SUCCESS;
	}
}
