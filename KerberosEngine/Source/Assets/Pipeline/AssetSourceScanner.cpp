#include "AssetSourceScanner.hpp"

#include "kbrpch.hpp"

#include <algorithm>
#include <string_view>
#include <cctype>
#include <unordered_set>
#include <ranges>

namespace Kerberos {

std::vector<std::filesystem::path> AssetSourceScanner::ScanForAssets(const std::filesystem::path& directory,
                                                                     const std::vector<std::string>& extensions)
{
    return Scan(directory, extensions).Files;
}

AssetSourceScanner::ScanResult AssetSourceScanner::Scan(const std::filesystem::path& directory,
                                                        const std::vector<std::string>& extensions,
                                                        const ScanOptions options)
{
    KBR_PROFILE_FUNCTION();

    ScanResult result;

    if (!std::filesystem::exists(directory) || !std::filesystem::is_directory(directory)) {
        result.Errors.push_back("Directory does not exist or is not a directory: " + directory.string());
        return result;
    }

    std::unordered_set<std::string> wanted;
    for (std::string extension : extensions) {
        if (!extension.empty() && extension.front() != '.')
            extension.insert(extension.begin(), '.');
        std::ranges::transform(extension, extension.begin(),
                               [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });

        wanted.insert(std::move(extension));
    }

    std::error_code error;
    constexpr auto iteratorOptions = std::filesystem::directory_options::skip_permission_denied;
    if (options.Recursive) 
    {
        for (std::filesystem::recursive_directory_iterator it(directory, iteratorOptions, error), end; it != end;
             it.increment(error)) {
            if (error) {
                result.Errors.push_back(error.message());
                error.clear();
                continue;
            }
            const auto& entry = *it;
            std::string filename = entry.path().filename().string();
            std::ranges::transform(
                filename, filename.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (options.SkipCacheDirectories && entry.is_directory(error) && filename == "cache") {
                it.disable_recursion_pending();
                continue;
            }
            if (entry.is_regular_file(error)) {
                const auto& path = entry.path();
                std::string ext = path.extension().string();
                std::ranges::transform(
                    ext, ext.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (wanted.contains(ext) && (!options.SkipMetaFiles || ext != ".meta")) {
                    result.Files.push_back(path.lexically_normal());
                }
            }
        }
    }
    else
    {
        for (const auto& entry : std::filesystem::directory_iterator(directory, iteratorOptions, error))
        {
            if (error) { result.Errors.push_back(error.message()); error.clear(); continue; }
            if (!entry.is_regular_file(error)) continue;
            auto ext = entry.path().extension().string();
            std::ranges::transform(ext, ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (wanted.contains(ext) && (!options.SkipMetaFiles || ext != ".meta"))
                result.Files.push_back(entry.path().lexically_normal());
        }
    }
    std::ranges::sort(result.Files);
    return result;
};

}