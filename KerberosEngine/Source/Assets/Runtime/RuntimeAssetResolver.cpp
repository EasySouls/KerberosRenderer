#include "kbrpch.hpp"
#include "RuntimeAssetResolver.hpp"

#include <system_error>
#include <utility>

namespace Kerberos {

RuntimeAssetResolver::RuntimeAssetResolver(const AssetRegistry* registry,
	std::filesystem::path assetRoot, std::filesystem::path libraryRoot,
	std::filesystem::path engineLibraryRoot)
{
	KBR_TRACY_FUNCTION();

	Configure(registry, std::move(assetRoot), std::move(libraryRoot), std::move(engineLibraryRoot));
}

void RuntimeAssetResolver::Configure(const AssetRegistry* registry,
	std::filesystem::path assetRoot, std::filesystem::path libraryRoot,
	std::filesystem::path engineLibraryRoot)
{
	m_Registry = registry;
	m_AssetRoot = std::move(assetRoot);
	m_LibraryRoot = std::move(libraryRoot);
	m_EngineLibraryRoot = std::move(engineLibraryRoot);
}

std::optional<RuntimeAssetLocation> RuntimeAssetResolver::Resolve(const AssetHandle handle) const
{
	KBR_TRACY_FUNCTION();

	if (!m_Registry || !handle.IsValid() || !m_Registry->Contains(handle))
		return std::nullopt;

	const AssetMetadata& metadata = m_Registry->Get(handle);

	const std::filesystem::path& libraryRoot = metadata.SourceRoot == AssetSourceRoot::Engine
		? (m_EngineLibraryRoot.empty() ? m_LibraryRoot : m_EngineLibraryRoot)
		: m_LibraryRoot;

	std::vector<std::filesystem::path> candidates;
	if (metadata.LibraryPath.is_absolute())
		candidates.push_back(metadata.LibraryPath);
	else if (!metadata.LibraryPath.empty() && !libraryRoot.empty())
		candidates.push_back(libraryRoot / metadata.LibraryPath);
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
