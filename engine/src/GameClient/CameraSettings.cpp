// OpenBFME. GPL-3.0.
// See GameClient/CameraSettings.h.

#include "GameClient/CameraSettings.h"

#include "Common/ArchiveFileSystem.h"
#include "Common/AsciiString.h"
#include "Common/Dict.h"
#include "Common/INI.h"
#include "Common/INIException.h"
#include "Common/INI/INIBlockStubs.h"

#include <cstddef>
#include <cstdint>

namespace
{
struct State
{
	CameraSettings values;
	unsigned long long seen = 0;
	bool block = false;
};

enum : unsigned long long
{
	SEEN_MIN = 1ull << 0,
	SEEN_MAX = 1ull << 1,
	SEEN_PITCH = 1ull << 2,
	SEEN_YAW = 1ull << 3,
	SEEN_SCALAR = 1ull << 4,
	SEEN_LOCKDELTA = 1ull << 5,
	SEEN_EASE = 1ull << 6,
	SEEN_SAMPLE = 1ull << 7,
	SEEN_REPLAY = 1ull << 8,
	SEEN_CUTOFF = 1ull << 9,
	SEEN_ADJUST = 1ull << 10,
	SEEN_ENFORCE = 1ull << 11,
	SEEN_HSCROLL = 1ull << 12,
	SEEN_VSCROLL = 1ull << 13,
	SEEN_EDGE = 1ull << 14,
	SEEN_RAMP = 1ull << 15,
	SEEN_KBSCROLL = 1ull << 16,
	SEEN_KBDEFAULT = 1ull << 17,
	SEEN_KBROTATE = 1ull << 18,
	SEEN_PARTITION = 1ull << 19,
	SEEN_MOVEHINT = 1ull << 20
};

template <INIFieldParseProc Proc>
void parseNoting(INI *ini, void *instance, void *store, const void *userData)
{
	Proc(ini, instance, store, nullptr);
	static_cast<State *>(instance)->seen |= (unsigned long long)(std::uintptr_t)userData;
}

// RW 0x42EE37: scanReal, times 1000.0f, truncated to an int (milliseconds)
void parseSecondsToMilliseconds(INI *ini, void *instance, void *store, const void *userData)
{
	const float seconds = ini->scanReal(ini->getNextToken());
	const float ms = seconds * 1000.0f;
	*static_cast<int *>(store) = (int)ms;
	static_cast<State *>(instance)->seen |= (unsigned long long)(std::uintptr_t)userData;
}

// lane PLAY-1: MoveHintName (GlobalData + 0x10, parseAsciiString RW 0x42EE5E): written through the instance (the settings hold a std::string)
void parseMoveHintName(INI *ini, void *instance, void *, const void *)
{
	State *st = static_cast<State *>(instance);
	st->values.moveHintName = ini->getNextAsciiString();
	st->seen |= SEEN_MOVEHINT;
}

// lane HUD-5: ShowObjectHealth / VeterancyPipDrawObjectFilter, written through the instance (see CameraSettings.h)
void parseShowObjectHealth(INI *ini, void *instance, void *, const void *)
{
	State *st = static_cast<State *>(instance);
	INI::parseBool(ini, nullptr, &st->values.showObjectHealth, nullptr);
}
void parseVeterancyPipFilter(INI *ini, void *instance, void *, const void *)
{
	State *st = static_cast<State *>(instance);
	st->values.veterancyPipFilter = ObjectFilter::parserDefault();
	ParseObjectFilter(ini, nullptr, &st->values.veterancyPipFilter, nullptr);
	st->values.haveVeterancyPipFilter = true;
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
// the catch-all row (a GameData field this lane does not read: the full GlobalData table belongs to the lanes that own those fields): a `Field = value` line is
// consumed, a line without `=` opens a nested block that ends at its End
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
#define CS_OFF(member) (int)offsetof(State, values.member)

FieldParse kFields[] = {
	{ "DefaultCameraMinHeight", parseNoting<INI::parseReal>, SEEN_DATA(SEEN_MIN), CS_OFF(defaultMinHeight) },
	{ "DefaultCameraMaxHeight", parseNoting<INI::parseReal>, SEEN_DATA(SEEN_MAX), CS_OFF(defaultMaxHeight) },
	{ "DefaultCameraPitchAngle", parseNoting<INI::parseReal>, SEEN_DATA(SEEN_PITCH), CS_OFF(defaultPitchAngle) },
	{ "DefaultCameraYawAngle", parseNoting<INI::parseReal>, SEEN_DATA(SEEN_YAW), CS_OFF(defaultYawAngle) },
	{ "DefaultCameraScrollSpeedScalar", parseNoting<INI::parseReal>, SEEN_DATA(SEEN_SCALAR), CS_OFF(defaultScrollSpeedScalar) },
	{ "CameraLockHeightDelta", parseNoting<INI::parseReal>, SEEN_DATA(SEEN_LOCKDELTA), CS_OFF(lockHeightDelta) },
	{ "CameraEaseFactor", parseNoting<INI::parseReal>, SEEN_DATA(SEEN_EASE), CS_OFF(easeFactor) },
	{ "CameraTerrainSampleRadiusForHeight", parseNoting<INI::parseReal>, SEEN_DATA(SEEN_SAMPLE), CS_OFF(terrainSampleRadius) },
	{ "UseCameraInReplay", parseNoting<INI::parseBool>, SEEN_DATA(SEEN_REPLAY), CS_OFF(useCameraInReplay) },
	{ "ScrollAmountCutoff", parseNoting<INI::parseReal>, SEEN_DATA(SEEN_CUTOFF), CS_OFF(scrollAmountCutoff) },
	{ "CameraAdjustSpeed", parseNoting<INI::parseReal>, SEEN_DATA(SEEN_ADJUST), CS_OFF(cameraAdjustSpeed) },
	{ "EnforceMaxCameraHeight", parseNoting<INI::parseBool>, SEEN_DATA(SEEN_ENFORCE), CS_OFF(enforceMaxCameraHeight) },
	{ "HorizontalScrollSpeedFactor", parseNoting<INI::parseReal>, SEEN_DATA(SEEN_HSCROLL), CS_OFF(horizontalScrollSpeedFactor) },
	{ "VerticalScrollSpeedFactor", parseNoting<INI::parseReal>, SEEN_DATA(SEEN_VSCROLL), CS_OFF(verticalScrollSpeedFactor) },
	{ "ScreenEdgeScrollSpeedFactor", parseNoting<INI::parseReal>, SEEN_DATA(SEEN_EDGE), CS_OFF(screenEdgeScrollSpeedFactor) },
	{ "ScreenEdgeScrollRampTime", parseSecondsToMilliseconds, SEEN_DATA(SEEN_RAMP), CS_OFF(screenEdgeScrollRampTimeMs) },
	{ "KeyboardScrollSpeedFactor", parseNoting<INI::parseReal>, SEEN_DATA(SEEN_KBSCROLL), CS_OFF(keyboardScrollSpeedFactor) },
	{ "KeyboardDefaultScrollSpeedFactor", parseNoting<INI::parseReal>, SEEN_DATA(SEEN_KBDEFAULT), CS_OFF(keyboardDefaultScrollSpeedFactor) },
	{ "KeyboardCameraRotateSpeed", parseNoting<INI::parseReal>, SEEN_DATA(SEEN_KBROTATE), CS_OFF(keyboardCameraRotateSpeed) },
	{ "PartitionCellSize", parseNoting<INI::parseReal>, SEEN_DATA(SEEN_PARTITION), CS_OFF(partitionCellSize) },
	{ "MoveHintName", parseMoveHintName, nullptr, 0 },
	{ "ShowObjectHealth", parseShowObjectHealth, nullptr, 0 },
	{ "VeterancyPipDrawObjectFilter", parseVeterancyPipFilter, nullptr, 0 },
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
	static const Need needs[] = { { SEEN_MIN, "DefaultCameraMinHeight" }, { SEEN_MAX, "DefaultCameraMaxHeight" }, { SEEN_PITCH, "DefaultCameraPitchAngle" },
		{ SEEN_YAW, "DefaultCameraYawAngle" }, { SEEN_SCALAR, "DefaultCameraScrollSpeedScalar" }, { SEEN_LOCKDELTA, "CameraLockHeightDelta" },
		{ SEEN_SAMPLE, "CameraTerrainSampleRadiusForHeight" }, { SEEN_REPLAY, "UseCameraInReplay" },
		{ SEEN_CUTOFF, "ScrollAmountCutoff" }, { SEEN_ADJUST, "CameraAdjustSpeed" }, { SEEN_ENFORCE, "EnforceMaxCameraHeight" },
		{ SEEN_HSCROLL, "HorizontalScrollSpeedFactor" }, { SEEN_VSCROLL, "VerticalScrollSpeedFactor" }, { SEEN_EDGE, "ScreenEdgeScrollSpeedFactor" },
		{ SEEN_RAMP, "ScreenEdgeScrollRampTime" }, { SEEN_KBSCROLL, "KeyboardScrollSpeedFactor" }, { SEEN_KBROTATE, "KeyboardCameraRotateSpeed" }, { SEEN_PARTITION, "PartitionCellSize" } };
	for (const Need &n : needs)
	{
		if (!(seen & n.bit))
		{
			return n.name;
		}
	}
	return nullptr;
}

bool finish(Load &l, const char *what, CameraSettings &out, std::string *error)
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

bool CameraSettings::scan(const std::string &text, CameraSettings &out, std::string *error)
{
	Load l(nullptr);
	return l.run("gamedata.ini", &text, error) && finish(l, "gamedata.ini", out, error);
}

bool CameraSettings::load(ArchiveFileSystem &fs, CameraSettings &out, std::string *error)
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

MapCameraValues MapCameraValues::resolve(const CameraSettings &gd, const Dict *worldInfo)
{
	MapCameraValues v;
	v.minHeight = gd.defaultMinHeight;
	v.maxHeight = gd.defaultMaxHeight;
	v.pitchAngle = gd.defaultPitchAngle;
	v.yawAngle = gd.defaultYawAngle;
	v.scrollSpeedScalar = gd.defaultScrollSpeedScalar;
	v.heightSmoothness = gd.mapHeightSmoothness;
	v.groundMinHeight = -9999999.0f;
	v.groundMaxHeight = 9999999.0f;
	if (worldInfo)
	{
		struct Key
		{
			const char *name;
			float *dest;
		};
		const Key keys[] = { { "cameraMinHeight", &v.minHeight }, { "cameraMaxHeight", &v.maxHeight }, { "cameraPitchAngle", &v.pitchAngle },
			{ "cameraYawAngle", &v.yawAngle }, { "cameraScrollSpeedScalar", &v.scrollSpeedScalar }, { "cameraMapHeightSmoothnessScalar", &v.heightSmoothness },
			{ "cameraGroundMinHeight", &v.groundMinHeight }, { "cameraGroundMaxHeight", &v.groundMaxHeight } };
		for (const Key &k : keys)
		{
			bool exists = false;
			const float value = worldInfo->getReal(k.name, &exists);
			if (exists)
			{
				*k.dest = value;
				v.overridden.push_back(k.name);
			}
		}
	}
	return v;
}
