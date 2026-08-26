#pragma once

#include <filesystem>
#include <vector>

namespace Kerberos {

class AssetSourceScanner
{
public:
    AssetSourceScanner() = default;

    std::vector<std::filesystem::path> ScanForAssets(const std::filesystem::path& directory,
                                                     const std::vector<std::string>& extensions) const;
};

}