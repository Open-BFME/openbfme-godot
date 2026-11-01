// OpenBFME. GPL-3.0.
// See GameClient/TutorialSettings.h.

#include "GameClient/TutorialSettings.h"

#include "Common/ArchiveFileSystem.h"
#include "Common/AsciiString.h"
#include "Common/INI.h"
#include "Common/INIException.h"
#include "Common/INI/INIBlockStubs.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace
{
struct State
{
	TutorialSettings values;
	bool block = false;
};

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
// the catch-all row (GameData fields other lanes read; the same rule as NetworkSettings): a `Field = value` line is consumed, a line without `=` opens a
// nested block that ends at its End
void skipField(INI *ini, void *, void *, const void *)
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
			throw INIException(4, "Missing 'END' token.\n\nError parsing nested block in file '%s', line %i.\n", ini->getFilename().c_str(), ini->currentSourceLine());
		}
		const std::string line = *next;
		ini->readLine();
		if (firstTokenIsEnd(line) && indentOf(line) <= headerIndent)
		{
			return;
		}
	}
}


// the record's fields by user data: 0 Map, 1 LoadMovie, 2 LoadScreenStillImage, 3 LoadScreenVoiceTrack, 4 LoadScreenMusicTrack; + 8 for the Advanced record
std::string &stringField(State &st, std::uintptr_t k)
{
	TutorialRecord &r = (k & 8) ? st.values.advanced : st.values.basic;
	switch (k & 7)
	{
	case 0: return r.map;
	case 1: return r.loadMovie;
	case 2: return r.loadScreenStillImage;
	case 3: return r.loadScreenVoiceTrack;
	default: return r.loadScreenMusicTrack;
	}
}
void parseString(INI *ini, void *instance, void *, const void *userData)
{
	State &st = *static_cast<State *>(instance);
	INI::parseAsciiString(ini, nullptr, &stringField(st, (std::uintptr_t)userData), nullptr); // RW 0x42EE5E
}
void parseObjective(INI *ini, void *instance, void *, const void *userData)
{
	State &st = *static_cast<State *>(instance);
	TutorialRecord &r = ((std::uintptr_t)userData & 8) ? st.values.advanced : st.values.basic;
	INI::parseAsciiStringVectorAppend(ini, nullptr, &r.objective, nullptr); // RW 0x42E59E
}
void parseFadeUp(INI *ini, void *instance, void *, const void *userData)
{
	State &st = *static_cast<State *>(instance);
	TutorialRecord &r = ((std::uintptr_t)userData & 8) ? st.values.advanced : st.values.basic;
	INI::parseDurationUnsignedInt(ini, nullptr, &r.fadeUpFrames, nullptr); // RW 0x73A429
}

#define UD(k) ((const void *)(std::uintptr_t)(k))

// RW 0xC00E00 .. 0xC00ED0
FieldParse kFields[] = {
	{ "BasicTutorialMap", parseString, UD(0), 0 },
	{ "BasicTutorialLoadMovie", parseString, UD(1), 0 },
	{ "BasicTutorialLoadScreenStillImage", parseString, UD(2), 0 },
	{ "BasicTutorialLoadScreenVoiceTrack", parseString, UD(3), 0 },
	{ "BasicTutorialLoadScreenMusicTrack", parseString, UD(4), 0 },
	{ "BasicTutorialObjective", parseObjective, UD(0), 0 },
	{ "BasicTutorialMillisecondsAfterStartToStartFadeUp", parseFadeUp, UD(0), 0 },
	{ "AdvancedTutorialMap", parseString, UD(8), 0 },
	{ "AdvancedTutorialLoadMovie", parseString, UD(9), 0 },
	{ "AdvancedTutorialLoadScreenStillImage", parseString, UD(10), 0 },
	{ "AdvancedTutorialLoadScreenVoiceTrack", parseString, UD(11), 0 },
	{ "AdvancedTutorialLoadScreenMusicTrack", parseString, UD(12), 0 },
	{ "AdvancedTutorialObjective", parseObjective, UD(8), 0 },
	{ "AdvancedTutorialMillisecondsAfterStartToStartFadeUp", parseFadeUp, UD(8), 0 },
	{ nullptr, skipField, nullptr, 0 }
};

class Load
{
public:
	explicit Load(ArchiveFileSystem *fs)
	{
		m_env.fileSystem = fs;
		RegisterRecordingBlockStubs(m_env.blocks, m_recorder, { "GameData" }, StubExtent::Lenient);
		m_env.blocks.registerBlock("GameData", [this](INI *ini) {
			m_state.block = true;
			ini->getNextTokenOrNull();
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

bool finish(Load &l, const std::string &what, TutorialSettings &out, std::string *error)
{
	if (!l.state().block)
	{
		if (error)
		{
			*error = what + ": no GameData block";
		}
		return false;
	}
	out = l.state().values;
	out.loaded = true;
	return true;
}
} // namespace

bool TutorialSettings::scan(const std::string &text, TutorialSettings &out, std::string *error)
{
	Load l(nullptr);
	return l.run("gamedata.ini", &text, error) && finish(l, "gamedata.ini", out, error);
}

bool TutorialSettings::load(ArchiveFileSystem &fs, TutorialSettings &out, std::string *error)
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

const TutorialRecord *TutorialSettings::record(const std::string &which) const
{
	// RW 0x91B825: strcmp with "Advanced" (RW 0xC7CE60), then "Basic" (RW 0xC7CE58)
	if (which == "Advanced")
	{
		return &advanced;
	}
	if (which == "Basic")
	{
		return &basic;
	}
	return nullptr;
}
