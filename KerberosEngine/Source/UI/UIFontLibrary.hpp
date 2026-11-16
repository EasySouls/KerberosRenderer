#pragma once

#include "Core/Core.hpp"
#include "Renderer/Font.hpp"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace Kerberos {

class TextureManager;

/// Process-wide cache of UI fonts. A Font owns its MSDF atlas, so the library
/// keeps the Ref alive for the lifetime of the process and registers each
/// atlas once in the global bindless texture table.
class UIFontLibrary
{
public:
    static UIFontLibrary& Get();

    void Initialize(TextureManager* textureManager);
    void Shutdown();

    /// Resolves the authored path and returns a font rasterized at a bucket
    /// close to the requested pixel size. Never returns nullptr.
    Ref<Font> Get(const std::string& authoredPath, float fontSize);

    /// Bindless texture index of the font's atlas. Returns 0 when unavailable.
    uint32_t GetTextureIndex(const Ref<Font>& font);

    /// Authored path -> absolute path. Returns an empty path when nothing resolves.
    static std::filesystem::path ResolveFontPath(const std::string& authoredPath);
    /// Authored (project- or engine-relative) path for a resolved absolute path.
    static std::string MakeAuthoredPath(const std::filesystem::path& absolutePath);

    static std::string DefaultFontPath();

    /// Font files discoverable by the editor font picker, as authored paths.
    static std::vector<std::string> DiscoverFontFiles();

    UIFontLibrary(const UIFontLibrary&) = delete;
    UIFontLibrary& operator=(const UIFontLibrary&) = delete;

private:
    UIFontLibrary() = default;
    ~UIFontLibrary() = default;

    static double SelectEmSize(float fontSize);

    struct Entry
    {
        Ref<Font> Font = nullptr;
        uint32_t TextureIndex = 0;
    };

    TextureManager* m_TextureManager = nullptr;
    std::unordered_map<std::string, Entry> m_Entries;
    std::unordered_map<std::string, std::filesystem::path> m_ResolvedPaths;
};

}
