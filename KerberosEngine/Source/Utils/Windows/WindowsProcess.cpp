#ifdef KBR_PLATFORM_WINDOWS
#include "kbrpch.hpp"
#include "Utils/Process.hpp"


#include <format>

namespace
{
	std::string GetWindowsErrorMessage(const DWORD errorCode)
	{
		if (errorCode == 0)
		{
			return {};
		}

		LPSTR buffer = nullptr;
		const DWORD length = FormatMessageA(
			FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			nullptr,
			errorCode,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			reinterpret_cast<LPSTR>(&buffer),
			0,
			nullptr);

		if (length == 0 || buffer == nullptr)
		{
			return std::format("Windows error code {}", errorCode);
		}

		std::string message(buffer, length);
		LocalFree(buffer);

		while (!message.empty() && (message.back() == '\r' || message.back() == '\n'))
		{
			message.pop_back();
		}

		return message;
	}

	std::string QuoteWindowsArgument(const std::string& argument)
	{
		if (argument.empty())
		{
			return "\"\"";
		}

		const bool needsQuoting = argument.find_first_of(" \t\"") != std::string::npos;
		if (!needsQuoting)
		{
			return argument;
		}

		std::string quoted;
		quoted.reserve(argument.size() + 2);
		quoted.push_back('"');

		size_t backslashCount = 0;
		for (const char character : argument)
		{
			if (character == '\\')
			{
				++backslashCount;
				continue;
			}

			if (character == '"')
			{
				quoted.append(backslashCount * 2 + 1, '\\');
				quoted.push_back('"');
				backslashCount = 0;
				continue;
			}

			if (backslashCount > 0)
			{
				quoted.append(backslashCount, '\\');
				backslashCount = 0;
			}

			quoted.push_back(character);
		}

		if (backslashCount > 0)
		{
			quoted.append(backslashCount * 2, '\\');
		}

		quoted.push_back('"');
		return quoted;
	}

	std::string BuildCommandLine(const std::string& executable, const std::vector<std::string>& arguments)
	{
		std::string commandLine = QuoteWindowsArgument(executable);
		for (const std::string& argument : arguments)
		{
			commandLine += " ";
			commandLine += QuoteWindowsArgument(argument);
		}

		return commandLine;
	}
}

namespace Kerberos::Process
{
	ProcessResult Execute(const std::filesystem::path& executable, const std::vector<std::string>& arguments)
	{
		ProcessResult result{};
		if (executable.empty())
		{
			result.ErrorMessage = "Process::Execute called with an empty executable path.";
			return result;
		}

		const std::string executablePath = executable.string();
		std::string commandLine = BuildCommandLine(executablePath, arguments);

		STARTUPINFOA startupInfo{};
		startupInfo.cb = sizeof(startupInfo);
		PROCESS_INFORMATION processInfo{};

		const BOOL created = CreateProcessA(
			nullptr,
			commandLine.data(),
			nullptr,
			nullptr,
			FALSE,
			CREATE_NO_WINDOW,
			nullptr,
			nullptr,
			&startupInfo,
			&processInfo);

		if (!created)
		{
			result.ErrorCode = GetLastError();
			result.ErrorMessage = GetWindowsErrorMessage(result.ErrorCode);
			return result;
		}

		result.Started = true;
		const DWORD waitResult = WaitForSingleObject(processInfo.hProcess, INFINITE);
		if (waitResult != WAIT_OBJECT_0)
		{
			result.ErrorCode = GetLastError();
			result.ErrorMessage = std::format("WaitForSingleObject failed: {}", GetWindowsErrorMessage(result.ErrorCode));
			CloseHandle(processInfo.hThread);
			CloseHandle(processInfo.hProcess);
			return result;
		}

		DWORD exitCode = 0;
		if (!GetExitCodeProcess(processInfo.hProcess, &exitCode))
		{
			result.ErrorCode = GetLastError();
			result.ErrorMessage = std::format("GetExitCodeProcess failed: {}", GetWindowsErrorMessage(result.ErrorCode));
			CloseHandle(processInfo.hThread);
			CloseHandle(processInfo.hProcess);
			return result;
		}

		result.ExitCode = exitCode;
		result.Succeeded = (exitCode == 0);

		CloseHandle(processInfo.hThread);
		CloseHandle(processInfo.hProcess);
		return result;
	}
}
#endif
