#include "AssetSourceScanner.hpp"

#include "kbrpch.hpp"

#include <algorithm>
#include <string_view>

namespace Kerberos {

std::vector<std::filesystem::path> AssetSourceScanner::ScanForAssets(const std::filesystem::path& directory,
                                                                     const std::vector<std::string>& extensions) const
{
    std::vector<std::filesystem::path> assetFiles;

    if (!std::filesystem::exists(directory) || !std::filesystem::is_directory(directory)) {
        KBR_CORE_WARN("AssetSourceScanner: Directory does not exist or is not a directory: {0}", directory.string());
        return assetFiles;
    }

    constexpr std::string_view cacheDirName = "Cache";

    for (const auto& entry : std::filesystem::recursive_directory_iterator(directory)) {
        if (entry.is_regular_file() && entry.path().parent_path().filename() != cacheDirName) {
            const auto& path = entry.path();
            const auto ext = path.extension().string();
            if (std::find(extensions.begin(), extensions.end(), ext) != extensions.end()) {
                assetFiles.push_back(path);
            }
        }
    }

    return assetFiles;
};

}