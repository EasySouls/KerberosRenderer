#pragma once

#include "Assets/Formats/NativeAssetFormat.hpp"

#include <filesystem>

namespace Kerberos
{
    class GltfSceneImporter
    {
    public:
        /// Parses a glTF/GLB file and writes a deterministic CPU-only scene cache.
        /// @param source The path to the glTF/GLB file.
        /// @param outputDirectory The directory where the scene cache will be written.
        /// @param output Optional pointer to a manifest to populate with the import results.
        static bool Import(const std::filesystem::path& source,
            const std::filesystem::path& outputDirectory,
            GltfSceneManifest* output = nullptr);
    };
}
