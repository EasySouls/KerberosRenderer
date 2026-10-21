#ifdef KBR_PLATFORM_WINDOWS
#include "Application.hpp"
#include "Utils/SystemOperations.hpp"

#include <Windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <future>


import Kerberos;

namespace Kerberos
{
	namespace
	{
		std::wstring ToWideString(const std::string& str)
		{
			if (str.empty())
				return {};

			const int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), nullptr, 0);
			std::wstring result(static_cast<size_t>(size), L'\0');
			MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), result.data(), size);
			return result;
		}

		std::string ToUtf8String(const std::wstring& str)
		{
			if (str.empty())
				return {};

			const int size = WideCharToMultiByte(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()),
				nullptr, 0, nullptr, nullptr);
			std::string result(static_cast<size_t>(size), '\0');
			WideCharToMultiByte(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()),
				result.data(), size, nullptr, nullptr);
			return result;
		}

		/// Legacy folder browser. It does not require an STA apartment, so it is
		/// used as a fallback when the calling thread was initialized as MTA.
		std::string SelectFolderLegacy(const char* title)
		{
			BROWSEINFOA browseInfo{};
			browseInfo.hwndOwner = glfwGetWin32Window(Application::Get().GetWindow());
			browseInfo.lpszTitle = title ? title : "Select a folder";
			browseInfo.ulFlags = BIF_RETURNONLYFSDIRS | BIF_EDITBOX;

			PIDLIST_ABSOLUTE itemList = SHBrowseForFolderA(&browseInfo);
			if (!itemList)
				return {};

			char path[MAX_PATH] = {};
			const bool succeeded = SHGetPathFromIDListA(itemList, path) == TRUE;
			CoTaskMemFree(itemList);

			return succeeded ? std::string(path) : std::string();
		}
	}
	std::string FileDialog::OpenFile(const char* filter)
	{
		OPENFILENAMEA ofn;
		CHAR szFile[260]{};

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

	std::string FileDialog::SaveFile(const char* filter, const char* requiredExtension)
	{
		OPENFILENAMEA ofn;
		CHAR szFile[260]{};

		ZeroMemory(&ofn, sizeof(ofn));
		ofn.lStructSize = sizeof(ofn);
		ofn.hwndOwner = glfwGetWin32Window(Application::Get().GetWindow());
		ofn.lpstrFile = szFile;
		ofn.nMaxFile = sizeof(szFile);
		ofn.lpstrFilter = filter;
		ofn.nFilterIndex = 1;
		ofn.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
		ofn.lpstrTitle = "Select a location to save the file to";

		if (requiredExtension)
		{
			ofn.lpstrDefExt = requiredExtension;
		}

		if (GetSaveFileNameA(&ofn) == TRUE)
		{
			return ofn.lpstrFile;
		}

		return {};
	}

	std::string FileDialog::SelectFolder(const char* title)
	{
		/// IFileOpenDialog requires an STA thread. Application initializes the UI
		/// thread as STA (OleInitialize) before audio/COM consumers run, so this
		/// normally returns S_FALSE (already initialized). If the thread is somehow
		/// MTA, use the legacy browser instead of blocking forever in Show.
		const HRESULT comInitResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
		const bool shouldUninitialize = comInitResult == S_OK || comInitResult == S_FALSE;

		if (comInitResult == RPC_E_CHANGED_MODE)
		{
			Log::CoreWarn("FileDialog::SelectFolder called on an MTA thread; using the legacy folder browser.");
			return SelectFolderLegacy(title);
		}

		if (FAILED(comInitResult))
		{
			Log::CoreError("Failed to initialize COM for the folder dialog (HRESULT 0x{:08x}).",
				static_cast<uint32_t>(comInitResult));
			return {};
		}

		std::string selectedPath;

		{
			Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
			HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));

			if (FAILED(hr) || !dialog)
			{
				Log::CoreError("Failed to create the folder selection dialog (HRESULT 0x{:08x}).",
					static_cast<uint32_t>(hr));
			}
			else
			{
				DWORD options = 0;
				if (SUCCEEDED(dialog->GetOptions(&options)))
				{
					dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_PATHMUSTEXIST | FOS_FORCEFILESYSTEM);
				}

				const std::wstring wideTitle = ToWideString(title ? title : "Select a folder");
				dialog->SetTitle(wideTitle.c_str());

				hr = dialog->Show(glfwGetWin32Window(Application::Get().GetWindow()));
				if (SUCCEEDED(hr))
				{
					Microsoft::WRL::ComPtr<IShellItem> item;
					if (SUCCEEDED(dialog->GetResult(&item)) && item)
					{
						PWSTR path = nullptr;
						if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path)
						{
							selectedPath = ToUtf8String(path);
							CoTaskMemFree(path);
						}
					}
				}
				else if (hr != HRESULT_FROM_WIN32(ERROR_CANCELLED))
				{
					Log::CoreError("Folder selection dialog failed (HRESULT 0x{:08x}).", static_cast<uint32_t>(hr));
				}
			}
		}

		if (shouldUninitialize)
			CoUninitialize();

		return selectedPath;
	}

	bool FileOperations::OpenFile(const char* path)
	{
		if (!path || path[0] == '\0')
		{
			Log::CoreWarn("FileOperations::OpenFile called with an empty path.");
			return false;
		}

		SHELLEXECUTEINFOA sei = { .cbSize = sizeof(sei) };
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
		Log::CoreError("Failed to open file: {0}, Error code: {1}", path, error);

		return false;
	}

	bool FileOperations::Delete(const char* path)
	{
		if (!path || path[0] == '\0')
		{
			Log::CoreWarn("FileOperations::DeleteFile called with an empty path.");
			return false;
		}
		if (std::remove(path) == 0)
		{
			return true;
		}

		Log::CoreError("Failed to delete file: {0}", path);
		return false;
	}

	bool FileOperations::RevealInFileExplorer(const char* path)
	{
		if (!path || path[0] == '\0')
		{
			Log::CoreWarn("FileOperations::RevealInFileExplorer called with an empty path.");
			return false;
		}
		std::string params = "/select,\"";
		params += path;
		params += "\"";
		SHELLEXECUTEINFOA sei = { .cbSize = sizeof(sei) };
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
		Log::CoreError("Failed to reveal file in explorer: {0}, Error code: {1}", path, error);
		return false;
	}
}

#endif