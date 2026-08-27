#include "kbrpch.hpp"
#include "ImporterRegistry.hpp"

#include <algorithm>
#include <cctype>
#include <mutex>

namespace Kerberos {

void ImporterRegistry::Register(std::shared_ptr<IAssetImporter> importer)
{
	if (importer) {
		std::unique_lock lock(m_Mutex);
		m_Importers.emplace_back(std::move(importer));
	}
}

std::shared_ptr<IAssetImporter> ImporterRegistry::Find(const std::string_view extension) const
{
	std::shared_lock lock(m_Mutex);
	std::string normalized(extension);
	if (!normalized.empty() && normalized.front() != '.')
		normalized.insert(normalized.begin(), '.');
	std::ranges::transform(normalized, normalized.begin(), [](const unsigned char value)
	{
		return static_cast<char>(std::tolower(value));
	});

	const auto it = std::ranges::find_if(m_Importers, [&normalized](const auto& importer)
	{
		return importer->SupportsExtension(normalized);
	});
	return it == m_Importers.end() ? nullptr : *it;
}

}
