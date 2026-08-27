#pragma once

#include "AssetBuildTransaction.hpp"
#include "AssetMetaService.hpp"
#include "AssetStalenessEvaluator.hpp"
#include "Assets/AssetRegistry.hpp"
#include "Assets/Importers/ImporterRegistry.hpp"

#include <future>
#include <mutex>
#include <vector>

namespace Kerberos {

struct AssetBuildReport
{
    std::filesystem::path Source;
    AssetStaleReason Reason = AssetStaleReason::None;
    bool Built = false;
    std::vector<std::string> Errors;
    std::vector<std::string> Warnings;
};

class AssetBuildCoordinator
{
public:
    AssetBuildCoordinator(std::filesystem::path assetsRoot,
                           std::filesystem::path cacheRoot,
                           AssetMetaService& metaService,
                           ImporterRegistry& importers,
                           AssetRegistry* registry = nullptr);

    AssetBuildReport Build(const std::filesystem::path& source, bool force = false);
    std::future<AssetBuildReport> BuildAsync(std::filesystem::path source, bool force = false);
    std::vector<AssetBuildReport> BuildAll(const std::vector<std::filesystem::path>& sources, bool force = false);
    void Shutdown();

private:
    std::filesystem::path m_AssetsRoot;
    std::filesystem::path m_CacheRoot;
    AssetMetaService& m_MetaService;
    ImporterRegistry& m_Importers;
    AssetRegistry* m_Registry;
    std::mutex m_BuildMutex;
};

}
