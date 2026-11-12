#pragma once

#include "ShaderEntryPoints.hpp"

#include <cstdint>
#include <filesystem>
#include <source_location>
#include <stdexcept>
#include <string>
#include <vector>

namespace Kerberos {

class CompilationFailedException : public std::runtime_error
{
public:
	explicit CompilationFailedException(const std::string& message)
		: std::runtime_error(message), Location(std::source_location::current())
	{
	}

	std::source_location Location;
};

/**
 * Result of compiling a Slang entry module.
 * Dependencies contains every file the module needs, including the entry source
 * itself, transitively #included files, and imported modules.
 */
struct CompiledShader
{
	std::vector<uint32_t> Spirv;
	std::vector<std::filesystem::path> Dependencies;
	std::string CompilerBuildTag;
};

class SlangCompiler
{
public:
	
	/**
	 * Compiles a Slang shader file to SPIR-V bytecode.
	 * If entryPoints is empty, it will attempt to compile all entry points defined in the file.
	 * @param filepath The path to the Slang shader file.
	 * @param entryPoints A list of entry points to compile. If empty, all entry points will be compiled.
     * @throws CompilationFailedException if the compilation fails.
	 * @return The compiled SPIR-V bytecode plus the transitive source dependency list.
	 */
	static CompiledShader CompileToSpirv(const std::filesystem::path& filepath, const std::vector<ShaderEntryPoint>& entryPoints = {});

	/// Build tag of the Slang library backing the compiler. Used to invalidate caches across SDK upgrades.
	static std::string GetCompilerBuildTag();
};

}
