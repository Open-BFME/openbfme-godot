// OpenBFME. GPL-3.0.
// See GameNetwork/NetworkSettings.h.

#include "GameNetwork/NetworkSettings.h"

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
	NetworkSettings values;
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
// the catch-all row (GameData fields other lanes read; the same rule as EconomySettings): a `Field = value` line is consumed, a line without `=` opens a
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

#define NS_OFF(member) (int)offsetof(State, values.member)

// RW 0xC007D0 .. 0xC00850: every row parses with RW 0x42EC5E
FieldParse kFields[] = {
	{ "NetworkFPSHistoryLength", INI::parseUnsignedInt, nullptr, NS_OFF(fpsHistoryLength) },
	{ "NetworkLatencyHistoryLength", INI::parseUnsignedInt, nullptr, NS_OFF(latencyHistoryLength) },
	{ "NetworkRunAheadMetricsTime", INI::parseUnsignedInt, nullptr, NS_OFF(runAheadMetricsTime) },
	{ "NetworkCushionHistoryLength", INI::parseUnsignedInt, nullptr, NS_OFF(cushionHistoryLength) },
	{ "NetworkRunAheadSlack", INI::parseUnsignedInt, nullptr, NS_OFF(runAheadSlack) },
	{ "NetworkKeepAliveDelay", INI::parseUnsignedInt, nullptr, NS_OFF(keepAliveDelay) },
	{ "NetworkDisconnectTime", INI::parseUnsignedInt, nullptr, NS_OFF(disconnectTime) },
	{ "NetworkPlayerTimeoutTime", INI::parseUnsignedInt, nullptr, NS_OFF(playerTimeoutTime) },
	{ "NetworkDisconnectScreenNotifyTime", INI::parseUnsignedInt, nullptr, NS_OFF(disconnectScreenNotifyTime) },
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

bool finish(Load &l, const std::string &what, NetworkSettings &out, std::string *error)
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

bool NetworkSettings::scan(const std::string &text, NetworkSettings &out, std::string *error)
{
	Load l(nullptr);
	return l.run("gamedata.ini", &text, error) && finish(l, "gamedata.ini", out, error);
}

bool NetworkSettings::load(ArchiveFileSystem &fs, NetworkSettings &out, std::string *error)
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
