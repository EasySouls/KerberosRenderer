#pragma once

#include "Assets/AssetRegistry.hpp"

#include <filesystem>
#include <optional>

namespace Kerberos {

struct RuntimeAssetLocation
{
	AssetMetadata Metadata;
	std::filesystem::path Path;
};

class RuntimeAssetResolver
{
public:
	RuntimeAssetResolver() = default;
    explicit RuntimeAssetResolver(const AssetRegistry* registry,
                                  std::filesystem::path assetRoot = {},
                                  std::filesystem::path libraryRoot = {});

	void Configure(const AssetRegistry* registry,
		std::filesystem::path assetRoot = {},
		std::filesystem::path libraryRoot = {});

	std::optional<RuntimeAssetLocation> Resolve(AssetHandle handle) const;

private:
	const AssetRegistry* m_Registry = nullptr;
	std::filesystem::path m_AssetRoot;
	std::filesystem::path m_LibraryRoot;
};

}
