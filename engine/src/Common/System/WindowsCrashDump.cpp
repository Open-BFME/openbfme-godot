// OpenBFME. GPL-3.0.
#include "Common/WindowsCrashDump.h"
#include "Common/PathIO.h"

// lane CRASHDIALOG-1: on every platform (the unit test), used only by the Windows handler below
std::wstring WindowsCrashDump::crashDialogCommandLine(const std::wstring &executable, const std::vector<std::wstring> &extraArguments, unsigned long pid)
{
	std::wstring line = L"\"" + executable + L"\"";
	for (const std::wstring &argument : extraArguments)
	{
		line += argument.find(L' ') == std::wstring::npos ? L" " + argument : L" \"" + argument + L"\"";
	}
	// a small window on the compatibility renderer, no audio: the crash may be the graphics driver's or the audio device's
	line += L" --windowed --resolution 720x600 --rendering-method gl_compatibility --rendering-driver opengl3 --audio-driver Dummy";
	return line + L" -- --crash-report=" + std::to_wstring(pid);
}

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>
#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <exception>
#include <filesystem>

namespace WindowsCrashDump
{
namespace
{
HANDLE requestEvent = nullptr, completeEvent = nullptr, writer = nullptr;
PVOID vectored = nullptr;
LPTOP_LEVEL_EXCEPTION_FILTER previousFilter = nullptr;
std::terminate_handler previousTerminate = nullptr;
using SignalHandler = void (*)(int);
SignalHandler previousAbort = SIG_DFL;
std::wstring dumpPath;
std::wstring dialogExecutable, dialogCommandLine; // lane CRASHDIALOG-1: ready before any crash
volatile LONG captureStarted = 0;
volatile LONG stopping = 0;
DWORD writerId = 0, faultThread = 0;
CONTEXT faultContext;
EXCEPTION_RECORD faultRecord;
EXCEPTION_POINTERS faultPointers{&faultRecord, &faultContext};
bool installed = false;

void diagnostic(const char *message)
{
	DWORD written;
	WriteFile(GetStdHandle(STD_ERROR_HANDLE), message, (DWORD)std::char_traits<char>::length(message), &written, nullptr);
	OutputDebugStringA(message);
}

DWORD WINAPI writeDump(void *)
{
	if (WaitForSingleObject(requestEvent, INFINITE) != WAIT_OBJECT_0 || InterlockedCompareExchange(&stopping, 0, 0))
		return 0;
	HANDLE file = CreateFileW(dumpPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
	DWORD error = file == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
	if (file != INVALID_HANDLE_VALUE)
	{
		MINIDUMP_EXCEPTION_INFORMATION exception{faultThread, &faultPointers, FALSE};
		// No heap/full-memory dump: even the small dump contains private process data.
		if (!MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
			static_cast<MINIDUMP_TYPE>(MiniDumpNormal | MiniDumpWithThreadInfo), &exception, nullptr, nullptr))
			error = GetLastError();
		if (!CloseHandle(file) && !error)
			error = GetLastError();
		if (error)
			DeleteFileW(dumpPath.c_str());
	}
	char message[160];
	std::snprintf(message, sizeof(message), "CRASH local minidump %s; thread=%lu code=0x%08lX error=0x%08lX\n",
		error ? "FAILED" : "written", faultThread, faultRecord.ExceptionCode, error);
	diagnostic(message);
	if (!dialogCommandLine.empty())
	{
		// lane CRASHDIALOG-1: the dialog waits for this process to end (the crash handlers after this one still write the log), then reads the crashed run's log
		STARTUPINFOW startup{};
		startup.cb = sizeof(startup);
		PROCESS_INFORMATION started{};
		if (CreateProcessW(dialogExecutable.c_str(), dialogCommandLine.data(), nullptr, nullptr, FALSE, DETACHED_PROCESS, nullptr, nullptr, &startup, &started))
		{
			CloseHandle(started.hThread);
			CloseHandle(started.hProcess);
			diagnostic("CRASH the crash dialog started\n");
		}
		else
		{
			diagnostic("CRASH the crash dialog did not start\n");
		}
	}
	SetEvent(completeEvent);
	return 0;
}

void capture(EXCEPTION_POINTERS *exception)
{
	// One writer and one dump per process. Never wait on a fault in the writer itself.
	if (GetCurrentThreadId() == writerId)
		return;
	if (InterlockedCompareExchange(&captureStarted, 1, 0) == 0)
	{
		faultThread = GetCurrentThreadId();
		faultContext = *exception->ContextRecord;
		faultRecord = *exception->ExceptionRecord;
		faultRecord.ExceptionRecord = nullptr;
		SetEvent(requestEvent);
	}
	// A bounded wait: corrupted locks/loader state may prevent even the reserved thread
	// from completing. The copied exception context remains valid after this returns.
	if (WaitForSingleObject(completeEvent, 15000) != WAIT_OBJECT_0)
		diagnostic("CRASH local minidump timed out\n");
}

LONG WINAPI unhandled(EXCEPTION_POINTERS *exception)
{
	capture(exception);
	// Preserve host reporting, but only after our local dump has been attempted.
	return previousFilter ? previousFilter(exception) : EXCEPTION_CONTINUE_SEARCH;
}

LONG WINAPI fatalException(EXCEPTION_POINTERS *exception)
{
	const DWORD code = exception->ExceptionRecord->ExceptionCode;
	// A noncontinuable C++ throw is NOT necessarily fatal. Require both the flag and
	// the explicit fatal-code allowlist; ignore first-chance AVs, breakpoints, etc.
	if ((exception->ExceptionRecord->ExceptionFlags & EXCEPTION_NONCONTINUABLE) &&
		(code == 0xC0000409u || code == 0xC0000602u || code == 0xC0000374u ||
			code == EXCEPTION_NONCONTINUABLE_EXCEPTION))
		capture(exception);
	return EXCEPTION_CONTINUE_SEARCH;
}

void captureTermination()
{
	CONTEXT context;
	RtlCaptureContext(&context);
	EXCEPTION_RECORD record{};
	record.ExceptionCode = 0x40000015u; // STATUS_FATAL_APP_EXIT: synthetic abort/terminate context
	record.ExceptionFlags = EXCEPTION_NONCONTINUABLE;
#if defined(_M_X64) || defined(__x86_64__)
	record.ExceptionAddress = reinterpret_cast<void *>(context.Rip);
#elif defined(_M_IX86) || defined(__i386__)
	record.ExceptionAddress = reinterpret_cast<void *>(context.Eip);
#elif defined(_M_ARM64) || defined(__aarch64__)
	record.ExceptionAddress = reinterpret_cast<void *>(context.Pc);
#endif
	EXCEPTION_POINTERS pointers{&record, &context};
	capture(&pointers);
}

void aborted(int signal)
{
	// UCRT set_terminate is thread-local, but its SIGABRT handler is process-wide.
	// Default worker termination reaches here before abort's non-interceptable
	// __fastfail. This is a terminal CRT path, not first-chance exception capture.
	captureTermination();
	if (previousAbort != SIG_DFL && previousAbort != SIG_IGN && previousAbort != SIG_ERR)
		previousAbort(signal);
}

[[noreturn]] void terminated()
{
	captureTermination();
	TerminateProcess(GetCurrentProcess(), 0x40000015u);
	std::abort();
}
}

bool install(const std::string &directory, std::string &error)
{
	if (installed)
	{
		reassertHandlers();
		return true;
	}
	std::error_code ec;
	const auto path = PathIO::native(directory);
	std::filesystem::create_directories(path, ec);
	if (ec)
	{
		error = "cannot create local dump directory " + directory + ": " + ec.message();
		return false;
	}
	FILETIME now;
	GetSystemTimeAsFileTime(&now);
	dumpPath = (path / (L"openbfme-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
		std::to_wstring((static_cast<unsigned long long>(now.dwHighDateTime) << 32) | now.dwLowDateTime) + L".dmp")).wstring();
	requestEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	completeEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	if (requestEvent && completeEvent)
		writer = CreateThread(nullptr, 0, writeDump, nullptr, 0, &writerId);
	if (writer)
		vectored = AddVectoredExceptionHandler(1, fatalException);
	if (!requestEvent || !completeEvent || !writer || !vectored)
	{
		error = "cannot initialize local dump handler: Win32 " + std::to_string(GetLastError());
		uninstall();
		return false;
	}
	previousAbort = std::signal(SIGABRT, aborted);
	if (previousAbort == SIG_ERR)
	{
		error = "cannot initialize local dump SIGABRT handler";
		uninstall();
		return false;
	}
	previousFilter = SetUnhandledExceptionFilter(unhandled);
	previousTerminate = std::set_terminate(terminated);
	installed = true;
	return true;
}

bool armCrashDialog(const std::vector<std::string> &extraArguments, std::string &error)
{
	if (!installed)
	{
		error = "the local dump handler is not installed";
		return false;
	}
	std::wstring executable(MAX_PATH, L'\0');
	for (;;)
	{
		const DWORD length = GetModuleFileNameW(nullptr, executable.data(), (DWORD)executable.size());
		if (length == 0)
		{
			error = "cannot read this program's path: Win32 " + std::to_string(GetLastError());
			return false;
		}
		if (length < executable.size())
		{
			executable.resize(length);
			break;
		}
		executable.resize(executable.size() * 2);
	}
	std::vector<std::wstring> extra;
	for (const std::string &argument : extraArguments)
	{
		extra.push_back(PathIO::native(argument).wstring());
	}
	dialogExecutable = executable;
	dialogCommandLine = crashDialogCommandLine(executable, extra, GetCurrentProcessId());
	return true;
}

bool reassertHandlers()
{
	if (!installed)
		return false;
	const auto replaced = SetUnhandledExceptionFilter(unhandled);
	const auto terminate = std::set_terminate(terminated);
	const auto abort = std::signal(SIGABRT, aborted);
	const bool changed = replaced != unhandled || terminate != terminated || abort != aborted;
	if (replaced != unhandled)
		previousFilter = replaced;
	if (terminate != terminated)
		previousTerminate = terminate;
	if (abort != aborted && abort != SIG_ERR)
		previousAbort = abort;
	if (changed)
		diagnostic("CRASH a crash handler was replaced; restored OpenBFME handlers\n");
	return changed;
}

void uninstall()
{
	if (installed)
	{
		const auto current = SetUnhandledExceptionFilter(previousFilter);
		if (current != unhandled)
			SetUnhandledExceptionFilter(current);
		const auto terminate = std::set_terminate(previousTerminate);
		if (terminate != terminated)
			std::set_terminate(terminate);
		const auto abort = std::signal(SIGABRT, previousAbort);
		if (abort != aborted && abort != SIG_ERR)
			std::signal(SIGABRT, abort);
	}
	if (vectored)
		RemoveVectoredExceptionHandler(vectored);
	if (writer)
	{
		InterlockedExchange(&stopping, 1);
		SetEvent(requestEvent);
		WaitForSingleObject(writer, INFINITE);
		CloseHandle(writer);
	}
	if (requestEvent)
		CloseHandle(requestEvent);
	if (completeEvent)
		CloseHandle(completeEvent);
	requestEvent = completeEvent = writer = nullptr;
	dialogExecutable.clear();
	dialogCommandLine.clear();
	vectored = nullptr;
	writerId = 0;
	installed = false;
	InterlockedExchange(&captureStarted, 0);
	InterlockedExchange(&stopping, 0);
}
}
#else
namespace WindowsCrashDump
{
bool install(const std::string &, std::string &error)
{
	error = "Windows minidumps are not supported on this platform";
	return false;
}
bool armCrashDialog(const std::vector<std::string> &, std::string &error)
{
	error = "Windows minidumps are not supported on this platform";
	return false;
}
bool reassertHandlers() { return false; }
void uninstall() {}
}
#endif
