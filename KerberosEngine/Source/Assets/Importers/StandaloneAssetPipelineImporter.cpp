#include "StandaloneAssetPipelineImporter.hpp"

#include <algorithm>
#include <cctype>

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

	StandaloneAssetPipelineImporter::StandaloneAssetPipelineImporter(std::string extension, const AssetType assetType)
		: m_Extension(NormalizeExtension(std::move(extension))), m_AssetType(assetType)
	{
	}

	bool StandaloneAssetPipelineImporter::SupportsExtension(const std::string_view extension) const
	{
		return m_Extension == NormalizeExtension(std::string(extension));
	}

	ImportResult StandaloneAssetPipelineImporter::Import(const ImportContext& context)
	{
		ImportResult result;
		result.SourceHandle = context.Meta.SourceHandle.IsValid() ? context.Meta.SourceHandle : AssetHandle();
		return result;
	}

	ImporterType StandaloneAssetPipelineImporter::Type() const
	{
		return ImporterType::Standalone;
	}

	AssetType StandaloneAssetPipelineImporter::SourceAssetType() const
	{
		return m_AssetType;
	}
}
