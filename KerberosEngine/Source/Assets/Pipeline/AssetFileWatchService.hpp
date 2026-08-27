#pragma once

#include "AssetPipelineEvents.hpp"

#include <filewatch/FileWatch.hpp>

#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>

namespace Kerberos {

class AssetFileWatchService
{
public:
    using Callback = std::function<void(const AssetFileEvent&)>;

    AssetFileWatchService() = default;
    ~AssetFileWatchService();

    AssetFileWatchService(const AssetFileWatchService&) = delete;
    AssetFileWatchService& operator=(const AssetFileWatchService&) = delete;

    void Start(std::filesystem::path assetsRoot, const std::unordered_set<std::string>& supportedExtensions,
               Callback callback);
    void Stop();
    void Reload(std::filesystem::path assetsRoot, std::unordered_set<std::string> supportedExtensions);

private:
    void OnFileEvent(const std::filesystem::path& path, filewatch::Event event);
    bool IsWatchedPath(const std::filesystem::path& path) const;
    static std::filesystem::path Normalize(const std::filesystem::path& path);

    std::filesystem::path m_AssetsRoot;
    std::unordered_set<std::string> m_SupportedExtensions;
    Callback m_Callback;
    Owner<filewatch::FileWatch<std::string>> m_Watch;
    AssetEventDebouncer m_Debouncer;
    std::mutex m_Mutex;
    std::filesystem::path m_RenameSource;
};

}
