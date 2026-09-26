#pragma once

#include "Assets/Pipeline/ImportPipeline.hpp"
#include "Assets/Asset.hpp"

#include <string_view>

namespace Kerberos {

class IAssetImporter
{
public:
	virtual ~IAssetImporter() = default;

	virtual bool SupportsExtension(std::string_view extension) const = 0;
	virtual ImportResult Import(const ImportContext& context) = 0;
	virtual ImporterType Type() const { return ImporterType::None; }
	virtual uint32_t Version() const { return 1; }
	virtual AssetType SourceAssetType() const { return AssetType::Texture2D; }
};

}
