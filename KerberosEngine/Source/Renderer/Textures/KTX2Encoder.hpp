#pragma once

#include "Texture.hpp"
#include "Core/Buffer.hpp"

#include <filesystem>

namespace Kerberos::KTX2Encoder {

[[nodiscard]] bool Encode(const std::filesystem::path& outputPath,
	const TextureSpecification& specification, const Buffer& pixels);

[[nodiscard]] bool EncodeIfNeeded(const std::filesystem::path& sourcePath,
	const std::filesystem::path& outputPath,
	const TextureSpecification& specification, const Buffer& pixels);

}
