#include "TextureImporter.hpp"
#include "Core/Core.hpp"
#include "Renderer/Textures/Texture2D.hpp"
#include "Renderer/Textures/KTX2Encoder.hpp"
#include "Renderer/Textures/KTX2FormatSelector.hpp"
#include "Profiling/Profilers.hpp"
#include "Utils/KtxConversion.hpp"
#include "ImportUtils.hpp"

#include <stb_image.h>

#include <array>
#include <cmath>
#include <unordered_set>
#include <algorithm>

import Kerberos;

namespace 
{

	bool IsExtensionSupported(const std::filesystem::path& filepath)
	{
		const static std::unordered_set<std::string> supportedExtensions = {
			".png", ".jpg", ".jpeg", ".bmp", ".tga", ".gif", ".psd", ".hdr", ".pic", ".pnm",
			".ktx", ".ktx2"
		};

		std::string extension = filepath.extension().string();
		std::ranges::transform(extension, extension.begin(), [](unsigned char c) {
			return static_cast<char>(std::tolower(c));
		});
		return supportedExtensions.contains(extension);
	}

	/// Vulkan has no 16-bit UNORM+sRGB format, so 16-bit color sources are
	/// linearized on the CPU and stored as linear UNORM16. A lookup table keeps
	/// this cheap for large textures.
	const std::array<uint16_t, 65536>& GetSrgb16ToLinear16Lut()
	{
		static const std::array<uint16_t, 65536> lookup = []()
		{
			std::array<uint16_t, 65536> table{};
			for (uint32_t i = 0; i < table.size(); ++i)
			{
				const float srgb = static_cast<float>(i) / 65535.0f;
				const float linear = srgb <= 0.04045f
					? srgb / 12.92f
					: std::pow((srgb + 0.055f) / 1.055f, 2.4f);
				table[i] = static_cast<uint16_t>(std::lround(std::clamp(linear, 0.0f, 1.0f) * 65535.0f));
			}
			return table;
		}();
		return lookup;
	}
}

namespace Kerberos
{
	Ref<Texture2D> TextureImporter::ImportTexture(AssetHandle /*handle*/, const AssetMetadata& metadata)
	{
		// TODO: Read an sRGB flag from import settings (.meta) once the editor
		// exposes an import options dialog, for now authored textures default to
		// sRGB color interpretation.
		return ImportTexture(metadata.Filepath, true);
	}

	Ref<Texture2D> TextureImporter::ImportTexture(const std::filesystem::path& filepath, const bool srgb)
	{
		KBR_TRACY_FUNCTION();

		const auto extension = filepath.extension();
		if (!IsExtensionSupported(filepath))
		{
			Log::CoreError("TextureImporter::ImportTexture - unsupported texture file format: {}", extension.string());
            KBRAssert(false, "TextureImporter::ImportTexture - unsupported texture file format: {}", extension.string());
			return nullptr;
		}

		if (IsKTXFormat(filepath))
		{
			if (NeedsConvertingToKTX2(filepath))
			{
				auto ktx2Filepath = filepath;
				ktx2Filepath.replace_extension(".ktx2");

				if (!std::filesystem::exists(ktx2Filepath))
				{
					const auto currentDir = std::filesystem::current_path();
					const Process::ProcessResult result = KtxConversion::ConvertKtxToKtx2(filepath, true);
					KBRAssert(
						result.Succeeded,
						"TextureImporter::ImportTexture - failed to convert KTX to KTX2 for file: {} (started: {}, exit code: {}, error code: {}, error: {})",
						filepath.string(),
						result.Started,
						result.ExitCode,
						result.ErrorCode,
						result.ErrorMessage);
				}

				const Ref<Texture2D> texture = CreateRef<Texture2D>(ktx2Filepath);
				return texture;
			}

			const Ref<Texture2D> texture = CreateRef<Texture2D>(filepath);

			const std::string name = filepath.filename().string();
			//texture->SetDebugName(name);

			return texture;
		}

		auto [spec, buffer] = LoadTextureData(filepath, true, 0, srgb);
		auto ktx2Filepath = filepath;
		ktx2Filepath.replace_extension(".ktx2");
		if (KTX2Encoder::EncodeIfNeeded(filepath, ktx2Filepath, spec, buffer))
			return Texture2D::FromFile(ktx2Filepath);

		auto texture = CreateRef<Texture2D>(spec, buffer);

		const std::string name = filepath.filename().string();
		//texture->SetDebugName(name);

		return texture;
	}

	std::pair<TextureSpecification, Buffer> TextureImporter::LoadTextureData(const std::filesystem::path& filepath, const bool flip, const int desiredChannels, const bool srgb)
	{
		KBR_TRACY_FUNCTION();

		int width = 0, height = 0, channels = 0;

		stbi_set_flip_vertically_on_load(flip);
		Buffer data;

		if (!std::filesystem::exists(filepath))
		{
			Log::CoreError("TextureImporter::ImportTexture - file does not exist: {}", filepath.string());
			KBRAssert(false, "TextureImporter::ImportTexture - file does not exist: {}", filepath.string());
			return std::make_pair(TextureSpecification{}, Buffer{});
		}

		const std::string path = filepath.string();
		const int requestedChannels = desiredChannels > 0 ? std::clamp(desiredChannels, 1, 4) : STBI_rgb_alpha;
		const int outputChannels = desiredChannels > 0 ? std::clamp(desiredChannels, 1, 4) : 4;

		TextureSpecification spec;
		spec.Width = static_cast<uint32_t>(width);
		spec.Height = static_cast<uint32_t>(height);

		// HDR radiance data must not be routed through the 8-bit LDR decoder.
		if (stbi_is_hdr(path.c_str()))
		{
			float* pixels = nullptr;
			{
				KBR_TRACY_SCOPE("TextureImporter::ImportTexture - stbi_loadf");
				pixels = stbi_loadf(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
			}

			if (pixels == nullptr)
			{
				Log::CoreError("TextureImporter::ImportTexture - failed to load HDR texture from filepath: {}", path);
				KBRAssert(false, "TextureImporter::LoadTextureData - stbi_loadf returned null data for filepath: {}", path);
				return std::make_pair(TextureSpecification{}, Buffer{});
			}

			KBRAssert(width > 0 && height > 0, "TextureImporter::ImportTexture - failed to load texture with valid dimensions from filepath: {}", path);

			data.Size = static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * 4ull * sizeof(float);
			data.Allocate(data.Size);
			std::memcpy(data.Data, pixels, data.Size);
			stbi_image_free(pixels);

			spec.Width = static_cast<uint32_t>(width);
			spec.Height = static_cast<uint32_t>(height);
			spec.Format = ImageFormat::RGBA32F;
			return std::make_pair(spec, std::move(data));
		}

		// Preserve 16-bit integer precision for PNG/TGA/PSD sources. There is no
		// 16-bit UNORM+sRGB Vulkan format, so sRGB color data is linearized here.
		if (stbi_is_16_bit(path.c_str()))
		{
			stbi_us* pixels = nullptr;
			{
				KBR_TRACY_SCOPE("TextureImporter::ImportTexture - stbi_load_16");
				pixels = stbi_load_16(path.c_str(), &width, &height, &channels, requestedChannels);
			}

			if (pixels == nullptr)
			{
				Log::CoreError("TextureImporter::ImportTexture - failed to load 16-bit texture from filepath: {}", path);
				KBRAssert(false, "TextureImporter::LoadTextureData - stbi_load_16 returned null data for filepath: {}", path);
				return std::make_pair(TextureSpecification{}, Buffer{});
			}

			KBRAssert(width > 0 && height > 0, "TextureImporter::ImportTexture - failed to load texture with valid dimensions from filepath: {}", path);

			data.Size = static_cast<uint64_t>(width) * static_cast<uint64_t>(height) *
				static_cast<uint64_t>(outputChannels) * sizeof(stbi_us);
			data.Allocate(data.Size);
			std::memcpy(data.Data, pixels, data.Size);
			stbi_image_free(pixels);

			if (srgb && outputChannels >= 3)
			{
				const auto& linearLut = GetSrgb16ToLinear16Lut();
				auto* values = reinterpret_cast<stbi_us*>(data.Data);
				const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
				for (size_t pixel = 0; pixel < pixelCount; ++pixel)
				{
					stbi_us* components = values + pixel * static_cast<size_t>(outputChannels);
					components[0] = linearLut[components[0]];
					components[1] = linearLut[components[1]];
					components[2] = linearLut[components[2]];
				}
			}

			spec.Width = static_cast<uint32_t>(width);
			spec.Height = static_cast<uint32_t>(height);
			spec.Format = outputChannels == 1 ? ImageFormat::R16 :
				(outputChannels == 2 ? ImageFormat::RG16 :
				(outputChannels == 3 ? ImageFormat::RGB16 : ImageFormat::RGBA16));
			spec.SourceSrgb = srgb && outputChannels >= 3;
			return std::make_pair(spec, std::move(data));
		}

		stbi_uc* pixels = nullptr;
		{
			KBR_TRACY_SCOPE("TextureImporter::ImportTexture - stbi_load");
			pixels = stbi_load(path.c_str(), &width, &height, &channels, requestedChannels);
		}

		if (pixels == nullptr)
		{
			Log::CoreError("TextureImporter::ImportTexture - failed to load texture from filepath: {}", path);
			KBRAssert(false, "TextureImporter::ImportTexture - stbi_load returned null data for filepath: {}", path);
			return std::make_pair(TextureSpecification{}, Buffer{});
		}

		KBRAssert(width > 0 && height > 0, "TextureImporter::ImportTexture - failed to load texture with valid dimensions from filepath: {}", path);

		data.Size = static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * outputChannels;
		data.Allocate(data.Size);
		std::memcpy(data.Data, pixels, data.Size);
		stbi_image_free(pixels);

		spec.Width = static_cast<uint32_t>(width);
		spec.Height = static_cast<uint32_t>(height);
		const ImageFormat format = outputChannels == 1 ? ImageFormat::R8 :
			(outputChannels == 2 ? ImageFormat::RG8 :
			(outputChannels == 3 ? ImageFormat::RGB8 : ImageFormat::RGBA8));
		spec.Format = srgb ? KTX2FormatSelector::ToSrgb(format) : format;
		spec.SourceSrgb = srgb && outputChannels >= 3;

		return std::make_pair(spec, std::move(data));
	}
}