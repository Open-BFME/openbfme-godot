// OpenBFME. GPL-3.0.
#pragma once

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

// Disk paths and environment values at engine boundaries are UTF-8, never the Windows ANSI code page.
namespace PathIO
{
std::filesystem::path native(const std::string &utf8);
std::string environment(const char *name);
std::FILE *open(const std::string &utf8, const char *mode);
// A path must survive the line-oriented install-paths.cfg format without truncation.
bool checkInstallPath(const std::string &path, std::string &error);
// Windows ignores the CRT's ANSI argv; the callback receives UTF-8 with argv[argc] == nullptr.
int runMain(int argc, char **argv, int (*entry)(int, char **));
#ifdef _WIN32
std::wstring widen(const std::string &utf8);
std::string narrow(const std::wstring &wide);
std::vector<std::string> windowsArguments(const wchar_t *commandLine);
#endif
}
