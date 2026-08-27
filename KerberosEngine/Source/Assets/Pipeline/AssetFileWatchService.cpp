#include "kbrpch.hpp"
#include "AssetFileWatchService.hpp"

#include "filewatch/FileWatch.hpp"

#include <algorithm>

namespace Kerberos {

namespace {

std::string Lower(std::string value)
{
    std::ranges::transform(value, value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

}

AssetFileWatchService::~AssetFileWatchService()
{
    Stop();
}

void AssetFileWatchService::Start(std::filesystem::path assetsRoot,
    const std::unordered_set<std::string>& supportedExtensions, Callback callback)
{
    Stop();
    m_AssetsRoot = Normalize(std::move(assetsRoot));
    m_SupportedExtensions.clear();
    for (auto extension : supportedExtensions)
    {
        if (!extension.empty() && extension.front() != '.')
            extension.insert(extension.begin(), '.');
        m_SupportedExtensions.insert(Lower(std::move(extension)));
    }
    m_Callback = std::move(callback);
    m_Debouncer.Start();
    m_Debouncer.SetCallback(m_Callback);

    if (m_AssetsRoot.empty() || !std::filesystem::is_directory(m_AssetsRoot))
        return;

    auto watchCallback = [this](const std::string& path, const filewatch::Event event) {
        OnFileEvent(path, event);
    };
    m_Watch = std::make_unique<filewatch::FileWatch<std::string>>(m_AssetsRoot.string(), watchCallback);
}

void AssetFileWatchService::Stop()
{
    m_Watch.reset();
    m_Debouncer.Stop();
    std::scoped_lock lock(m_Mutex);
    m_RenameSource.clear();
}

void AssetFileWatchService::Reload(std::filesystem::path assetsRoot,
    std::unordered_set<std::string> supportedExtensions)
{
    auto callback = m_Callback;
    Start(std::move(assetsRoot), std::move(supportedExtensions), std::move(callback));
}

std::filesystem::path AssetFileWatchService::Normalize(const std::filesystem::path& path)
{
    return std::filesystem::absolute(path).lexically_normal();
}

bool AssetFileWatchService::IsWatchedPath(const std::filesystem::path& path) const
{
    const auto relative = path.lexically_relative(m_AssetsRoot);
    if (relative.empty() || relative == ".")
        return false;

    for (const auto& component : relative)
    {
        const auto name = component.string();
        if (name.empty() || name.front() == '.' || Lower(name) == "cache" ||
            Lower(name) == "staging" || Lower(name) == ".staging")
            return false;
    }

    return m_SupportedExtensions.contains(Lower(path.extension().string()));
}

void AssetFileWatchService::OnFileEvent(const std::filesystem::path& path,
    const filewatch::Event event)
{
    const auto absolute = Normalize(m_AssetsRoot / path);
    if (!IsWatchedPath(absolute))
    {
        if (event == filewatch::Event::renamed_new)
        {
            std::scoped_lock lock(m_Mutex);
            m_RenameSource.clear();
        }
        return;
    }

    std::scoped_lock lock(m_Mutex);
    switch (event)
    {
    case filewatch::Event::renamed_old:
        m_RenameSource = absolute;
        break;
    case filewatch::Event::renamed_new:
        if (!m_RenameSource.empty() && IsWatchedPath(absolute))
        {
            m_Debouncer.Push({ .Type = AssetFileEventType::Renamed, .Path = absolute, .OldPath = m_RenameSource });
            m_RenameSource.clear();
        }
        break;
    case filewatch::Event::added:
        m_Debouncer.Push({ .Type = AssetFileEventType::Added, .Path = absolute, .OldPath = {} });
        break;
    case filewatch::Event::modified:
        m_Debouncer.Push({ .Type = AssetFileEventType::Modified, .Path = absolute, .OldPath = {} });
        break;
    case filewatch::Event::removed:
        m_Debouncer.Push({ .Type = AssetFileEventType::Removed, .Path = absolute, .OldPath = {} });
        break;
    }
}

}
