#pragma once

#include "IAssetImporter.hpp"

#include <string_view>

namespace Kerberos
{
	/**
	 * Imports raster images and KTX/KTX2 textures into the cache as KTX2
	 * artifacts, so runtime loading only ever consumes KTX2.
	 *
	 * Raster sources are decoded and re-encoded; KTX1 sources are converted;
	 * KTX2 sources are copied as-is (their embedded format governs). sRGB is
	 * driven by the source meta for raster images only, since raster formats
	 * cannot reliably describe their own color space.
	 */
	class TexturePipelineImporter final : public IAssetImporter
	{
	public:
		TexturePipelineImporter() = default;

		bool SupportsExtension(std::string_view extension) const override;
		ImportResult Import(const ImportContext& context) override;
		ImporterType Type() const override;
		uint32_t Version() const override;
		AssetType SourceAssetType() const override;
	};
}
