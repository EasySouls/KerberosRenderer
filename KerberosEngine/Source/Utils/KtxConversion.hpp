#pragma once

#include "Utils/Process.hpp"

#include "Profiling/Profilers.hpp"

namespace Kerberos::KtxConversion
{
	/// Converts a KTX1 source to KTX2.
	/// @param sourceFilepath The KTX1 file to convert.
	/// @param forceRebuild Overwrite an existing output (passes -f).
	/// @param outputFilepath Optional explicit output path. When empty the tool
	/// writes `<source>.ktx2` next to the source. When set the result is written
	/// directly to that path (passes -o).
	[[nodiscard]]
	inline Process::ProcessResult ConvertKtxToKtx2(const std::filesystem::path& sourceFilepath,
		const bool forceRebuild = false, const std::filesystem::path& outputFilepath = {})
	{
		KBR_TRACY_FUNCTION();

		std::vector<std::string> arguments;
		if (forceRebuild)
		{
			arguments.emplace_back("-f");
		}

		arguments.emplace_back("-b");
		if (!outputFilepath.empty())
		{
			arguments.emplace_back("-o");
			arguments.emplace_back(outputFilepath.string());
		}
		arguments.emplace_back(sourceFilepath.string());
		return Process::Execute("ktx2ktx2", arguments);
	}
}
