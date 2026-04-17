#pragma once

#include "Utils/Process.hpp"

namespace Kerberos::KtxConversion
{
	[[nodiscard]]
	inline Process::ProcessResult ConvertKtxToKtx2(const std::filesystem::path& sourceFilepath, const bool forceRebuild = false)
	{
		std::vector<std::string> arguments;
		if (forceRebuild)
		{
			arguments.emplace_back("-f");
		}

		arguments.emplace_back("-b");
		arguments.emplace_back(sourceFilepath.string());
		return Process::Execute("ktx2ktx2", arguments);
	}
}
