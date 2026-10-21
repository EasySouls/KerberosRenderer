#include "MaterialPipelineImporter.hpp"

#include "Profiling/Profilers.hpp"

#include <algorithm>
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
	}

	bool MaterialPipelineImporter::SupportsExtension(const std::string_view extension) const
	{
		return NormalizeExtension(std::string(extension)) == ".kbrmat";
	}

	ImportResult MaterialPipelineImporter::Import(const ImportContext& context)
	{
		KBR_TRACY_FUNCTION();

		ImportResult result;
		result.SourceHandle = context.Meta.SourceHandle.IsValid() ? context.Meta.SourceHandle : AssetHandle();

		std::error_code ec;
		std::filesystem::path relative =
			std::filesystem::relative(context.SourceAbsolutePath, context.AssetRootAbsolutePath, ec);
		if (ec || relative.empty() || relative.is_absolute() || *relative.begin() == "..")
			relative = context.SourceAbsolutePath.filename();

		const std::filesystem::path artifactAbsolute = context.CacheRootAbsolutePath / relative;
		std::filesystem::create_directories(artifactAbsolute.parent_path(), ec);
		if (ec)
			throw std::runtime_error("Failed to create material cache directory: " + ec.message());

		if (!std::filesystem::copy_file(context.SourceAbsolutePath, artifactAbsolute,
				std::filesystem::copy_options::overwrite_existing, ec) || ec)
		{
			throw std::runtime_error("Failed to copy material '" + context.SourceAbsolutePath.string() +
				"' into the cache: " + ec.message());
		}

		result.SourceLibraryPath = relative;
		return result;
	}

	ImporterType MaterialPipelineImporter::Type() const
	{
		return ImporterType::Material;
	}

	uint32_t MaterialPipelineImporter::Version() const
	{
		return 1;
	}

	AssetType MaterialPipelineImporter::SourceAssetType() const
	{
		return AssetType::Material;
	}
}
