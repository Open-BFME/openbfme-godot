// OpenBFME. GPL-3.0.
#include "GameClient/CreateAHeroStats.h"
#include "Common/PathIO.h"
#include <filesystem>
#include <fstream>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace
{
bool fail(std::string *error, const std::string &message)
{
	if (error) *error = message;
	return false;
}
bool safeID(const std::string &id)
{
	if (id.empty() || id == "." || id == "..") return false;
	for (unsigned char c : id)
		if (!(c >= '0' && c <= '9') && !(c >= 'A' && c <= 'Z') && !(c >= 'a' && c <= 'z') && c != '_' && c != '-') return false;
	return true;
}
}
std::uint32_t CreateAHeroStats::get(const std::string &name) const
{
	auto i = counters.find(name);
	return i == counters.end() ? 0 : i->second;
}
bool CreateAHeroStats::load(const std::string &dir, const std::string &id, std::string *error)
{
	if (dir.empty() || !safeID(id)) return fail(error, "Create-a-Hero statistics: invalid profile folder or hero id");
	const auto path = PathIO::native(dir) / std::filesystem::u8path(id);
	std::error_code ec;
	const bool exists = std::filesystem::exists(path, ec);
	if (ec) return fail(error, "Create-a-Hero statistics: " + ec.message());
	if (!exists) { counters.clear(); return true; }
	if (!std::filesystem::is_regular_file(path, ec) || ec) return fail(error, "Cannot read hero statistics: " + path.u8string());
	std::ifstream in(path);
	if (!in) return fail(error, "Cannot read hero statistics: " + path.u8string());
	CreateAHeroCounters loaded;
	std::string line;
	while (std::getline(in, line))
	{
		if (line.find_first_not_of(" \t\r") == std::string::npos) continue;
		// RW 0x80B3D5..0x80B479 trims and splits at '=', then calls
		// MSVCR71 atoi. Its numeric prefix and 32-bit wrap are intentional.
		const auto equal = line.find('=');
		std::string key = line.substr(0, equal);
		const auto first = key.find_first_not_of(" \t\r");
		if (first == std::string::npos) continue;
		key = key.substr(first, key.find_last_not_of(" \t\r") - first + 1);
		const std::string value = equal == std::string::npos ? "" : line.substr(equal + 1);
		size_t i = value.find_first_not_of(" \t\r\n\v\f");
		if (i == std::string::npos) i = value.size();
		bool negative = false;
		if (i < value.size() && (value[i] == '+' || value[i] == '-')) negative = value[i++] == '-';
		std::uint32_t count = 0;
		while (i < value.size() && value[i] >= '0' && value[i] <= '9') count = count * 10u + (std::uint32_t)(value[i++] - '0');
		if (negative) count = 0u - count;
		loaded[key] = count;
	}
	if (in.bad()) return fail(error, "Cannot read hero statistics: " + path.u8string());
	counters = std::move(loaded);
	return true;
}
bool CreateAHeroStats::save(const std::string &dir, const std::string &id, std::string *error) const
{
	if (dir.empty() || !safeID(id)) return fail(error, "Create-a-Hero statistics: invalid profile folder or hero id");
	const auto folder = PathIO::native(dir);
	std::error_code ec;
	std::filesystem::create_directories(folder, ec);
	if (ec) return fail(error, "Cannot create hero statistics folder: " + ec.message());
	const auto path = folder / std::filesystem::u8path(id);
	auto temp = path;
	temp += ".tmp";
	{
		std::ofstream out(temp, std::ios::trunc);
		if (!out) return fail(error, "Cannot write hero statistics: " + temp.u8string());
		for (const auto &kv : counters) out << kv.first << " = " << (std::int32_t)kv.second << '\n';
		out.close();
		if (!out) return fail(error, "Cannot write hero statistics: " + temp.u8string());
	}
#ifdef _WIN32
	if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
		return fail(error, "Cannot replace hero statistics: " + path.u8string());
#else
	std::filesystem::rename(temp, path, ec);
	if (ec) return fail(error, "Cannot replace hero statistics: " + ec.message());
#endif
	return true;
}
