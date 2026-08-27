#pragma once

#include <filesystem>
#include <string>

namespace Kerberos {

class AssetBuildTransaction
{
public:
    AssetBuildTransaction(std::filesystem::path outputRoot, std::string name);
    ~AssetBuildTransaction();
    AssetBuildTransaction(const AssetBuildTransaction&) = delete;
    AssetBuildTransaction& operator=(const AssetBuildTransaction&) = delete;

    const std::filesystem::path& StagingRoot() const { return m_StagingRoot; }
    bool Commit(std::string* error = nullptr);
    void Rollback() const;

private:
    std::filesystem::path m_OutputRoot;
    std::filesystem::path m_StagingRoot;
    bool m_Committed = false;
};

}
