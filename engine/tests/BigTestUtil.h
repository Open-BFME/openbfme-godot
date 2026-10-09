// OpenBFME unit tests. GPL-3.0.
// Builds BIG archives byte by byte so tests never need retail files.

#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace bigtest
{

inline void putBE32(std::vector<std::uint8_t> &out, std::uint32_t v)
{
	out.push_back((std::uint8_t)(v >> 24));
	out.push_back((std::uint8_t)(v >> 16));
	out.push_back((std::uint8_t)(v >> 8));
	out.push_back((std::uint8_t)v);
}

inline void putLE32(std::vector<std::uint8_t> &out, std::uint32_t v)
{
	out.push_back((std::uint8_t)v);
	out.push_back((std::uint8_t)(v >> 8));
	out.push_back((std::uint8_t)(v >> 16));
	out.push_back((std::uint8_t)(v >> 24));
}

// files: (path inside archive, contents). declaredSize < 0 writes the real size.
inline std::vector<std::uint8_t> makeBig(const char *magic, const std::vector<std::pair<std::string, std::string>> &files,
	long long declaredSize = -1)
{
	std::uint32_t dirSize = 0;
	for (const auto &f : files)
	{
		dirSize += 8 + (std::uint32_t)f.first.size() + 1;
	}
	std::uint32_t headerEnd = 16 + dirSize;
	std::uint32_t total = headerEnd;
	for (const auto &f : files)
	{
		total += (std::uint32_t)f.second.size();
	}

	std::vector<std::uint8_t> out(magic, magic + 4);
	putLE32(out, declaredSize < 0 ? total : (std::uint32_t)declaredSize);
	putBE32(out, (std::uint32_t)files.size());
	putBE32(out, headerEnd);
	std::uint32_t offset = headerEnd;
	for (const auto &f : files)
	{
		putBE32(out, offset);
		putBE32(out, (std::uint32_t)f.second.size());
		out.insert(out.end(), f.first.begin(), f.first.end());
		out.push_back(0);
		offset += (std::uint32_t)f.second.size();
	}
	for (const auto &f : files)
	{
		out.insert(out.end(), f.second.begin(), f.second.end());
	}
	return out;
}

inline std::string asString(const std::vector<std::uint8_t> &v)
{
	return std::string(v.begin(), v.end());
}

} // namespace bigtest
