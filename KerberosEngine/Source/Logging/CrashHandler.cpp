#include "CrashHandler.hpp"
#include "Log.hpp"

#ifdef KBR_PLATFORM_WINDOWS
#include <windows.h>
#include <DbgHelp.h>
#pragma comment(lib, "Dbghelp.lib")
#elifdef KBR_PLATFORM_LINUX
#include <csignal>
#include <execinfo>
#else
#error No crash handler implemented for this OS!
#endif

#include <cstdlib>
#include <stacktrace>

static void LogCrashAndStackTrace(const std::string& crashReason) 
{
    KBR_CORE_CRITICAL("========================================");
    KBR_CORE_CRITICAL("ENGINE CRASH DETECTED!");
    KBR_CORE_CRITICAL("Reason: {}", crashReason);
    KBR_CORE_CRITICAL("--- Stack Trace ---");

    const auto trace = std::stacktrace::current();

    KBR_CORE_CRITICAL("\n{}", std::to_string(trace));
    KBR_CORE_CRITICAL("========================================");

    Kerberos::Log::Flush();
}

#ifdef KBR_PLATFORM_WINDOWS

static void GenerateMiniDump(EXCEPTION_POINTERS* exceptionInfo)
{
    KBR_CORE_INFO("Generating Minidump...");

    const HANDLE hFile = CreateFileA("KerberosEngineCrash.dmp",
                               GENERIC_READ | GENERIC_WRITE,
                               0, nullptr, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr);

    if (hFile != INVALID_HANDLE_VALUE) 
    {
        MINIDUMP_EXCEPTION_INFORMATION dumpInfo;
        dumpInfo.ThreadId = GetCurrentThreadId();
        dumpInfo.ExceptionPointers = exceptionInfo;
        dumpInfo.ClientPointers = FALSE;

        const bool success = MiniDumpWriteDump(
            GetCurrentProcess(),
            GetCurrentProcessId(),
            hFile,
            MiniDumpWithDataSegs,
            &dumpInfo,
            nullptr,
            nullptr
        );

        if (success) {
            KBR_CORE_INFO("Minidump saved to KerberosEngineCrash.dmp");
        }
        else {
            KBR_CORE_ERROR("Failed to write Minidump. Error Code: {}", GetLastError());
        }
        CloseHandle(hFile);
    }
    else {
        KBR_CORE_ERROR("Failed to create Minidump file.");
    }

    Kerberos::Log::Flush();
}

static LONG WINAPI UnhandledExceptionHandler(EXCEPTION_POINTERS* exceptionInfo)
{
    const std::string reason = std::format("Exception Code: 0x{:X}",
                                     exceptionInfo->ExceptionRecord->ExceptionCode);
    LogCrashAndStackTrace(reason);

    GenerateMiniDump(exceptionInfo);

	return EXCEPTION_EXECUTE_HANDLER;
}

#endif

#ifdef KBR_PLATFORM_LINUX

static void SignalHandler(const int signal) 
{
    const char* reason = "Unknown";
    switch (signal) {
        case SIGSEGV: reason = "SIGSEGV (Segmentation Fault)"; break;
        case SIGABRT: reason = "SIGABRT (Abort)"; break;
        case SIGFPE:  reason = "SIGFPE (Floating Point Exception)"; break;
        case SIGILL:  reason = "SIGILL (Illegal Instruction)"; break;
    }

    LogCrashAndStackTrace(reason);

    std::signal(signal, SIG_DFL);
    std::raise(signal);
}

#endif

namespace Kerberos
{
    void CrashHandler::Init()
    {
#ifdef KBR_PLATFORM_WINDOWS
	    SetUnhandledExceptionFilter(UnhandledExceptionHandler);
#else
        std::signal(SIGSEGV, SignalHandler);
        std::signal(SIGABRT, SignalHandler);
        std::signal(SIGFPE, SignalHandler);
        std::signal(SIGILL, SignalHandler);
#endif
        KBR_CORE_INFO("CrashHandler initialized successfully.");
    }
}