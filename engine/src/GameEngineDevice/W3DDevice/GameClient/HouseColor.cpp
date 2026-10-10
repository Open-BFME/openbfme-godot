// OpenBFME: faithful rebuild of The Battle for Middle-earth II: Rise of the Witch-king 2.01.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.

#include "GameEngineDevice/W3DDevice/GameClient/HouseColor.h"

#include "Common/AsciiString.h"

#include <cctype>
#include <set>

namespace
{
std::string trim(const std::string &s)
{
	size_t b = 0, e = s.size();
	while (b < e && std::isspace((unsigned char)s[b])) ++b;
	while (e > b && std::isspace((unsigned char)s[e - 1])) --e;
	return s.substr(b, e - b);
}
} // namespace

bool HouseColorTable::Parse(const std::string &text, std::string *error)
{
	Map.clear();
	ReplacedKeys.clear();
	Blocks = 0;

	auto fail = [&](int line, const std::string &msg) {
		if (error) *error = "housecolor.ini line " + std::to_string(line) + ": " + msg;
		return false;
	};

	bool inBlock = false;
	std::string base, house;
	bool haveBase = false, haveHouse = false;
	int lineNo = 0;
	size_t pos = 0;
	while (pos <= text.size())
	{
		size_t nl = text.find('\n', pos);
		std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
		pos = nl == std::string::npos ? text.size() + 1 : nl + 1;
		++lineNo;
		size_t c = line.find(';');
		if (c != std::string::npos) line.resize(c);
		c = line.find("//");
		if (c != std::string::npos) line.resize(c);
		line = trim(line);
		if (line.empty()) continue;

		if (!inBlock)
		{
			if (AsciiStringUtil::compareNoCase(line, "HouseColor") != 0)
			{
				return fail(lineNo, "expected 'HouseColor', found '" + line + "'");
			}
			inBlock = true;
			haveBase = haveHouse = false;
			continue;
		}
		if (AsciiStringUtil::compareNoCase(line, "End") == 0)
		{
			if (!haveBase || !haveHouse)
			{
				return fail(lineNo, "HouseColor block without both BaseTexture and HouseTexture");
			}
			std::string key = AsciiStringUtil::lowered(base);
			if (!Map.insert({ key, house }).second)
			{
				Map[key] = house; // retail: std::map operator[] assignment, the later block wins
				ReplacedKeys.push_back(key);
			}
			++Blocks;
			inBlock = false;
			continue;
		}
		size_t eq = line.find('=');
		if (eq == std::string::npos)
		{
			return fail(lineNo, "expected Field = value, found '" + line + "'");
		}
		std::string field = trim(line.substr(0, eq));
		std::string value = trim(line.substr(eq + 1));
		if (AsciiStringUtil::compareNoCase(field, "BaseTexture") == 0)
		{
			base = value;
			haveBase = true;
		}
		else if (AsciiStringUtil::compareNoCase(field, "HouseTexture") == 0)
		{
			house = value;
			haveHouse = true;
		}
		else
		{
			return fail(lineNo, "unknown field '" + field + "'");
		}
	}
	if (inBlock)
	{
		return fail(lineNo, "HouseColor block is not closed by End");
	}
	return true;
}

const std::string *HouseColorTable::Find(const std::string &baseTexture) const
{
	auto it = Map.find(AsciiStringUtil::lowered(baseTexture));
	return it == Map.end() ? nullptr : &it->second;
}

size_t HouseColorTable::House_Texture_Count() const
{
	std::set<std::string> houses;
	for (const auto &kv : Map)
	{
		houses.insert(AsciiStringUtil::lowered(kv.second));
	}
	return houses.size();
}

void Recolor_House_Texel(const HouseColorParams &params, const std::uint8_t in[4], std::uint8_t out[4])
{
	// RW 0x531C77 (see HouseColor.h): RW 0x53184D unpacks a colour as [0] = R (bits 16..23), [1] = G, [2] = B
	auto ch = [](std::uint32_t argb, int c) { return c == 0 ? (argb >> 16) & 255u : c == 1 ? (argb >> 8) & 255u : argb & 255u; };
	const std::uint32_t R = in[0], G = in[1], B = in[2];
	for (int c = 0; c < 3; ++c)
	{
		std::uint32_t v;
		switch (params.kind)
		{
			case 1: v = (R * ch(params.colors[0], c)) >> 8; break;
			case 2: v = ((R * ch(params.colors[0], c)) >> 8) + ((G * ch(params.colors[1], c)) >> 8); break;
			case 3: v = ((R * ch(params.colors[0], c)) >> 8) + ((G * ch(params.colors[1], c)) >> 8) + ((B * ch(params.colors[2], c)) >> 8); break;
			default: v = in[c]; break;
		}
		out[c] = (std::uint8_t)(v > 255u ? 255u : v);
	}
	out[3] = in[3];
}

void Recolor_House_Pixels(const HouseColorParams &params, std::uint8_t *rgba, std::size_t texels)
{
	for (std::size_t i = 0; i < texels; ++i)
	{
		std::uint8_t out[4];
		Recolor_House_Texel(params, rgba + i * 4, out);
		for (int c = 0; c < 4; ++c)
		{
			rgba[i * 4 + c] = out[c];
		}
	}
}
