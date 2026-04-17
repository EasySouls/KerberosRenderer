#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Kerberos::Process 
{
	struct ProcessResult
	{
		bool Started = false;
		bool Succeeded = false;
		uint32_t ExitCode = UINT32_MAX;
		uint32_t ErrorCode = 0;
		std::string ErrorMessage;
	};

	[[nodiscard]]
	ProcessResult Execute(const std::filesystem::path& executable, const std::vector<std::string>& arguments = {});
}
