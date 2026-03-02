#include "kbrpch.hpp"
#include "Utils/SystemOperations.hpp"

#include <commdlg.h>
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include "Application.hpp"

namespace Kerberos
{
	std::string FileDialog::OpenFile(const char* filter)
	{
		OPENFILENAMEA ofn;
		CHAR szFile[260] = { 0 };

		ZeroMemory(&ofn, sizeof(ofn));
		ofn.lStructSize = sizeof(ofn);
		ofn.hwndOwner = glfwGetWin32Window(Application::Get().GetWindow());
		ofn.lpstrFile = szFile;
		ofn.nMaxFile = sizeof(szFile);
		ofn.lpstrFilter = filter;
		ofn.nFilterIndex = 1;
		ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
		ofn.lpstrTitle = "Select a file";

		if (GetOpenFileNameA(&ofn) == TRUE)
		{
			return ofn.lpstrFile;
		}

		return {};
	}

	std::string FileDialog::SaveFile(const char* filter)
	{
		OPENFILENAMEA ofn;
		CHAR szFile[260] = { 0 };

		ZeroMemory(&ofn, sizeof(ofn));
		ofn.lStructSize = sizeof(ofn);
		ofn.hwndOwner = glfwGetWin32Window(Application::Get().GetWindow());
		ofn.lpstrFile = szFile;
		ofn.nMaxFile = sizeof(szFile);
		ofn.lpstrFilter = filter;
		ofn.nFilterIndex = 1;
		ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
		ofn.lpstrTitle = "Select a location to save the file to";

		if (GetSaveFileNameA(&ofn) == TRUE)
		{
			return ofn.lpstrFile;
		}

		return {};
	}

	bool FileOperations::OpenFile(const char* path)
	{
		if (!path || path[0] == '\0')
		{
			KBR_CORE_WARN("FileOperations::OpenFile called with an empty path.");
			return false;
		}

		SHELLEXECUTEINFOA sei = { sizeof(sei) };
		sei.fMask = SEE_MASK_NOCLOSEPROCESS;
		sei.hwnd = glfwGetWin32Window(Application::Get().GetWindow());
		sei.lpVerb = "open";
		sei.lpFile = path;
		sei.nShow = SW_SHOWNORMAL;
		if (ShellExecuteExA(&sei))
		{
			/// This will block until the opened process is closed
			WaitForSingleObject(sei.hProcess, INFINITE);
			CloseHandle(sei.hProcess);
			return true;
		}

		DWORD error = GetLastError();
		KBR_CORE_ERROR("Failed to open file: {0}, Error code: {1}", path, error);

		return false;
	}

	bool FileOperations::Delete(const char* path)
	{
		if (!path || path[0] == '\0')
		{
			KBR_CORE_WARN("FileOperations::DeleteFile called with an empty path.");
			return false;
		}
		if (std::remove(path) == 0)
		{
			return true;
		}

		KBR_CORE_ERROR("Failed to delete file: {0}", path);
		return false;
	}

	bool FileOperations::RevealInFileExplorer(const char* path)
	{
		if (!path || path[0] == '\0')
		{
			KBR_CORE_WARN("FileOperations::RevealInFileExplorer called with an empty path.");
			return false;
		}
		std::string params = "/select,\"";
		params += path;
		params += "\"";
		SHELLEXECUTEINFOA sei = { sizeof(sei) };
		sei.fMask = SEE_MASK_NOCLOSEPROCESS;
		sei.hwnd = glfwGetWin32Window(Application::Get().GetWindow());
		sei.lpVerb = "open";
		sei.lpFile = "explorer.exe";
		sei.lpParameters = params.c_str();
		sei.nShow = SW_SHOWNORMAL;
		if (ShellExecuteExA(&sei))
		{
			/// This will block until the opened process is closed
			WaitForSingleObject(sei.hProcess, INFINITE);
			CloseHandle(sei.hProcess);
			return true;
		}
		DWORD error = GetLastError();
		KBR_CORE_ERROR("Failed to reveal file in explorer: {0}, Error code: {1}", path, error);
		return false;
	}
}