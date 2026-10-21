#include "TexturePipelineImporter.hpp"

#include "Assets/Importers/TextureImporter.hpp"
#include "Renderer/Textures/KTX2Encoder.hpp"
#include "Utils/KtxConversion.hpp"
#include "Profiling/Profilers.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace Kerberos
{
	namespace
	{
		std::string NormalizeExtension(std::string extension)
		{
			if (!extension.empty() && extension.front() != '.')
				extension.insert(extension.begin(), '.');
			std::ranges::transform(extension, extension.begin(), [](const unsigned char value)
			{
				return static_cast<char>(std::tolower(value));
			});
			return extension;
		}

		bool IsRasterExtension(const std::string_view extension)
		{
			constexpr std::array<std::string_view, 10> rasterExtensions = {
				".png", ".jpg", ".jpeg", ".bmp", ".tga", ".gif", ".psd", ".hdr", ".pic", ".pnm"
			};
			return std::ranges::find(rasterExtensions, extension) != rasterExtensions.end();
		}
	}

	bool TexturePipelineImporter::SupportsExtension(const std::string_view extension) const
	{
		const std::string normalized = NormalizeExtension(std::string(extension));
		return IsRasterExtension(normalized) || normalized == ".ktx" || normalized == ".ktx2";
	}

	ImportResult TexturePipelineImporter::Import(const ImportContext& context)
	{
		KBR_TRACY_FUNCTION();

		ImportResult result;
		result.SourceHandle = context.Meta.SourceHandle.IsValid() ? context.Meta.SourceHandle : AssetHandle();

		const std::string extension = NormalizeExtension(context.SourceAbsolutePath.extension().string());
		const bool isKtx2 = extension == ".ktx2";
		const bool isKtx1 = extension == ".ktx";
		const bool isRaster = IsRasterExtension(extension);
		if (!isKtx2 && !isKtx1 && !isRaster)
			throw std::runtime_error("TexturePipelineImporter does not support extension: " + extension);

		// Per-source cache namespace derived from the source path relative to the
		// assets root. The source extension is kept in the namespace so same-stem
		// sources (e.g. foo.png and foo.ktx2) never overwrite each other.
		std::error_code ec;
		std::filesystem::path namespacePath =
			std::filesystem::relative(context.SourceAbsolutePath, context.AssetRootAbsolutePath, ec);
		if (ec || namespacePath.empty() || namespacePath.is_absolute() || *namespacePath.begin() == "..")
			namespacePath = context.SourceAbsolutePath.filename();

		// Artifact path inside the per-source namespace, e.g.
		// Textures/foo.png/foo.ktx2. This path is relative to the cache root.
		const std::filesystem::path artifactRelative =
			namespacePath / (context.SourceAbsolutePath.stem().string() + ".ktx2");
		const std::filesystem::path artifactAbsolute = context.CacheRootAbsolutePath / artifactRelative;
		std::filesystem::create_directories(artifactAbsolute.parent_path(), ec);
		if (ec)
			throw std::runtime_error("Failed to create texture cache directory: " + ec.message());

		if (isKtx2)
		{
			// Already KTX2: copy as-is. The embedded format (including sRGB) governs.
			if (!std::filesystem::copy_file(context.SourceAbsolutePath, artifactAbsolute,
					std::filesystem::copy_options::overwrite_existing, ec) || ec)
				throw std::runtime_error("Failed to copy KTX2 texture '" +
					context.SourceAbsolutePath.string() + "': " + ec.message());
		}
		else if (isKtx1)
		{
			// KTX1 -> KTX2, written directly into the staging cache.
			const Process::ProcessResult converted =
				KtxConversion::ConvertKtxToKtx2(context.SourceAbsolutePath, true, artifactAbsolute);
			if (!converted.Succeeded || !std::filesystem::exists(artifactAbsolute))
			{
				// Fallback for tool versions without explicit output support:
				// convert next to the source, move the result into the cache, and
				// clean up the source-tree sidecar.
				auto sidecar = context.SourceAbsolutePath;
				sidecar.replace_extension(".ktx2");
				const Process::ProcessResult fallback =
					KtxConversion::ConvertKtxToKtx2(context.SourceAbsolutePath, true);
				if (!fallback.Succeeded || !std::filesystem::exists(sidecar))
					throw std::runtime_error("Failed to convert KTX to KTX2 for '" +
						context.SourceAbsolutePath.string() + "': " + converted.ErrorMessage);

				ec.clear();
				std::filesystem::copy_file(sidecar, artifactAbsolute,
					std::filesystem::copy_options::overwrite_existing, ec);
				std::filesystem::remove(sidecar, ec);
				if (ec || !std::filesystem::exists(artifactAbsolute))
					throw std::runtime_error("Failed to move converted KTX2 into the cache: " + ec.message());
			}
		}
		else
		{
			// Raster: decode + encode. sRGB is user-configurable because raster
			// formats cannot reliably describe their color space.
			const auto srgbIt = context.Meta.TextureSrgb.find("source");
			const bool srgb = srgbIt != context.Meta.TextureSrgb.end() ? srgbIt->second : true;

			auto [specification, buffer] = TextureImporter::LoadTextureData(
				context.SourceAbsolutePath, /*flip=*/true, /*desiredChannels=*/0, srgb);
			if (!buffer.Data || buffer.Size == 0)
				throw std::runtime_error("Failed to decode texture '" + context.SourceAbsolutePath.string() + "'");
			if (!KTX2Encoder::Encode(artifactAbsolute, specification, buffer))
				throw std::runtime_error("Failed to encode texture to KTX2 '" + artifactAbsolute.string() + "'");

			result.TextureSrgb["source"] = srgb;
		}

		result.SourceLibraryPath = artifactRelative;
		return result;
	}

	ImporterType TexturePipelineImporter::Type() const
	{
		return ImporterType::Texture2D;
	}

	uint32_t TexturePipelineImporter::Version() const
	{
		return 1;
	}

	AssetType TexturePipelineImporter::SourceAssetType() const
	{
		return AssetType::Texture2D;
	}
}
