#pragma once

#include "Assets/Formats/NativeAssetFormat.hpp"

#include <filesystem>
#include <map>

namespace Kerberos
{
    class GLTFSceneImporter
    {
    public:
        /// Parses a glTF/GLB file and writes a deterministic CPU-only scene cache.
        /// @param source The path to the glTF/GLB file.
        /// @param outputDirectory The directory where the scene cache will be written.
        /// @param namespacePath A source-unique subpath (relative to the assets root)
        ///     under which this source's artifacts are written, so one source never
        ///     overwrites another source's cache entries.
        /// @param output Optional pointer to a manifest to populate with the import results.
        /// @param textureSrgbOverrides Optional user overrides for per-texture sRGB,
        ///     keyed "texture:<imageIndex>". Absent keys fall back to the slot-derived
        ///     default.
        /// @param textureSrgbOut Optional output; receives the materialized per-texture
        ///     sRGB values (override or default) for every imported image.
        static bool Import(const std::filesystem::path& source,
            const std::filesystem::path& outputDirectory,
            const std::filesystem::path& namespacePath,
            GltfSceneManifest* output = nullptr,
            const std::map<std::string, bool>* textureSrgbOverrides = nullptr,
            std::map<std::string, bool>* textureSrgbOut = nullptr);
    };
}
