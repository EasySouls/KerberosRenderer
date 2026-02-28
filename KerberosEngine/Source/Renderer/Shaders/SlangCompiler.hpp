#pragma once

#include "ShaderEntryPoints.hpp"

#include <vector>
#include <source_location>

namespace Kerberos
{
	class CompilationFailedException : public std::runtime_error
	{
	public:
		explicit CompilationFailedException(const std::string& message)
			: std::runtime_error(message), Location(std::source_location::current())
		{
		}

		std::source_location Location;
	};

	class SlangCompiler
	{
	public:
		
		/**
		 * Compiles a Slang shader file to SPIR-V bytecode.
		 * If entryPoints is empty, it will attempt to compile all entry points defined in the file.
		 * @param filepath The path to the Slang shader file.
		 * @param entryPoints A list of entry points to compile. If empty, all entry points will be compiled.
		 * @return A vector containing the compiled SPIR-V bytecode.
		 */
		static std::vector<uint32_t> CompileToSpirv(const std::filesystem::path& filepath, const std::vector<ShaderEntryPoint>& entryPoints = {});
	};
}