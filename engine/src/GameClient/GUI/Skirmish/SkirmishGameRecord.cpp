// OpenBFME. GPL-3.0.
// See GameClient/GUI/Skirmish/SkirmishGameRecord.h.

#include "GameClient/GUI/Skirmish/SkirmishGameRecord.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace
{
std::string format(const char *fmt, ...)
{
	char buf[512];
	va_list ap;
	va_start(ap, fmt);
	const int n = std::vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	return n < 0 ? std::string() : std::string(buf, (std::size_t)std::min(n, (int)sizeof(buf) - 1));
}

std::string utf8(const std::u16string &s)
{
	std::string out;
	for (std::size_t i = 0; i < s.size(); ++i)
	{
		std::uint32_t c = s[i];
		if (c >= 0xD800 && c <= 0xDBFF && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF)
		{
			c = 0x10000 + ((c - 0xD800) << 10) + (s[i + 1] - 0xDC00);
			++i;
		}
		if (c < 0x80)
		{
			out += (char)c;
		}
		else if (c < 0x800)
		{
			out += (char)(0xC0 | (c >> 6));
			out += (char)(0x80 | (c & 0x3F));
		}
		else if (c < 0x10000)
		{
			out += (char)(0xE0 | (c >> 12));
			out += (char)(0x80 | ((c >> 6) & 0x3F));
			out += (char)(0x80 | (c & 0x3F));
		}
		else
		{
			out += (char)(0xF0 | (c >> 18));
			out += (char)(0x80 | ((c >> 12) & 0x3F));
			out += (char)(0x80 | ((c >> 6) & 0x3F));
			out += (char)(0x80 | (c & 0x3F));
		}
	}
	return out;
}

std::u16string utf16(const std::string &s)
{
	std::u16string out;
	for (std::size_t i = 0; i < s.size();)
	{
		const unsigned char b = (unsigned char)s[i];
		std::uint32_t c = b;
		int extra = 0;
		if (b >= 0xF0)
		{
			c = b & 0x07;
			extra = 3;
		}
		else if (b >= 0xE0)
		{
			c = b & 0x0F;
			extra = 2;
		}
		else if (b >= 0xC0)
		{
			c = b & 0x1F;
			extra = 1;
		}
		++i;
		for (int k = 0; k < extra && i < s.size(); ++k, ++i)
		{
			c = (c << 6) | ((unsigned char)s[i] & 0x3F);
		}
		if (c >= 0x10000)
		{
			c -= 0x10000;
			out += (char16_t)(0xD800 + (c >> 10));
			out += (char16_t)(0xDC00 + (c & 0x3FF));
		}
		else
		{
			out += (char16_t)c;
		}
	}
	return out;
}

// strtok_r over a std::string: the next token after skipping leading delimiters, or false at the end
bool nextToken(const std::string &s, std::size_t &pos, const char *delims, std::string &token)
{
	const std::string d(delims);
	while (pos < s.size() && d.find(s[pos]) != std::string::npos)
	{
		++pos;
	}
	if (pos >= s.size())
	{
		return false;
	}
	const std::size_t end = s.find_first_of(d, pos);
	const std::size_t stop = end == std::string::npos ? s.size() : end;
	token = s.substr(pos, stop - pos);
	pos = stop < s.size() ? stop + 1 : stop;
	return true;
}

// MSVCR71 atoi: leading white space, a sign, decimal digits (the values here are small)
int atoiRetail(const char *p)
{
	return std::atoi(p);
}

unsigned int hexValue(const std::string &v)
{
	unsigned int out = 0;
	std::sscanf(v.c_str(), "%X", &out);
	return out;
}

std::string mapPathOut(const std::string &name)
{
	std::string out = name;
	for (char &c : out)
	{
		c = c == '/' ? '\\' : c;
	}
	return out;
}

std::string mapPathIn(const std::string &path)
{
	std::string out = path;
	for (char &c : out)
	{
		c = c == '\\' ? '/' : (c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c);
	}
	return out;
}
} // namespace

std::string SkirmishGameInfoToString(const SkirmishGameInfo &info, int gameMode)
{
	// RW 0x8023C1
	std::string out = format("M=%3.3x%s;MC=%X;MS=%d;SD=%d;GSID=%X;GT=%d;SI=%d;", (unsigned)info.mapMask, mapPathOut(info.mapName).c_str(), (unsigned)info.mapCRC,
		(int)info.mapSize, (int)info.seed, 0u, gameMode, -1);
	out += "GR=";
	for (int i = 0; i < NUM_GAME_RULES; ++i)
	{
		out += format("%d ", (int)info.rules[(std::size_t)i]); // RW 0x960B4F
	}
	out += ";";
	if (info.mapStartCount > LEGACY_MAX_SLOTS)
	{
		out += "SC=" + std::to_string(info.mapStartCount) + ";";
	}
	out += "S=";
	for (int i = 0; i < info.serializedSlotCount(); ++i)
	{
		const SkirmishGameSlot &slot = info.slots[i];
		if (slot.isHuman())
		{
			out += format("H%s,%X,%d,%c%c,%d,%d,%d,%d,%d,%d,%d:", utf8(slot.name).c_str(), 0u, 0, slot.accepted ? 'T' : 'F', slot.hasMap ? 'T' : 'F', slot.color,
				slot.playerTemplate, slot.startPos, slot.teamNumber, slot.handicap, 0, 0); // lane OWNERFIX-2: + 0x20 is the slot's handicap
		}
		else if (slot.isAI())
		{
			// RW 0x802603 .. 0x802620: 2 E, 3 M, 4 H, any other computer B
			const char c = slot.state == SLOT_EASY_AI ? 'E' : slot.state == SLOT_MED_AI ? 'M' : slot.state == SLOT_HARD_AI ? 'H' : 'B';
			out += format("C%c,%d,%d,%d,%d,%d,%d:", c, slot.color, slot.playerTemplate, slot.startPos, slot.teamNumber, slot.handicap, 0); // lane OWNERFIX-2: + 0x20
		}
		else if (slot.state == SLOT_OPEN)
		{
			out += "O:";
		}
		else
		{
			out += "X:";
		}
	}
	out += ";";
	return out;
}

bool ParseSkirmishGameInfo(const std::string &text, const SkirmishGameRecordLimits &limits, SkirmishGameInfo &out, int *gameMode, std::string *error)
{
	// RW 0x802DBA
	auto fail = [&](const std::string &why) {
		if (error)
		{
			*error = why;
		}
		return false;
	};
	SkirmishGameSlot newSlot[MAX_SLOTS];
	std::string mapName;
	int mapContentsMask = 0, gt = 0, mapStartCount = LEGACY_MAX_SLOTS, recordCount = 0;
	unsigned int mapCRC = 0, mapSize = 0;
	int seed = 0;
	GameRules rules = MpGameRules::none();
	bool sawMap = false, sawMapCRC = false, sawMapSize = false, sawSeed = false, sawSlotlist = false, sawGR = false, sawGSID = false, sawGT = false, sawSI = false;
	std::size_t at = 0;
	std::string pair;
	while (nextToken(text, at, ";", pair))
	{
		std::size_t kp = 0;
		std::string key, val;
		nextToken(pair, kp, "=", key);
		nextToken(pair, kp, "\n", val);
		if (val.empty())
		{
			return fail("the value of '" + key + "' is empty");
		}
		if (key == "M")
		{
			if (val.size() < 3)
			{
				return fail("M is shorter than its mask");
			}
			mapContentsMask = (int)std::strtol(("0x" + val.substr(0, 3)).c_str(), nullptr, 16);
			mapName = mapPathIn(val.substr(3));
			sawMap = true;
		}
		else if (key == "MC")
		{
			mapCRC = hexValue(val);
			sawMapCRC = true;
		}
		else if (key == "MS")
		{
			mapSize = (unsigned int)atoiRetail(val.c_str());
			sawMapSize = true;
		}
		else if (key == "SD")
		{
			seed = atoiRetail(val.c_str());
			sawSeed = true;
		}
		else if (key == "GR")
		{
			// RW 0x9609D9: ten atoi()s, each from just past the next space
			const char *p = val.c_str();
			for (int i = 0; i < NUM_GAME_RULES; ++i)
			{
				rules[(std::size_t)i] = atoiRetail(p);
				while (*p && *p != ' ')
				{
					++p;
				}
				if (*p)
				{
					++p;
				}
			}
			sawGR = true;
		}
		else if (key == "GSID")
		{
			hexValue(val);
			sawGSID = true;
		}
		else if (key == "GT")
		{
			gt = atoiRetail(val.c_str());
			sawGT = true;
		}
		else if (key == "SI")
		{
			atoiRetail(val.c_str());
			sawSI = true;
		}
		else if (key == "SC")
		{
			if (val != "9" && val != "10" && val != "11" && val != "12" && val != "13" && val != "14" && val != "15" && val != "16")
			{
				return fail("invalid extended map start count '" + val + "'");
			}
			mapStartCount = atoiRetail(val.c_str());
		}
		else if (key == "S")
		{
			sawSlotlist = true;
			std::size_t sp = 0;
			std::vector<std::string> records;
			std::string record;
			while (nextToken(val, sp, ":", record))
			{
				records.push_back(record);
			}
			const int slotCount = (int)records.size();
			recordCount = slotCount;
			if (slotCount != LEGACY_MAX_SLOTS && slotCount != MAX_SLOTS)
			{
				return fail("the slot list has " + std::to_string(slotCount) + " slots (8 or 16 required)");
			}
			for (int i = 0; i < slotCount; ++i)
			{
				const std::string &raw = records[(std::size_t)i];
				SkirmishGameSlot &slot = newSlot[i];
				std::size_t fp = 0;
				std::vector<std::string> f;
				std::string field;
				while (nextToken(raw, fp, ",", field))
				{
					f.push_back(field);
				}
				auto num = [&](std::size_t k, int lo, int hi, int &value) {
					if (k >= f.size() || f[k].empty())
					{
						return false;
					}
					value = atoiRetail(f[k].c_str());
					return value >= lo && value <= hi;
				};
				const std::string slotText = "slot " + std::to_string(i);
				switch (raw[0])
				{
					case 'H':
					{
						// H<name>,<ip>,<port>,<TF><TF>,colour,template,start,team,+0x20,+0x40,hero
						if (f.size() < 11 || f[3].size() != 2)
						{
							return fail(slotText + ": a human record of " + std::to_string(f.size()) + " fields");
						}
						slot = SkirmishGameSlot();
						slot.state = SLOT_PLAYER;
						slot.name = utf16(f[0].substr(1));
						slot.accepted = f[3][0] == 'T';
						slot.hasMap = f[3][1] == 'T';
						int v20 = 0, v40 = 0;
						if (!num(4, -1, limits.numColors - 1, slot.color) || !num(5, PLAYERTEMPLATE_OBSERVER, limits.numTemplates - 1, slot.playerTemplate)
							|| !num(6, -1, 0x7FFFFFFF, slot.startPos) || !num(7, -1, MAX_SLOTS / 2 - 1, slot.teamNumber) || !num(8, -100, 0, v20) || !num(9, 0, 0x100, v40))
						{
							return fail(slotText + ": a human record's value is out of range");
						}
						slot.handicap = v20; // lane OWNERFIX-2: + 0x20 (retail's range -100 .. 0; the lobby checks its own set, SkirmishGameSetup::restoreSaved)
						break;
					}
					case 'C':
					{
						// C<E|M|H|B>,colour,template,start,team,+0x20,hero
						if (f.size() < 7 || f[0].size() < 2)
						{
							return fail(slotText + ": a computer record of " + std::to_string(f.size()) + " fields");
						}
						slot = SkirmishGameSlot();
						switch (f[0][1])
						{
							case 'E': slot.state = SLOT_EASY_AI; break;
							case 'M': slot.state = SLOT_MED_AI; break;
							case 'H': slot.state = SLOT_HARD_AI; break;
							case 'B': slot.state = SLOT_BRUTAL_AI; break;
							default: return fail(slotText + ": no computer level '" + f[0].substr(1, 1) + "'");
						}
						slot.accepted = true;
						slot.hasMap = true;
						int v20 = 0;
						if (!num(1, -1, limits.numColors - 1, slot.color) || !num(2, PLAYERTEMPLATE_OBSERVER, limits.numTemplates - 1, slot.playerTemplate)
							|| !num(3, -1, 0x7FFFFFFF, slot.startPos) || !num(4, -1, MAX_SLOTS / 2 - 1, slot.teamNumber) || !num(5, -100, 0, v20))
						{
							return fail(slotText + ": a computer record's value is out of range");
						}
						slot.handicap = v20; // lane OWNERFIX-2: + 0x20
						break;
					}
					case 'O':
						slot = SkirmishGameSlot();
						slot.state = SLOT_OPEN;
						break;
					case 'X':
						slot = SkirmishGameSlot();
						slot.state = SLOT_CLOSED;
						break;
					default:
						return fail(slotText + ": no slot kind '" + raw.substr(0, 1) + "'");
				}
			}
		}
		else
		{
			return fail("an unknown key '" + key + "'");
		}
	}
	if (!(sawMap && sawMapCRC && sawMapSize && sawSeed && sawSlotlist && sawGR && sawGSID && sawGT && sawSI))
	{
		return fail("a key is missing");
	}
	if (recordCount != (mapStartCount > LEGACY_MAX_SLOTS ? MAX_SLOTS : LEGACY_MAX_SLOTS))
	{
		return fail("the slot list disagrees with the map start count");
	}
	const int teamCount = (mapStartCount > LEGACY_MAX_SLOTS ? MAX_SLOTS : LEGACY_MAX_SLOTS) / 2;
	for (int i = 0; i < recordCount; ++i)
	{
		if (i >= mapStartCount && newSlot[i].state != SLOT_CLOSED)
		{
			return fail("slot " + std::to_string(i + 1) + ": outside the map capacity");
		}
		if (newSlot[i].isOccupied() && newSlot[i].teamNumber >= teamCount)
		{
			return fail("slot " + std::to_string(i) + ": the team is outside the map capacity");
		}
	}
	out.mapStartCount = mapStartCount;
	for (int i = 0; i < MAX_SLOTS; ++i)
	{
		out.slots[i] = newSlot[i];
	}
	out.mapName = mapName;
	out.mapCRC = mapCRC;
	out.mapSize = mapSize;
	out.mapMask = mapContentsMask;
	out.seed = (std::uint32_t)seed;
	out.rules = rules; // after the mode (RW 0x802DBA: rva003FF1A7(gt), then the rules)
	if (gameMode)
	{
		*gameMode = gt;
	}
	return true;
}

std::string SkirmishHeroIndexesString()
{
	// RW 0x821D55: ":" then "%d:" for the eight slots (S-3701: no hero index is kept, -1 each)
	std::string out = ":";
	for (int i = 0; i < LEGACY_MAX_SLOTS; ++i)
	{
		out += "-1:";
	}
	return out;
}
