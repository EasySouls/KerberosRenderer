#include "kbrpch.hpp"
#include "KTX2Encoder.hpp"
#include "KTX2FormatSelector.hpp"

#include <ktx.h>

namespace Kerberos::KTX2Encoder {

bool Encode(const std::filesystem::path& outputPath,
	const TextureSpecification& specification, const Buffer& pixels)
{
	const vk::Format format = KTX2FormatSelector::Select(specification.Format);
	if (!KTX2FormatSelector::IsSupported(format) || !pixels.Data || pixels.Size == 0)
		return false;

	ktxTextureCreateInfo createInfo{};
	createInfo.vkFormat = static_cast<ktx_uint32_t>(format);
	createInfo.baseWidth = specification.Width;
	createInfo.baseHeight = specification.Height;
	createInfo.baseDepth = 1;
	createInfo.numDimensions = 2;
	createInfo.numLevels = 1;
	createInfo.numLayers = 1;
	createInfo.numFaces = 1;
	createInfo.generateMipmaps = KTX_FALSE;

	ktxTexture2* texture = nullptr;
	if (ktxTexture2_Create(&createInfo, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &texture) != KTX_SUCCESS)
		return false;

	const ktx_error_code_e setResult = ktxTexture_SetImageFromMemory(
		reinterpret_cast<ktxTexture*>(texture), 0, 0, 0, pixels.Data,
		static_cast<ktx_size_t>(pixels.Size));
	const ktx_error_code_e writeResult = setResult == KTX_SUCCESS
		? ktxTexture2_WriteToNamedFile(texture, outputPath.string().c_str())
		: setResult;
	ktxTexture2_Destroy(texture);
	return writeResult == KTX_SUCCESS;
}

bool EncodeIfNeeded(const std::filesystem::path& sourcePath,
	const std::filesystem::path& outputPath,
	const TextureSpecification& specification, const Buffer& pixels)
{
	std::error_code error;
	if (std::filesystem::exists(outputPath, error) &&
		std::filesystem::last_write_time(outputPath, error) >= std::filesystem::last_write_time(sourcePath, error))
		return true;
	return Encode(outputPath, specification, pixels);
}

}
