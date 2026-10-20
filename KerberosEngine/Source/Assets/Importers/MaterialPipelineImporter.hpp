#pragma once

#include "IAssetImporter.hpp"

#include <string_view>

namespace Kerberos
{
	/**
	 * Copies authored .kbrmat YAML sources into the cache so every consumer loads
	 * materials through a registry LibraryPath instead of reaching back into the
	 * source tree. Texture references inside the material are asset handles, so the
	 * copied artifact stays independent of the material's source location.
	 */
	class MaterialPipelineImporter final : public IAssetImporter
	{
	public:
		MaterialPipelineImporter() = default;

		bool SupportsExtension(std::string_view extension) const override;
		ImportResult Import(const ImportContext& context) override;
		ImporterType Type() const override;
		uint32_t Version() const override;
		AssetType SourceAssetType() const override;
	};
}
