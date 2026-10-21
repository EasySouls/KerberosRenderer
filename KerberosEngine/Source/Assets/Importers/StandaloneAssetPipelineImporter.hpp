#pragma once

#include "IAssetImporter.hpp"

#include <string>

namespace Kerberos
{
	class StandaloneAssetPipelineImporter final : public IAssetImporter
	{
	public:
		StandaloneAssetPipelineImporter(std::string extension, AssetType assetType);

		bool SupportsExtension(std::string_view extension) const override;
		ImportResult Import(const ImportContext& context) override;
		ImporterType Type() const override;
		AssetType SourceAssetType() const override;

	private:
		std::string m_Extension;
		AssetType m_AssetType;
	};
}
