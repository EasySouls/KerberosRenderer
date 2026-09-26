#include "kbrpch.hpp"
#include "RuntimeAssetResolver.hpp"

#include <system_error>
#include <utility>

namespace Kerberos {

RuntimeAssetResolver::RuntimeAssetResolver(const AssetRegistry* registry,
	std::filesystem::path assetRoot, std::filesystem::path libraryRoot)
{
	KBR_TRACY_FUNCTION();

	Configure(registry, std::move(assetRoot), std::move(libraryRoot));
}

void RuntimeAssetResolver::Configure(const AssetRegistry* registry,
	std::filesystem::path assetRoot, std::filesystem::path libraryRoot)
{
	m_Registry = registry;
	m_AssetRoot = std::move(assetRoot);
	m_LibraryRoot = std::move(libraryRoot);
}

std::optional<RuntimeAssetLocation> RuntimeAssetResolver::Resolve(const AssetHandle handle) const
{
	KBR_TRACY_FUNCTION();

	if (!m_Registry || !handle.IsValid() || !m_Registry->Contains(handle))
		return std::nullopt;

	const AssetMetadata& metadata = m_Registry->Get(handle);
	std::vector<std::filesystem::path> candidates;
	if (metadata.LibraryPath.is_absolute())
		candidates.push_back(metadata.LibraryPath);
	else if (!metadata.LibraryPath.empty() && !m_LibraryRoot.empty())
		candidates.push_back(m_LibraryRoot / metadata.LibraryPath);
	else if (!metadata.LibraryPath.empty())
		candidates.push_back(metadata.LibraryPath);

	std::error_code ec;
	for (const auto& candidate : candidates)
	{
		if (std::filesystem::is_regular_file(candidate, ec))
			return RuntimeAssetLocation{ .Metadata = metadata, .Path = candidate };
		ec.clear();
	}
	return std::nullopt;
}

}
