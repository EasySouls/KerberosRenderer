#pragma once

#include "Core/Core.hpp"
#include "Renderer/Font.hpp"

#include <chrono>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace Kerberos {

class TextureManager;

/// Process-wide cache of UI fonts. A Font owns its MSDF atlas, so the library
/// keeps the Ref alive for the lifetime of the process and registers each
/// atlas once in the global bindless texture table.
///
/// All caches are owned by the singleton and are not thread-safe, use them from
/// the main/render thread only, when implementing multiple threads.
class UIFontLibrary
{
public:
    static UIFontLibrary& Get();

    void Initialize(TextureManager* textureManager);
    void Shutdown();

    /// Resolves the authored path and returns a font rasterized at a bucket
    /// close to the requested pixel size. Returns nullptr only when even the
    /// default font cannot be resolved.
    Ref<Font> Get(const std::string& authoredPath, float fontSize);

    /// Bindless texture index of the font's atlas. Returns 0 when unavailable.
    uint32_t GetTextureIndex(const Ref<Font>& font);

    /// Authored path -> absolute path. Returns an empty path when nothing resolves.
    static std::filesystem::path ResolveFontPath(const std::string& authoredPath);
    /// Authored (project- or engine-relative) path for a resolved absolute path.
    static std::string MakeAuthoredPath(const std::filesystem::path& absolutePath);

    static std::string DefaultFontPath();

    /// Font files discoverable by the editor font picker, as authored paths.
    ///
    /// The list is cached: the font directories are only rescanned after a
    /// project switch or when a scanned root's last-write time moves (checked
    /// at most twice per second). Never hold the returned reference across
    /// frames.
    const std::vector<std::string>& GetFontFiles();

    /// Drops the cached font file list so the next GetFontFiles() rescans.
    void InvalidateFontFiles();

    UIFontLibrary(const UIFontLibrary&) = delete;
    UIFontLibrary& operator=(const UIFontLibrary&) = delete;

private:
    UIFontLibrary() = default;
    ~UIFontLibrary() = default;

    static double SelectEmSize(float fontSize);

    /// Drops the project-relative caches when the active project changed.
    /// Authored font paths and the discovered file list are project-relative,
    /// so a project switch invalidates both.
    void EnsureProjectKey();

    /// Rebuilds the cached font file list and records the scanned roots with
    /// their timestamps.
    void ScanFontFiles();

    /// True when a scanned root was created, removed or modified since the last
    /// scan. Cheap because it only stats directories and is gated by
    /// FontFileRefreshInterval.
    bool FontRootsChanged();

    struct Entry
    {
        Ref<Font> Font = nullptr;
        uint32_t TextureIndex = 0;
    };

    TextureManager* m_TextureManager = nullptr;
    std::unordered_map<std::string, Entry> m_Entries;
    std::unordered_map<std::string, std::filesystem::path> m_ResolvedPaths;

    // Identity of the project the project-relative caches were built for.
    bool m_HasProject = false;
    std::filesystem::path m_ProjectDirectory{};

    // Cached font picker entries and the roots they were discovered under.
    std::vector<std::string> m_FontFiles;
    std::vector<std::filesystem::path> m_FontRoots;
    std::vector<std::filesystem::file_time_type> m_FontRootStamps;
    bool m_FontFilesScanned = false;
    std::chrono::steady_clock::time_point m_NextFontFileCheck{};
};

}
