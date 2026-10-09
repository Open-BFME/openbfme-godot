// OpenBFME. GPL-3.0.
// See GameLogic/EconomySettings.h.

#include "GameLogic/EconomySettings.h"

#include "Common/ArchiveFileSystem.h"
#include "Common/AsciiString.h"
#include "Common/INI.h"
#include "Common/INIException.h"
#include "Common/INI/INIBlockStubs.h"

#include <cstddef>
#include <cstdint>

namespace
{
struct State
{
	EconomySettings values;
	unsigned long long seen = 0;
	bool block = false;
};

enum : unsigned long long
{
	SEEN_CELL = 1ull << 0,
	SEEN_SOLO_G = 1ull << 1,
	SEEN_SOLO_E = 1ull << 2,
	SEEN_AI_G = 1ull << 3,
	SEEN_AI_E = 1ull << 4,
	SEEN_BONUS_G = 1ull << 5,
	SEEN_BONUS_E = 1ull << 6,
	SEEN_LIMIT_G = 1ull << 7,
	SEEN_LIMIT_E = 1ull << 8,
	SEEN_POWER = 1ull << 9,
	SEEN_RESBONUS = 1ull << 10,
	SEEN_RESLIMIT = 1ull << 11,
	SEEN_MONEYMULT = 1ull << 12,
	SEEN_MP0 = 1ull << 13 // 14 bits from here: Good MP2..MP8, Evil MP2..MP8
};

template <INIFieldParseProc Proc>
void parseNoting(INI *ini, void *instance, void *store, const void *userData)
{
	Proc(ini, instance, store, nullptr);
	static_cast<State *>(instance)->seen |= (unsigned long long)(std::uintptr_t)userData;
}

// RW 0x641911: two parseInt (start, cap)
void parsePair(INI *ini, void *instance, void *store, const void *userData)
{
	EconomySettings::Pair *pair = static_cast<EconomySettings::Pair *>(store);
	INI::parseInt(ini, instance, &pair->start, nullptr);
	INI::parseInt(ini, instance, &pair->cap, nullptr);
	static_cast<State *>(instance)->seen |= (unsigned long long)(std::uintptr_t)userData;
}

// RW 0x642182: tokens split at ':' ("MP1:1.0"): the name, then the real; a name that is not MP1 .. MP8 is read and dropped
void parseMultiPlayMoneyMult(INI *ini, void *instance, void *store, const void *userData)
{
	float *table = static_cast<float *>(store);
	for (const char *tok = ini->getNextTokenOrNull(ini->getSepsColon()); tok; tok = ini->getNextTokenOrNull(ini->getSepsColon()))
	{
		const std::string name = tok;
		const char *valueToken = ini->getNextToken(ini->getSepsColon());
		const float value = ini->scanReal(valueToken);
		for (int n = 1; n <= 8; ++n)
		{
			if (AsciiStringUtil::compareNoCase(name, "MP" + std::to_string(n)) == 0)
			{
				table[n - 1] = value;
				break;
			}
		}
	}
	static_cast<State *>(instance)->seen |= (unsigned long long)(std::uintptr_t)userData;
}

size_t indentOf(const std::string &line)
{
	size_t n = 0;
	while (n < line.size() && (line[n] == ' ' || line[n] == '\t'))
	{
		++n;
	}
	return n;
}
std::string withoutComment(const std::string &line)
{
	size_t cut = line.find(';');
	const size_t slashes = line.find("//");
	if (slashes != std::string::npos && (cut == std::string::npos || slashes < cut))
	{
		cut = slashes;
	}
	return cut == std::string::npos ? line : line.substr(0, cut);
}
bool firstTokenIsEnd(const std::string &line)
{
	const std::string l = withoutComment(line);
	const size_t b = l.find_first_not_of(" \t");
	if (b == std::string::npos)
	{
		return false;
	}
	const size_t e = l.find_first_of(" \t=", b);
	return AsciiStringUtil::compareNoCase(l.substr(b, e == std::string::npos ? std::string::npos : e - b), "End") == 0;
}
// the catch-all row (a GameData field this lane does not read: the full GlobalData table belongs to the lanes that own those fields, stop S-152 of LOGIC-1
// reports them): a `Field = value` line is consumed, a line without `=` opens a nested block that ends at its End
void skipField(INI *ini, void *, void *, const void *userData)
{
	const std::string header = ini->currentLineText();
	if (withoutComment(header).find('=') != std::string::npos)
	{
		return;
	}
	const size_t headerIndent = indentOf(header);
	for (;;)
	{
		const std::string *next = ini->peekNextLine();
		if (!next)
		{
			throw INIException(4, "Missing 'END' token.\n\nError parsing nested block '%s' in file '%s', line %i.\n", static_cast<const char *>(userData), ini->getFilename().c_str(),
				ini->currentSourceLine());
		}
		const std::string line = *next;
		ini->readLine();
		if (firstTokenIsEnd(line) && indentOf(line) <= headerIndent)
		{
			return;
		}
	}
}

#define SEEN_DATA(bit) ((const void *)(std::uintptr_t)(bit))
#define ES_OFF(member) (int)offsetof(State, values.member)

FieldParse kFields[] = {
	{ "TerrainResourceCellSize", parseNoting<INI::parseReal>, SEEN_DATA(SEEN_CELL), ES_OFF(terrainResourceCellSize) },
	{ "GoodCommandPoints", parsePair, SEEN_DATA(SEEN_SOLO_G), ES_OFF(goodSolo) },
	{ "EvilCommandPoints", parsePair, SEEN_DATA(SEEN_SOLO_E), ES_OFF(evilSolo) },
	{ "GoodCommandPointsAI", parsePair, SEEN_DATA(SEEN_AI_G), ES_OFF(goodAI) },
	{ "EvilCommandPointsAI", parsePair, SEEN_DATA(SEEN_AI_E), ES_OFF(evilAI) },
	{ "GoodCommandPointsBonus", parseNoting<INI::parseInt>, SEEN_DATA(SEEN_BONUS_G), ES_OFF(goodBonus) },
	{ "EvilCommandPointsBonus", parseNoting<INI::parseInt>, SEEN_DATA(SEEN_BONUS_E), ES_OFF(evilBonus) },
	{ "GoodCommandPointLimit", parseNoting<INI::parseInt>, SEEN_DATA(SEEN_LIMIT_G), ES_OFF(goodLimit) },
	{ "EvilCommandPointLimit", parseNoting<INI::parseInt>, SEEN_DATA(SEEN_LIMIT_E), ES_OFF(evilLimit) },
	{ "PowerLimit", parseNoting<INI::parseInt>, SEEN_DATA(SEEN_POWER), ES_OFF(powerLimit) },
	{ "ResourceBonusMultiplier", parseNoting<INI::parseReal>, SEEN_DATA(SEEN_RESBONUS), ES_OFF(resourceBonusMultiplier) },
	{ "ResourceMultiplierLimit", parseNoting<INI::parseReal>, SEEN_DATA(SEEN_RESLIMIT), ES_OFF(resourceMultiplierLimit) },
	{ "MultiPlayMoneyMult", parseMultiPlayMoneyMult, SEEN_DATA(SEEN_MONEYMULT), ES_OFF(multiPlayMoneyMult[0]) },
	{ "MultiPlayUnitXPMult", parseMultiPlayMoneyMult, nullptr, ES_OFF(multiPlayUnitXPMult[0]) },         // lane XP-1 (RW 0x6422D2)
	{ "MultiPlayBuildingXPMult", parseMultiPlayMoneyMult, nullptr, ES_OFF(multiPlayBuildingXPMult[0]) }, // lane XP-1 (RW 0x642408)
	{ "GoodCommandPointsMP2", parsePair, SEEN_DATA(SEEN_MP0 << 0), ES_OFF(goodMP[0]) },
	{ "GoodCommandPointsMP3", parsePair, SEEN_DATA(SEEN_MP0 << 1), ES_OFF(goodMP[1]) },
	{ "GoodCommandPointsMP4", parsePair, SEEN_DATA(SEEN_MP0 << 2), ES_OFF(goodMP[2]) },
	{ "GoodCommandPointsMP5", parsePair, SEEN_DATA(SEEN_MP0 << 3), ES_OFF(goodMP[3]) },
	{ "GoodCommandPointsMP6", parsePair, SEEN_DATA(SEEN_MP0 << 4), ES_OFF(goodMP[4]) },
	{ "GoodCommandPointsMP7", parsePair, SEEN_DATA(SEEN_MP0 << 5), ES_OFF(goodMP[5]) },
	{ "GoodCommandPointsMP8", parsePair, SEEN_DATA(SEEN_MP0 << 6), ES_OFF(goodMP[6]) },
	{ "EvilCommandPointsMP2", parsePair, SEEN_DATA(SEEN_MP0 << 7), ES_OFF(evilMP[0]) },
	{ "EvilCommandPointsMP3", parsePair, SEEN_DATA(SEEN_MP0 << 8), ES_OFF(evilMP[1]) },
	{ "EvilCommandPointsMP4", parsePair, SEEN_DATA(SEEN_MP0 << 9), ES_OFF(evilMP[2]) },
	{ "EvilCommandPointsMP5", parsePair, SEEN_DATA(SEEN_MP0 << 10), ES_OFF(evilMP[3]) },
	{ "EvilCommandPointsMP6", parsePair, SEEN_DATA(SEEN_MP0 << 11), ES_OFF(evilMP[4]) },
	{ "EvilCommandPointsMP7", parsePair, SEEN_DATA(SEEN_MP0 << 12), ES_OFF(evilMP[5]) },
	{ "EvilCommandPointsMP8", parsePair, SEEN_DATA(SEEN_MP0 << 13), ES_OFF(evilMP[6]) },
	{ nullptr, skipField, nullptr, 0 }
};

const unsigned long long kAllSeen = (1ull << 13) - 1 | (((1ull << 14) - 1) << 13);

class Load
{
public:
	explicit Load(ArchiveFileSystem *fs)
	{
		m_env.fileSystem = fs;
		RegisterRecordingBlockStubs(m_env.blocks, m_recorder, { "GameData" }, StubExtent::Lenient);
		m_env.blocks.registerBlock("GameData", [this](INI *ini) {
			m_state.block = true;
			ini->getNextTokenOrNull(); // a name after the keyword is not used
			ini->initFromINI(&m_state, kFields);
		});
	}
	bool run(const std::string &path, const std::string *memory, std::string *error)
	{
		INI ini(m_env);
		try
		{
			if (memory)
			{
				ini.loadMemory(path, std::vector<std::uint8_t>(memory->begin(), memory->end()), INI_LOAD_OVERWRITE);
			}
			else
			{
				ini.load(path, INI_LOAD_OVERWRITE);
			}
		}
		catch (const std::exception &e)
		{
			if (error)
			{
				*error = path + ": " + e.what();
			}
			return false;
		}
		return true;
	}
	State &state() { return m_state; }

private:
	INIEnvironment m_env;
	INIBlockRecorder m_recorder;
	State m_state;
};

const char *missingKey(unsigned long long seen)
{
	struct Need
	{
		unsigned long long bit;
		const char *name;
	};
	static const Need needs[] = { { SEEN_CELL, "TerrainResourceCellSize" }, { SEEN_SOLO_G, "GoodCommandPoints" }, { SEEN_SOLO_E, "EvilCommandPoints" }, { SEEN_AI_G, "GoodCommandPointsAI" },
		{ SEEN_AI_E, "EvilCommandPointsAI" }, { SEEN_BONUS_G, "GoodCommandPointsBonus" }, { SEEN_BONUS_E, "EvilCommandPointsBonus" }, { SEEN_LIMIT_G, "GoodCommandPointLimit" },
		{ SEEN_LIMIT_E, "EvilCommandPointLimit" }, { SEEN_POWER, "PowerLimit" }, { SEEN_RESBONUS, "ResourceBonusMultiplier" }, { SEEN_RESLIMIT, "ResourceMultiplierLimit" },
		{ SEEN_MONEYMULT, "MultiPlayMoneyMult" }, { SEEN_MP0 << 0, "GoodCommandPointsMP2" }, { SEEN_MP0 << 1, "GoodCommandPointsMP3" }, { SEEN_MP0 << 2, "GoodCommandPointsMP4" },
		{ SEEN_MP0 << 3, "GoodCommandPointsMP5" }, { SEEN_MP0 << 4, "GoodCommandPointsMP6" }, { SEEN_MP0 << 5, "GoodCommandPointsMP7" }, { SEEN_MP0 << 6, "GoodCommandPointsMP8" },
		{ SEEN_MP0 << 7, "EvilCommandPointsMP2" }, { SEEN_MP0 << 8, "EvilCommandPointsMP3" }, { SEEN_MP0 << 9, "EvilCommandPointsMP4" }, { SEEN_MP0 << 10, "EvilCommandPointsMP5" },
		{ SEEN_MP0 << 11, "EvilCommandPointsMP6" }, { SEEN_MP0 << 12, "EvilCommandPointsMP7" }, { SEEN_MP0 << 13, "EvilCommandPointsMP8" } };
	for (const Need &n : needs)
	{
		if (!(seen & n.bit))
		{
			return n.name;
		}
	}
	return nullptr;
}

bool finish(Load &l, const char *what, EconomySettings &out, std::string *error)
{
	if (!l.state().block)
	{
		if (error)
		{
			*error = std::string(what) + ": no GameData block";
		}
		return false;
	}
	if (const char *miss = missingKey(l.state().seen))
	{
		if (error)
		{
			*error = std::string(what) + ": GameData has no " + miss;
		}
		return false;
	}
	out = l.state().values;
	out.loaded = true;
	return true;
}
} // namespace

bool EconomySettings::scan(const std::string &text, EconomySettings &out, std::string *error)
{
	Load l(nullptr);
	return l.run("gamedata.ini", &text, error) && finish(l, "gamedata.ini", out, error);
}

bool EconomySettings::load(ArchiveFileSystem &fs, EconomySettings &out, std::string *error)
{
	const char *file = "data\\ini\\gamedata.ini";
	if (!fs.doesFileExist(file))
	{
		if (error)
		{
			*error = std::string("file not found in any mounted archive: ") + file;
		}
		return false;
	}
	Load l(&fs);
	return l.run(file, nullptr, error) && finish(l, file, out, error);
}

void EconomySettings::crc(StateHasher &h) const
{
	h.addBool(loaded);
	h.addFloat(terrainResourceCellSize);
	for (const Pair *p : { &goodSolo, &evilSolo, &goodAI, &evilAI })
	{
		h.addI32(p->start);
		h.addI32(p->cap);
	}
	for (int i = 0; i < 7; ++i)
	{
		h.addI32(goodMP[i].start);
		h.addI32(goodMP[i].cap);
		h.addI32(evilMP[i].start);
		h.addI32(evilMP[i].cap);
	}
	h.addI32(goodBonus);
	h.addI32(evilBonus);
	h.addI32(goodLimit);
	h.addI32(evilLimit);
	h.addI32(powerLimit);
	h.addFloat(resourceBonusMultiplier);
	h.addFloat(resourceMultiplierLimit);
	for (float f : multiPlayMoneyMult)
	{
		h.addFloat(f);
	}
	for (int i = 0; i < 20; ++i) // lane XP-1
	{
		h.addFloat(multiPlayUnitXPMult[i]);
		h.addFloat(multiPlayBuildingXPMult[i]);
	}
}

void EconomyContext::crc(StateHasher &h) const
{
	h.addI32(gameMode);
	h.addI32(gameKind);
	h.addBool(livingWorld);
	h.addBool(gameInfoPresent);
	h.addI32(lobbyCommandPointPercent);
	h.addBool(scoring);
}
