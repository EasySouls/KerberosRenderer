#pragma once

#include "IAssetImporter.hpp"

#include <string_view>

namespace Kerberos
{
	/**
	 * Validates authored .kbrai state-tree documents and copies them into the
	 * cache so runtime loading consumes a registry LibraryPath rather than the
	 * project source tree.
	 */
	class AIStateTreePipelineImporter final : public IAssetImporter
	{
	public:
		AIStateTreePipelineImporter() = default;

		bool SupportsExtension(std::string_view extension) const override;
		ImportResult Import(const ImportContext& context) override;
		ImporterType Type() const override;
		uint32_t Version() const override;
		AssetType SourceAssetType() const override;
	};
}
