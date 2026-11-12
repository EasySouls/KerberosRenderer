#include "ShaderCache.hpp"

#include "Core/EnginePaths.hpp"
#include "IO.hpp"
#include "SlangCompiler.hpp"
#include "Profiling/TracyInstrumentation.hpp"

#include <cstring>
#include <fstream>
#include <optional>
#include <sstream>
#include <system_error>

import Kerberos;

namespace Kerberos::ShaderCache {

namespace {

constexpr uint32_t SpirvMagicNumber = 0x07230203u;
constexpr std::string_view ManifestHeader = "kerberos-shader-cache";
constexpr std::string_view CompilerKey = "compiler ";
constexpr std::string_view DependencyKey = "dep ";

std::filesystem::path NormalizePath(const std::filesystem::path& path)
{
	std::error_code ec;
	const auto canonical = std::filesystem::weakly_canonical(path, ec);
	return ec ? path.lexically_normal() : canonical;
}

/// Stores dependencies relative to the shader source directory so a moved project does not
/// invalidate the manifest. Paths outside the source tree stay absolute.
std::filesystem::path MakeManifestPath(const std::filesystem::path& path)
{
    KBR_TRACY_FUNCTION();
    
	const auto canonical = NormalizePath(path);
	const auto shaderRoot = NormalizePath(EnginePaths::Shaders());

	std::error_code ec;
	const auto relative = std::filesystem::relative(canonical, shaderRoot, ec);
	if (ec || relative.empty())
		return canonical;

	for (const auto& part : relative)
	{
		if (part == "..")
			return canonical;
	}

	return relative;
}

std::filesystem::path ResolveManifestPath(const std::filesystem::path& path)
{
    KBR_TRACY_FUNCTION();
    
	if (path.is_absolute())
		return path;
	return EnginePaths::Shaders() / path;
}

bool HasValidSpirvHeader(const std::filesystem::path& spirvPath)
{
    KBR_TRACY_FUNCTION();
    
	std::error_code ec;
	const auto size = std::filesystem::file_size(spirvPath, ec);
	if (ec || size == 0 || size % sizeof(uint32_t) != 0)
		return false;

	std::ifstream file(spirvPath, std::ios::binary);
	if (!file.is_open())
		return false;

	uint32_t magic = 0;
	file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
	return file.good() && magic == SpirvMagicNumber;
}

bool WriteFileAtomically(const std::filesystem::path& path, const void* data, const size_t size)
{
    KBR_TRACY_FUNCTION();
    
	std::filesystem::path tempPath = path;
	tempPath += ".tmp";

	{
		std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
		if (!file.is_open())
		{
			Log::CoreError("Failed to open shader cache file for writing: {}", tempPath.string());
			return false;
		}

		if (size > 0)
			file.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));

		file.flush();
		if (!file.good())
		{
			Log::CoreError("Failed to write shader cache file: {}", tempPath.string());
			std::error_code ec;
			std::filesystem::remove(tempPath, ec);
			return false;
		}
	}

	std::error_code ec;
	std::filesystem::rename(tempPath, path, ec);
	if (ec)
	{
		// rename() does not reliably replace an existing file on every platform.
		std::error_code removeError;
		std::filesystem::remove(path, removeError);
		ec.clear();
		std::filesystem::rename(tempPath, path, ec);
	}

	if (ec)
	{
		Log::CoreError("Failed to move shader cache file '{}' into place: {}", tempPath.string(), ec.message());
		std::error_code cleanupError;
		std::filesystem::remove(tempPath, cleanupError);
		return false;
	}

	return true;
}

std::optional<CacheManifest> ReadManifest(const std::filesystem::path& manifestPath)
{
    KBR_TRACY_FUNCTION();
    
	std::ifstream file(manifestPath);
	if (!file.is_open())
		return std::nullopt;

	std::string line;
	if (!std::getline(file, line))
		return std::nullopt;

	std::istringstream header(line);
	std::string magic;
	uint32_t version = 0;
	if (!(header >> magic >> version) || magic != ManifestHeader || version != ManifestFormatVersion)
		return std::nullopt;

	CacheManifest manifest;
	manifest.FormatVersion = version;

	while (std::getline(file, line))
	{
		if (line.rfind(CompilerKey, 0) == 0)
			manifest.CompilerBuildTag = line.substr(CompilerKey.size());
		else if (line.rfind(DependencyKey, 0) == 0)
			manifest.Dependencies.push_back(ResolveManifestPath(line.substr(DependencyKey.size())));
	}

	if (manifest.Dependencies.empty())
		return std::nullopt;

	return manifest;
}

}

std::filesystem::path GetSpirvPath(const std::string_view shaderName)
{
	return EnginePaths::ShaderCache() / (std::string(shaderName) + ".spv");
}

std::filesystem::path GetManifestPath(const std::filesystem::path& spirvPath)
{
	std::filesystem::path manifestPath = spirvPath;
	manifestPath += ".deps";
	return manifestPath;
}

bool IsFresh(const std::filesystem::path& spirvPath)
{
    KBR_TRACY_FUNCTION();
    
	std::error_code ec;
	if (!std::filesystem::exists(spirvPath, ec) || ec)
		return false;

	if (!HasValidSpirvHeader(spirvPath))
		return false;

	const auto manifest = ReadManifest(GetManifestPath(spirvPath));
	if (!manifest)
		return false;

	if (manifest->CompilerBuildTag != SlangCompiler::GetCompilerBuildTag())
		return false;

	const auto spirvTime = std::filesystem::last_write_time(spirvPath, ec);
	if (ec)
		return false;

	for (const auto& dependency : manifest->Dependencies)
	{
		if (!std::filesystem::exists(dependency, ec) || ec)
			return false;

		const auto dependencyTime = std::filesystem::last_write_time(dependency, ec);
		if (ec || dependencyTime > spirvTime)
			return false;
	}

	return true;
}

std::vector<uint32_t> ReadSpirv(const std::filesystem::path& spirvPath)
{
    KBR_TRACY_FUNCTION();
    
	const auto bytes = IO::ReadFile(spirvPath);
	std::vector<uint32_t> spirv(bytes.size() / sizeof(uint32_t));
	if (!spirv.empty())
		std::memcpy(spirv.data(), bytes.data(), spirv.size() * sizeof(uint32_t));
	return spirv;
}

bool WriteArtifacts(const std::filesystem::path& spirvPath,
					const std::span<const uint32_t> spirv,
					const std::span<const std::filesystem::path> dependencies,
					const std::string_view compilerBuildTag)
{
    KBR_TRACY_FUNCTION();
    
	std::error_code ec;
	std::filesystem::create_directories(spirvPath.parent_path(), ec);

	if (!WriteFileAtomically(spirvPath, spirv.data(), spirv.size() * sizeof(uint32_t)))
		return false;

	std::ostringstream manifest;
	manifest << ManifestHeader << ' ' << ManifestFormatVersion << '\n';
	manifest << CompilerKey << compilerBuildTag << '\n';
	for (const auto& dependency : dependencies)
		manifest << DependencyKey << MakeManifestPath(dependency).generic_string() << '\n';

	const std::string manifestText = manifest.str();
	return WriteFileAtomically(GetManifestPath(spirvPath), manifestText.data(), manifestText.size());
}

}
