#include "kbrpch.hpp"
#include "AssetBuildTransaction.hpp"

#include <chrono>

namespace Kerberos {

AssetBuildTransaction::AssetBuildTransaction(std::filesystem::path outputRoot, std::string name)
    : m_OutputRoot(std::move(outputRoot))
{
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    m_StagingRoot = m_OutputRoot / (".kbr-build-" + std::move(name) + "-" + std::to_string(stamp));
    std::error_code ec;
    std::filesystem::create_directories(m_StagingRoot, ec);
    if (ec) 
        m_StagingRoot.clear();
}

AssetBuildTransaction::~AssetBuildTransaction() 
{ 
    if (!m_Committed) 
        Rollback(); 
}

bool AssetBuildTransaction::Commit(std::string* error)
{
    if (m_StagingRoot.empty()) 
    { 
        if (error) 
        {
            *error = "Unable to create staging directory";
        }
        return false; 
    }
    std::error_code ec;
    std::filesystem::create_directories(m_OutputRoot, ec);
    if (ec) 
    { 
        if (error)
        {
            *error = ec.message(); 
        }
        return false; 
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(m_StagingRoot, ec))
    {
        if (ec) 
            break;
        if (!entry.is_regular_file()) 
            continue;

        const auto relative = std::filesystem::relative(entry.path(), m_StagingRoot, ec);
        const auto destination = m_OutputRoot / relative;
        std::filesystem::create_directories(destination.parent_path(), ec);
        std::filesystem::remove(destination, ec);
        ec.clear();
        std::filesystem::rename(entry.path(), destination, ec);
        if (ec) 
            break;
    }
    if (ec) 
    { 
        if (error)
        {
            *error = ec.message(); 
        }
        Rollback(); 
        return false; 
    }
    m_Committed = true;
    Rollback();
    return true;
}

void AssetBuildTransaction::Rollback() const
{
    if (!m_StagingRoot.empty())
    {
        std::error_code ec;
        std::filesystem::remove_all(m_StagingRoot, ec);
    }
}

}
