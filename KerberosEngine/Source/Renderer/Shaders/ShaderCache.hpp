#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Kerberos::ShaderCache
{
	inline constexpr uint32_t ManifestFormatVersion = 1;

	/// Sidecar metadata written next to every cached .spv.
	/// Dependencies are the complete transitive set reported by Slang: the entry source,
	/// files pulled in via #include, and imported modules.
	struct CacheManifest
	{
		uint32_t FormatVersion = ManifestFormatVersion;
		std::string CompilerBuildTag;
		std::vector<std::filesystem::path> Dependencies;
	};

	std::filesystem::path GetSpirvPath(std::string_view shaderName);
	std::filesystem::path GetManifestPath(const std::filesystem::path& spirvPath);

	/// Returns true when the cached SPIR-V exists, is structurally valid, was produced by the
	/// current Slang build, and is newer than every file in its dependency manifest.
	bool IsFresh(const std::filesystem::path& spirvPath);

	/// Reads a cached SPIR-V artifact. Throws std::runtime_error when the file cannot be read.
	std::vector<uint32_t> ReadSpirv(const std::filesystem::path& spirvPath);

	/// Writes the SPIR-V artifact and its dependency manifest atomically. 
	/// Returns false and logs on failure.
	/// A partial write can never be observed as fresh.
	bool WriteArtifacts(const std::filesystem::path& spirvPath,
						std::span<const uint32_t> spirv,
						std::span<const std::filesystem::path> dependencies,
						std::string_view compilerBuildTag);
}
