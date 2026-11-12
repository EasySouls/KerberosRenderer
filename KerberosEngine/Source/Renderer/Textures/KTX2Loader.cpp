#include "kbrpch.hpp"
#include "KTX2Loader.hpp"
#include "KTX2FormatSelector.hpp"

#include <ktx.h>

namespace Kerberos::KTX2Loader
{
	bool IsKTX2(const std::filesystem::path& filepath)
	{
		std::string extension = filepath.extension().string();
		std::ranges::transform(extension, extension.begin(), [](const unsigned char c) {
			return static_cast<char>(std::tolower(c));
		});
		return extension == ".ktx2";
	}

	bool Load(const std::filesystem::path& filepath, ktxTexture2** texture)
	{
		KBR_TRACY_FUNCTION();

		if (!texture || !IsKTX2(filepath))
			return false;

		*texture = nullptr;

		const ktxResult result =
            ktxTexture2_CreateFromNamedFile(filepath.string().c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, texture);

		return result == KTX_SUCCESS;
	}

	vk::Format ResolveVulkanFormat(ktxTexture2* texture)
	{
		if (!texture)
			return vk::Format::eUndefined;

		vk::Format format = static_cast<vk::Format>(texture->vkFormat);

		// KTX2 stores the transfer function in the DFD. Tools that convert from
		// PNG sometimes leave the vkFormat as UNORM while declaring sRGB in the
		// DFD, so reinterpret the view format when an sRGB variant exists.
		const khr_df_transfer_e transfer = ktxTexture2_GetOETF_e(texture);
		if (transfer == KHR_DF_TRANSFER_SRGB)
			format = KTX2FormatSelector::ToSrgb(format);

		return format;
	}
}
