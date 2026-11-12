#include "AIStateTreePipelineImporter.hpp"

#include "AI/AIStateTreeRegistry.hpp"
#include "AI/AIStateTreeSerializer.hpp"
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

	bool AIStateTreePipelineImporter::SupportsExtension(const std::string_view extension) const
	{
		return NormalizeExtension(std::string(extension)) == ".kbrai";
	}

	ImportResult AIStateTreePipelineImporter::Import(const ImportContext& context)
	{
		KBR_TRACY_FUNCTION();

		AI::AIStateTreeDocument document;
		std::string error;
		if (!AI::AIStateTreeSerializer::LoadFile(context.SourceAbsolutePath, document, &error))
		{
			throw std::runtime_error("Failed to import AI state tree '" +
				context.SourceAbsolutePath.string() + "': " + error);
		}

		if (document.Name.empty())
			document.Name = context.SourceAbsolutePath.stem().string();

		if (!AI::AIStateTreeRegistry::CompileDocument(document, &error))
		{
			throw std::runtime_error("AI state tree '" + context.SourceAbsolutePath.string() +
				"' failed validation: " + error);
		}

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
			throw std::runtime_error("Failed to create AI state tree cache directory: " + ec.message());

		if (!std::filesystem::copy_file(context.SourceAbsolutePath, artifactAbsolute,
				std::filesystem::copy_options::overwrite_existing, ec) || ec)
		{
			throw std::runtime_error("Failed to copy AI state tree '" +
				context.SourceAbsolutePath.string() + "' into the cache: " + ec.message());
		}

		result.SourceLibraryPath = relative;
		return result;
	}

	ImporterType AIStateTreePipelineImporter::Type() const
	{
		return ImporterType::AIStateTree;
	}

	uint32_t AIStateTreePipelineImporter::Version() const
	{
		return 1;
	}

	AssetType AIStateTreePipelineImporter::SourceAssetType() const
	{
		return AssetType::AIStateTree;
	}
}
