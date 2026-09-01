#include "TextureImporter.hpp"
#include "Core/Core.hpp"
#include "Renderer/Textures/Texture2D.hpp"
#include "Renderer/Textures/KTX2Encoder.hpp"
#include "Profiling/Instrumentor.hpp"
#include "Utils/KtxConversion.hpp"
#include "ImportUtils.hpp"

#include <stb_image.h>

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
}

namespace Kerberos
{
	Ref<Texture2D> TextureImporter::ImportTexture(AssetHandle handle, const AssetMetadata& metadata)
	{
		return ImportTexture(metadata.Filepath);
	}

	Ref<Texture2D> TextureImporter::ImportTexture(const std::filesystem::path& filepath)
	{
		KBR_PROFILE_FUNCTION();

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

		auto [spec, buffer] = LoadTextureData(filepath, true);
		auto ktx2Filepath = filepath;
		ktx2Filepath.replace_extension(".ktx2");
		if (KTX2Encoder::EncodeIfNeeded(filepath, ktx2Filepath, spec, buffer))
			return Texture2D::FromFile(ktx2Filepath);

		auto texture = CreateRef<Texture2D>(spec, buffer);

		const std::string name = filepath.filename().string();
		//texture->SetDebugName(name);

		return texture;
	}

	std::pair<TextureSpecification, Buffer> TextureImporter::LoadTextureData(const std::filesystem::path& filepath, const bool flip, const int desiredChannels)
	{
		int width = 0, height = 0, channels = 0;

		stbi_set_flip_vertically_on_load(flip);
		Buffer data;

		if (!std::filesystem::exists(filepath))
		{
			Log::CoreError("TextureImporter::ImportTexture - file does not exist: {}", filepath.string());
			KBRAssert(false, "TextureImporter::ImportTexture - file does not exist: {}", filepath.string());
			return std::make_pair(TextureSpecification{}, Buffer{});
		}

		stbi_uc* pixels = nullptr;
		{
			KBR_PROFILE_SCOPE("TextureImporter::ImportTexture - stbi_load");
			const int requestedChannels = desiredChannels > 0 ? std::clamp(desiredChannels, 1, 4) : STBI_rgb_alpha;
			pixels = stbi_load(filepath.string().c_str(), &width, &height, &channels, requestedChannels);
		}

		if (pixels == nullptr)
		{
			Log::CoreError("TextureImporter::ImportTexture - failed to load texture from filepath: {}", filepath.string());
			KBRAssert(false, "TextureImporter::ImportTexture - stbi_load returned null data for filepath: {}", filepath.string());
			return std::make_pair(TextureSpecification{}, Buffer{});
		}

		KBRAssert(width > 0 && height > 0, "TextureImporter::ImportTexture - failed to load texture with valid dimensions from filepath: {}", filepath.string());

		const int outputChannels = desiredChannels > 0 ? std::clamp(desiredChannels, 1, 4) : 4;
		data.Size = static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * outputChannels;
		data.Allocate(data.Size);
		std::memcpy(data.Data, pixels, data.Size);
		stbi_image_free(pixels);

		TextureSpecification spec;
		spec.Width = width;
		spec.Height = height;
		spec.Format = outputChannels == 1 ? ImageFormat::R8 :
			(outputChannels == 2 ? ImageFormat::RG8 :
			(outputChannels == 3 ? ImageFormat::RGB8 : ImageFormat::RGBA8));

		return std::make_pair(spec, std::move(data));
	}
}