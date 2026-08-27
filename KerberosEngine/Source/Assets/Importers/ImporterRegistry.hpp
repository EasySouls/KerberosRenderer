#pragma once

#include "IAssetImporter.hpp"

#include <memory>
#include <shared_mutex>
#include <string_view>
#include <vector>

namespace Kerberos {

class ImporterRegistry
{
public:
	void Register(Ref<IAssetImporter> importer);
	Ref<IAssetImporter> Find(std::string_view extension) const;

private:
	std::vector<Ref<IAssetImporter>> m_Importers;
	mutable std::shared_mutex m_Mutex;
};

}
