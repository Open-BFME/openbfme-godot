// OpenBFME. GPL-3.0.
// See GameLogic/System/VisionSettings.h.

#include "GameLogic/System/VisionSettings.h"

#include "Common/ArchiveFileSystem.h"
#include "Common/AsciiString.h"
#include "Common/INI.h"
#include "Common/INIDataTypes.h"
#include "Common/INIException.h"
#include "Common/INI/INIBlockStubs.h"

#include <cstddef>
#include <cstdint>

namespace
{
enum : unsigned
{
	SEEN_CELL = 1u << 0,
	SEEN_UNLOOK = 1u << 1,
	SEEN_COLOR = 1u << 2,
	SEEN_FOG = 1u << 3,
	SEEN_SHROUD = 1u << 4,
	SEEN_STEALTH = 1u << 5,
	SEEN_USESHROUD = 1u << 6
};

struct State
{
	float cellSize = 0.0f;
	unsigned unlook = 30;
	RGBColor color{ 0.0f, 0.0f, 0.0f };
	std::uint8_t clearAlpha = 255, fogAlpha = 127, shroudAlpha = 0;
	float stealth = 0.0f;
	bool useShroud = false;
	unsigned seen = 0;
	bool gameData = false, multiplayer = false;
};

template <INIFieldParseProc Proc>
void parseNoting(INI *ini, void *instance, void *store, const void *userData)
{
	Proc(ini, instance, store, nullptr);
	static_cast<State *>(instance)->seen |= (unsigned)(std::uintptr_t)userData;
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
// the catch-all row: the fields of these blocks other lanes own (a `Field = value` line, or a nested block up to its End)
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
#define VS_OFF(member) (int)offsetof(State, member)

FieldParse kGameData[] = {
	{ "PartitionCellSize", parseNoting<INI::parseReal>, SEEN_DATA(SEEN_CELL), VS_OFF(cellSize) },
	{ "UnlookPersistDuration", parseNoting<INI::parseDurationUnsignedInt>, SEEN_DATA(SEEN_UNLOOK), VS_OFF(unlook) },
	{ "ShroudColor", parseNoting<INI::parseRGBColor>, SEEN_DATA(SEEN_COLOR), VS_OFF(color) },
	{ "ClearAlpha", INI::parseUnsignedByte, nullptr, VS_OFF(clearAlpha) },
	{ "FogAlpha", parseNoting<INI::parseUnsignedByte>, SEEN_DATA(SEEN_FOG), VS_OFF(fogAlpha) },
	{ "ShroudAlpha", parseNoting<INI::parseUnsignedByte>, SEEN_DATA(SEEN_SHROUD), VS_OFF(shroudAlpha) },
	{ "StealthFriendlyOpacity", parseNoting<INI::parsePercentToReal>, SEEN_DATA(SEEN_STEALTH), VS_OFF(stealth) },
	{ nullptr, skipField, nullptr, 0 }
};

FieldParse kMultiplayer[] = {
	{ "UseShroud", parseNoting<INI::parseBool>, SEEN_DATA(SEEN_USESHROUD), VS_OFF(useShroud) },
	{ nullptr, skipField, nullptr, 0 }
};

class Load
{
public:
	Load(ArchiveFileSystem *fs, const char *block, FieldParse *fields, bool State::*flag)
	{
		m_env.fileSystem = fs;
		RegisterRecordingBlockStubs(m_env.blocks, m_recorder, { block }, StubExtent::Lenient);
		m_env.blocks.registerBlock(block, [this, fields, flag](INI *ini) {
			m_state->*flag = true;
			ini->getNextTokenOrNull();
			ini->initFromINI(m_state, fields);
		});
	}
	bool run(State &state, const std::string &path, const std::string *memory, std::string *error)
	{
		m_state = &state;
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

private:
	INIEnvironment m_env;
	INIBlockRecorder m_recorder;
	State *m_state = nullptr;
};

bool finish(const State &s, VisionSettings &out, std::string *error)
{
	struct Need
	{
		unsigned bit;
		const char *name;
	};
	static const Need needs[] = { { SEEN_CELL, "GameData PartitionCellSize" }, { SEEN_UNLOOK, "GameData UnlookPersistDuration" }, { SEEN_COLOR, "GameData ShroudColor" },
		{ SEEN_FOG, "GameData FogAlpha" }, { SEEN_SHROUD, "GameData ShroudAlpha" }, { SEEN_STEALTH, "GameData StealthFriendlyOpacity" },
		{ SEEN_USESHROUD, "MultiplayerSettings UseShroud" } };
	if (!s.gameData || !s.multiplayer)
	{
		if (error)
		{
			*error = !s.gameData ? "vision settings: no GameData block" : "vision settings: no MultiplayerSettings block";
		}
		return false;
	}
	for (const Need &n : needs)
	{
		if (!(s.seen & n.bit))
		{
			if (error)
			{
				*error = std::string("vision settings: missing ") + n.name;
			}
			return false;
		}
	}
	out = VisionSettings();
	out.partitionCellSize = s.cellSize;
	out.unlookPersistFrames = s.unlook;
	out.shroudRed = s.color.red;
	out.shroudGreen = s.color.green;
	out.shroudBlue = s.color.blue;
	out.clearAlpha = s.clearAlpha;
	out.fogAlpha = s.fogAlpha;
	out.shroudAlpha = s.shroudAlpha;
	out.stealthFriendlyOpacity = s.stealth;
	out.useShroud = s.useShroud;
	out.loaded = true;
	return true;
}
} // namespace

bool VisionSettings::scan(const std::string &gameData, const std::string &multiplayer, VisionSettings &out, std::string *error)
{
	State s;
	Load g(nullptr, "GameData", kGameData, &State::gameData);
	Load m(nullptr, "MultiplayerSettings", kMultiplayer, &State::multiplayer);
	return g.run(s, "gamedata.ini", &gameData, error) && m.run(s, "multiplayer.ini", &multiplayer, error) && finish(s, out, error);
}

bool VisionSettings::load(ArchiveFileSystem &fs, VisionSettings &out, std::string *error)
{
	const char *files[2] = { "data\\ini\\gamedata.ini", "data\\ini\\multiplayer.ini" };
	for (const char *f : files)
	{
		if (!fs.doesFileExist(f))
		{
			if (error)
			{
				*error = std::string("file not found in any mounted archive: ") + f;
			}
			return false;
		}
	}
	State s;
	Load g(&fs, "GameData", kGameData, &State::gameData);
	Load m(&fs, "MultiplayerSettings", kMultiplayer, &State::multiplayer);
	return g.run(s, files[0], nullptr, error) && m.run(s, files[1], nullptr, error) && finish(s, out, error);
}
