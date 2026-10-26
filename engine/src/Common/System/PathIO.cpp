// OpenBFME. GPL-3.0.
#include "Common/PathIO.h"

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace PathIO
{
#ifdef _WIN32
std::wstring widen(const std::string &utf8)
{
	if (utf8.empty()) return {};
	const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), (int)utf8.size(), nullptr, 0);
	if (!n) throw std::invalid_argument("invalid UTF-8 path");
	std::wstring out(n, L'\0');
	if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), (int)utf8.size(), out.data(), n))
		throw std::invalid_argument("invalid UTF-8 path");
	return out;
}

std::string narrow(const std::wstring &wide)
{
	if (wide.empty()) return {};
	const int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), (int)wide.size(), nullptr, 0, nullptr, nullptr);
	if (!n) throw std::invalid_argument("invalid UTF-16 path");
	std::string out(n, '\0');
	if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), (int)wide.size(), out.data(), n, nullptr, nullptr))
		throw std::invalid_argument("invalid UTF-16 path");
	return out;
}

std::vector<std::string> windowsArguments(const wchar_t *commandLine)
{
	int count = 0;
	const std::unique_ptr<wchar_t *, decltype(&LocalFree)> wide(CommandLineToArgvW(commandLine, &count), &LocalFree);
	if (!wide) throw std::runtime_error("cannot read UTF-16 command line");
	std::vector<std::string> args;
	args.reserve(count);
	for (int i = 0; i < count; ++i) args.push_back(narrow(wide.get()[i]));
	return args;
}
#endif

int runMain(int argc, char **argv, int (*entry)(int, char **))
{
#ifdef _WIN32
	std::vector<std::string> args;
	try
	{
		args = windowsArguments(GetCommandLineW());
	}
	catch (const std::exception &error)
	{
		std::fprintf(stderr, "command line: %s\n", error.what());
		return 1;
	}
	std::vector<char *> pointers;
	pointers.reserve(args.size() + 1);
	for (auto &arg : args) pointers.push_back(arg.data());
	pointers.push_back(nullptr);
	return entry((int)args.size(), pointers.data());
#else
	return entry(argc, argv);
#endif
}

std::filesystem::path native(const std::string &utf8)
{
#ifdef _WIN32
	std::wstring path = widen(utf8);
	std::replace(path.begin(), path.end(), L'/', L'\\');
	// Do not depend on the host executable's longPathAware manifest (the test runner has none).
	if (path.size() >= 248 && path.compare(0, 4, L"\\\\?\\") != 0)
	{
		const DWORD n = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
		if (!n) throw std::invalid_argument("cannot resolve path: " + utf8);
		std::vector<wchar_t> full(n);
		const DWORD length = GetFullPathNameW(path.c_str(), n, full.data(), nullptr);
		if (!length || length >= n) throw std::invalid_argument("cannot resolve path: " + utf8);
		path.assign(full.data(), length);
		path = path.compare(0, 2, L"\\\\") == 0 ? L"\\\\?\\UNC\\" + path.substr(2) : L"\\\\?\\" + path;
	}
	return std::filesystem::path(path);
#else
	return std::filesystem::u8path(utf8);
#endif
}

std::string environment(const char *name)
{
#ifdef _WIN32
	const auto key = widen(name);
	for (;;)
	{
		const DWORD n = GetEnvironmentVariableW(key.c_str(), nullptr, 0);
		if (!n) return {};
		std::vector<wchar_t> value(n);
		const DWORD length = GetEnvironmentVariableW(key.c_str(), value.data(), n);
		if (length >= n) continue;
		return narrow(std::wstring(value.data(), length));
	}
#else
	const char *value = std::getenv(name);
	return value ? value : "";
#endif
}

std::FILE *open(const std::string &utf8, const char *mode)
{
#ifdef _WIN32
	return _wfopen(native(utf8).c_str(), widen(mode).c_str());
#else
	return std::fopen(utf8.c_str(), mode);
#endif
}

bool checkInstallPath(const std::string &path, std::string &error)
{
	if (path.empty() || path.find_first_of(std::string("\r\n\0", 3)) != std::string::npos)
	{
		error = "folder cannot be stored in install-paths.cfg (empty, NUL or line break): " + path;
		return false;
	}
#ifdef _WIN32
	// Win32 and Godot normalize these names differently. Refuse rather than open a different folder.
	std::filesystem::path disk;
	try
	{
		disk = std::filesystem::path(widen(path));
	}
	catch (const std::invalid_argument &)
	{
		error = "folder is not valid UTF-8: " + path;
		return false;
	}
	for (const auto &part : disk)
	{
		const auto s = part.u8string();
		if (s != "." && s != ".." && !s.empty() && (s.back() == ' ' || s.back() == '.'))
		{
			error = "folder has a trailing space or dot unsupported by Windows: " + path;
			return false;
		}
	}
#endif
	return true;
}
}
