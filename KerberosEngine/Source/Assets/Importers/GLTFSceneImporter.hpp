#pragma once

#include "Assets/Formats/NativeAssetFormat.hpp"

#include <filesystem>

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
        static bool Import(const std::filesystem::path& source,
            const std::filesystem::path& outputDirectory,
            const std::filesystem::path& namespacePath,
            GltfSceneManifest* output = nullptr);
    };
}
