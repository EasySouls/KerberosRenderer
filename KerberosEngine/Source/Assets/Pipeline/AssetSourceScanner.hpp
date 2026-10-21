#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Kerberos {

class AssetSourceScanner
{
public:
    struct ScanOptions
    {
        bool Recursive = true;
        bool SkipCacheDirectories = true;
        bool SkipMetaFiles = true;
    };

    struct ScanResult
    {
        std::vector<std::filesystem::path> Files;
        std::vector<std::string> Errors;
    };

    AssetSourceScanner() = default;

    static std::vector<std::filesystem::path> ScanForAssets(const std::filesystem::path& directory,
                                                            const std::vector<std::string>& extensions);

    static ScanResult Scan(const std::filesystem::path& directory,
                           const std::vector<std::string>& extensions,
                           ScanOptions options = {});
};

}