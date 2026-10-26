// OpenBFME. GPL-3.0.
// See CommandButtonHelp.h (lane HUD-6). Client-only: the help reads the logic, it never changes it.

#include "GameClient/CommandButtonHelp.h"
#include "GameClient/LocalizedText.h"

#include "Common/ArchiveFileSystem.h"
#include "Common/AsciiString.h"
#include "Common/BuildAssistant.h"
#include "Common/INI.h"
#include "Common/INIException.h"
#include "Common/ModEngineFeatures.h"
#include "Common/INI/INIBlockStubs.h"
#include "Common/Player.h"
#include "Common/ProductionPrerequisite.h"
#include "Common/Science.h"
#include "Common/SpecialPower.h"
#include "Common/Thing/ThingTemplate.h"
#include "Common/Upgrade.h"
#include "GameClient/GUI/GameTextSource.h"
#include "GameClient/GUI/GameWindowManager.h"
#include "GameClient/GUI/LoadScreenInfo.h"
#include "GameClient/GlobalLanguage.h"
#include "GameClient/InGameUIMessages.h"
#include "GameLogic/AttributeModifiers.h"
#include "GameLogic/CreateAHeroSystem.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/CastleModules.h"
#include "GameLogic/Module/ConstructionModules.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cwctype>

// ---------------------------------------------------------------------------------------------------------------------------------
// HelpBoxSettings: InGameUI (data\ini\ingameui.ini) and Language (data\ini\language.ini... the install's language.ini)
// ---------------------------------------------------------------------------------------------------------------------------------
namespace
{
struct SettingsState
{
	HelpBoxSettings values;
	InGameUIMessageSettings messages; // lane CHAT-1: the Message* rows (constructor defaults until a row is read)
	GlobalLanguage language; // INTEG-4: UI-4's GlobalLanguage reads the Language block (its full RW 0xBF4C20 table)
	unsigned seen = 0;
	bool inGameUi = false;
	MilitaryCaptionSettings caption; // lane CAMPUI-1
	unsigned captionSeen = 0;
};

// lane CAMPUI-1: the MilitaryCaption* rows seen (SettingsState::captionSeen)
enum : unsigned
{
	SEEN_MC_COLOR = 1u << 0, SEEN_MC_POSITION = 1u << 1, SEEN_MC_CENTERED = 1u << 2, SEEN_MC_TITLE_FONT = 1u << 3, SEEN_MC_TITLE_SIZE = 1u << 4,
	SEEN_MC_TITLE_BOLD = 1u << 5, SEEN_MC_FONT = 1u << 6, SEEN_MC_SIZE = 1u << 7, SEEN_MC_BOLD = 1u << 8, SEEN_MC_DELAY = 1u << 9,
	SEEN_MC_ALL = (1u << 10) - 1
};
template <INIFieldParseProc Proc>
void parseCaptionNoting(INI *ini, void *instance, void *store, const void *userData)
{
	Proc(ini, instance, store, nullptr);
	static_cast<SettingsState *>(instance)->captionSeen |= (unsigned)(std::uintptr_t)userData;
}

enum : unsigned
{
	SEEN_NAME_FONT = 1u << 0, SEEN_NAME_SIZE = 1u << 1, SEEN_NAME_BOLD = 1u << 2, SEEN_NAME_COLOR = 1u << 3,
	SEEN_COST_FONT = 1u << 4, SEEN_COST_SIZE = 1u << 5, SEEN_COST_BOLD = 1u << 6, SEEN_COST_COLOR = 1u << 7,
	SEEN_SHORTCUT_FONT = 1u << 8, SEEN_SHORTCUT_SIZE = 1u << 9, SEEN_SHORTCUT_BOLD = 1u << 10, SEEN_SHORTCUT_COLOR = 1u << 11,
	SEEN_DESC_FONT = 1u << 12, SEEN_DESC_SIZE = 1u << 13, SEEN_DESC_BOLD = 1u << 14, SEEN_DESC_COLOR = 1u << 15,
	SEEN_ALL = (1u << 16) - 1
};

template <INIFieldParseProc Proc>
void parseNoting(INI *ini, void *instance, void *store, const void *userData)
{
	Proc(ini, instance, store, nullptr);
	static_cast<SettingsState *>(instance)->seen |= (unsigned)(std::uintptr_t)userData;
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
size_t indentOf(const std::string &line)
{
	const size_t b = line.find_first_not_of(" \t");
	return b == std::string::npos ? line.size() : b;
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
// the catch-all row (the fields of InGameUI / Language this lane does not read): a `Field = value` line is consumed, a line without `=` opens a block
void skipField(INI *ini, void *, void *, const void *userData)
{
	const std::string header = ini->currentLineText();
	if (withoutComment(header).find('=') != std::string::npos)
	{
		return;
	}
	const size_t headerIndent = indentOf(header);
	// community PR #36: BFME1's InGameUI cursor blocks end at their first End whatever its indent (docs/BFME1.md; BFME1 data only)
	const bool bfme1 = ini->bfme1Data();
	for (;;)
	{
		const std::string *next = ini->peekNextLine();
		if (!next)
		{
			throw INIException(4, "Missing 'END' token.\n\nError parsing nested block '%s' in file '%s', line %i.\n", static_cast<const char *>(userData),
				ini->getFilename().c_str(), ini->currentSourceLine());
		}
		const std::string line = *next;
		ini->readLine();
		if (firstTokenIsEnd(line) && (bfme1 || indentOf(line) <= headerIndent))
		{
			return;
		}
	}
}

// RW 0x6A4E27 (the InGameUI row RadiusCursorTemplate): `RadiusCursorTemplate = <name>` opens a nested block that ends at its End
void skipNestedBlock(INI *ini, void *, void *, const void *userData)
{
	for (;;) // its fields up to the first End (the template has no nested block of its own)
	{
		const std::string *next = ini->peekNextLine();
		if (!next)
		{
			throw INIException(4, "Missing 'END' token.\n\nError parsing nested block '%s' in file '%s', line %i.\n", static_cast<const char *>(userData),
				ini->getFilename().c_str(), ini->currentSourceLine());
		}
		const std::string line = *next;
		ini->readLine();
		if (firstTokenIsEnd(line))
		{
			return;
		}
	}
}

#define SEEN(bit) ((const void *)(std::uintptr_t)(bit))
#define HB_OFF(member) (int)offsetof(SettingsState, values.member)
#define MSG_OFF(member) (int)offsetof(SettingsState, messages.member)

// lane CHAT-1: InGameUI's Message* rows (table at file offset 0x812420; the parsers RW 0x42F13E colour int, RW 0x42F2FA the coordinate pair, RW 0x42EE5E
// string, RW 0x42EC5E int, RW 0x42E558 bool); a row parsed into a field of a nested struct: the coordinate pair writes X then Y (ICoord2D)
void parseMessagePosition(INI *ini, void *, void *store, const void *)
{
	ICoord2D c;
	INI::parseICoord2D(ini, nullptr, &c, nullptr);
	static_cast<int *>(store)[0] = c.x;
	static_cast<int *>(store)[1] = c.y;
}
#define MC_OFF(member) (int)offsetof(SettingsState, caption.member)

// InGameUI rows RW 0xC126C0 .. 0xC127B0 (parseAsciiString RW 0x42EE5E, parseInt RW 0x42EC5E, parseBool RW 0x42E558, parseColorInt RW 0x42F13E)
FieldParse kInGameUiFields[] = {
	{ "HelpBoxNameFont", parseNoting<INI::parseAsciiString>, SEEN(SEEN_NAME_FONT), HB_OFF(name.name) },
	{ "HelpBoxNamePointSize", parseNoting<INI::parseInt>, SEEN(SEEN_NAME_SIZE), HB_OFF(name.pointSize) },
	{ "HelpBoxNameBold", parseNoting<INI::parseBool>, SEEN(SEEN_NAME_BOLD), HB_OFF(name.bold) },
	{ "HelpBoxNameColor", parseNoting<INI::parseColorInt>, SEEN(SEEN_NAME_COLOR), HB_OFF(name.color) },
	{ "HelpBoxCostFont", parseNoting<INI::parseAsciiString>, SEEN(SEEN_COST_FONT), HB_OFF(cost.name) },
	{ "HelpBoxCostPointSize", parseNoting<INI::parseInt>, SEEN(SEEN_COST_SIZE), HB_OFF(cost.pointSize) },
	{ "HelpBoxCostBold", parseNoting<INI::parseBool>, SEEN(SEEN_COST_BOLD), HB_OFF(cost.bold) },
	{ "HelpBoxCostColor", parseNoting<INI::parseColorInt>, SEEN(SEEN_COST_COLOR), HB_OFF(cost.color) },
	{ "HelpBoxShortcutFont", parseNoting<INI::parseAsciiString>, SEEN(SEEN_SHORTCUT_FONT), HB_OFF(shortcut.name) },
	{ "HelpBoxShortcutPointSize", parseNoting<INI::parseInt>, SEEN(SEEN_SHORTCUT_SIZE), HB_OFF(shortcut.pointSize) },
	{ "HelpBoxShortcutBold", parseNoting<INI::parseBool>, SEEN(SEEN_SHORTCUT_BOLD), HB_OFF(shortcut.bold) },
	{ "HelpBoxShortcutColor", parseNoting<INI::parseColorInt>, SEEN(SEEN_SHORTCUT_COLOR), HB_OFF(shortcut.color) },
	{ "HelpBoxDescriptionFont", parseNoting<INI::parseAsciiString>, SEEN(SEEN_DESC_FONT), HB_OFF(description.name) },
	{ "HelpBoxDescriptionPointSize", parseNoting<INI::parseInt>, SEEN(SEEN_DESC_SIZE), HB_OFF(description.pointSize) },
	{ "HelpBoxDescriptionBold", parseNoting<INI::parseBool>, SEEN(SEEN_DESC_BOLD), HB_OFF(description.bold) },
	{ "HelpBoxDescriptionColor", parseNoting<INI::parseColorInt>, SEEN(SEEN_DESC_COLOR), HB_OFF(description.color) },
	{ "MessageColor1", INI::parseColorInt, nullptr, MSG_OFF(color1) },
	{ "MessageColor2", INI::parseColorInt, nullptr, MSG_OFF(color2) },
	{ "MessagePosition", parseMessagePosition, nullptr, MSG_OFF(positionX) },
	{ "MessagePositionLW", parseMessagePosition, nullptr, MSG_OFF(positionLWX) },
	{ "MessageFont", INI::parseAsciiString, nullptr, MSG_OFF(font) },
	{ "MessagePointSize", INI::parseInt, nullptr, MSG_OFF(pointSize) },
	{ "MessageBold", INI::parseBool, nullptr, MSG_OFF(bold) },
	{ "MessageDelayMS", INI::parseInt, nullptr, MSG_OFF(delayMS) },
	// lane CAMPUI-1: rows RW 0xC124A0 .. 0xC12530 (InGameUI + 0x83C colour, + 0x84C / + 0x850 position, + 0x854 centred, + 0x858 .. fonts, + 0x870 delay)
	{ "MilitaryCaptionColor", parseCaptionNoting<INI::parseRGBAColorInt>, SEEN(SEEN_MC_COLOR), MC_OFF(color) },
	{ "MilitaryCaptionPosition", parseCaptionNoting<INI::parseCoord2D>, SEEN(SEEN_MC_POSITION), MC_OFF(position) },
	{ "MilitaryCaptionCentered", parseCaptionNoting<INI::parseBool>, SEEN(SEEN_MC_CENTERED), MC_OFF(centered) },
	{ "MilitaryCaptionTitleFont", parseCaptionNoting<INI::parseAsciiString>, SEEN(SEEN_MC_TITLE_FONT), MC_OFF(title.name) },
	{ "MilitaryCaptionTitlePointSize", parseCaptionNoting<INI::parseInt>, SEEN(SEEN_MC_TITLE_SIZE), MC_OFF(title.pointSize) },
	{ "MilitaryCaptionTitleBold", parseCaptionNoting<INI::parseBool>, SEEN(SEEN_MC_TITLE_BOLD), MC_OFF(title.bold) },
	{ "MilitaryCaptionFont", parseCaptionNoting<INI::parseAsciiString>, SEEN(SEEN_MC_FONT), MC_OFF(text.name) },
	{ "MilitaryCaptionPointSize", parseCaptionNoting<INI::parseInt>, SEEN(SEEN_MC_SIZE), MC_OFF(text.pointSize) },
	{ "MilitaryCaptionBold", parseCaptionNoting<INI::parseBool>, SEEN(SEEN_MC_BOLD), MC_OFF(text.bold) },
	{ "MilitaryCaptionDelayMS", parseCaptionNoting<INI::parseInt>, SEEN(SEEN_MC_DELAY), MC_OFF(delayMs) },
	{ "RadiusCursorTemplate", skipNestedBlock, "RadiusCursorTemplate", 0 },
	{ nullptr, skipField, "InGameUI", 0 }
};
class SettingsLoad
{
public:
	explicit SettingsLoad(ArchiveFileSystem *fs)
	{
		m_env.fileSystem = fs;
		RegisterRecordingBlockStubs(m_env.blocks, m_recorder, { "InGameUI", "Language" }, StubExtent::Lenient);
		m_env.blocks.registerBlock("InGameUI", [this](INI *ini) {
			m_state.inGameUi = true;
			ini->initFromINI(&m_state, kInGameUiFields);
		});
		m_state.language.registerBlocks(m_env.blocks);
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
	SettingsState &state() { return m_state; }

private:
	INIEnvironment m_env;
	INIBlockRecorder m_recorder;
	SettingsState m_state;
};

// RW 0x6A030D / 0x69E29E / 0x69E320 / 0x69E3A2: the InGameUI descriptor, its name / size / bold replaced by the GlobalLanguage font when that names one
void applyLanguage(HelpBoxFont &f, const FontDesc &language)
{
	if (!language.name.empty())
	{
		f.name = language.name;
		f.pointSize = language.size;
		f.bold = language.bold;
	}
}

bool finish(SettingsLoad &l, HelpBoxSettings &out, std::string *error)
{
	SettingsState &s = l.state();
	if (!s.inGameUi || (s.seen & SEEN_ALL) != SEEN_ALL)
	{
		if (error)
		{
			*error = !s.inGameUi ? "ingameui.ini: no InGameUI block" : "ingameui.ini: InGameUI lacks a HelpBox field (mask " + std::to_string(s.seen) + ")";
		}
		return false;
	}
	if (!s.language.loaded)
	{
		if (error)
		{
			*error = "language.ini: no Language block";
		}
		return false;
	}
	out = s.values;
	applyLanguage(out.name, s.language.helpBoxNameFont);
	applyLanguage(out.cost, s.language.helpBoxCostFont);
	applyLanguage(out.shortcut, s.language.helpBoxShortcutFont);
	applyLanguage(out.description, s.language.helpBoxDescriptionFont);
	out.loaded = true;
	return true;
}

// lane CAMPUI-1: MilitaryCaptionSettings (the same two files; the help box's rows are not required here)
bool finishCaption(SettingsLoad &l, MilitaryCaptionSettings &out, std::string *error)
{
	SettingsState &s = l.state();
	if (!s.inGameUi || (s.captionSeen & SEEN_MC_ALL) != SEEN_MC_ALL)
	{
		if (error)
		{
			*error = !s.inGameUi ? "ingameui.ini: no InGameUI block" : "ingameui.ini: InGameUI lacks a MilitaryCaption field (mask " + std::to_string(s.captionSeen) + ")";
		}
		return false;
	}
	if (!s.language.loaded)
	{
		if (error)
		{
			*error = "language.ini: no Language block";
		}
		return false;
	}
	out = s.caption;
	applyLanguage(out.title, s.language.militaryCaptionTitleFont);
	applyLanguage(out.text, s.language.militaryCaptionFont);
	out.loaded = true;
	return true;
}

// UnicodeString::format with one %d or %s (the retail labels carry one)
UnicodeString formatOne(const UnicodeString &fmt, const UnicodeString &value)
{
	return LocalizedText::format(fmt, { value });
}
UnicodeString u16(const std::string &ascii)
{
	return UnicodeString(ascii.begin(), ascii.end());
}
bool hasText(const UnicodeString &s)
{
	return !s.empty();
}
GameFont gameFont(const HelpBoxFont &f)
{
	GameFont g;
	g.name = f.name;
	g.pointSize = f.pointSize;
	g.bold = f.bold;
	return g;
}
// WWMath::Float_To_Long(floor(v + 0.5f))
float rounded(float v)
{
	return (float)(long)std::floor(v + 0.5f);
}
} // namespace

bool HelpBoxSettings::scan(const std::string &inGameUi, const std::string &language, HelpBoxSettings &out, std::string *error)
{
	SettingsLoad l(nullptr);
	return l.run("ingameui.ini", &inGameUi, error) && l.run("language.ini", &language, error) && finish(l, out, error);
}

bool MilitaryCaptionSettings::scan(const std::string &inGameUi, const std::string &language, MilitaryCaptionSettings &out, std::string *error)
{
	SettingsLoad l(nullptr);
	return l.run("ingameui.ini", &inGameUi, error) && l.run("language.ini", &language, error) && finishCaption(l, out, error);
}

bool MilitaryCaptionSettings::load(ArchiveFileSystem &fs, MilitaryCaptionSettings &out, std::string *error)
{
	for (const char *file : { "data\\ini\\ingameui.ini", "language.ini" })
	{
		if (!fs.doesFileExist(file))
		{
			if (error)
			{
				*error = std::string("file not found in any mounted archive: ") + file;
			}
			return false;
		}
	}
	SettingsLoad l(&fs);
	return l.run("data\\ini\\ingameui.ini", nullptr, error) && l.run("language.ini", nullptr, error) && finishCaption(l, out, error);
}

bool HelpBoxSettings::load(ArchiveFileSystem &fs, HelpBoxSettings &out, std::string *error)
{
	for (const char *file : { "data\\ini\\ingameui.ini", "language.ini" })
	{
		if (!fs.doesFileExist(file))
		{
			if (error)
			{
				*error = std::string("file not found in any mounted archive: ") + file;
			}
			return false;
		}
	}
	SettingsLoad l(&fs);
	return l.run("data\\ini\\ingameui.ini", nullptr, error) && l.run("language.ini", nullptr, error) && finish(l, out, error);
}

namespace
{
bool finishMessages(SettingsLoad &l, InGameUIMessageSettings &out, std::string *error)
{
	SettingsState &s = l.state();
	if (!s.inGameUi)
	{
		if (error)
		{
			*error = "ingameui.ini: no InGameUI block";
		}
		return false;
	}
	if (!s.language.loaded)
	{
		if (error)
		{
			*error = "language.ini: no Language block";
		}
		return false;
	}
	out = s.messages;
	// RW 0x69EDA7 .. 0x69EDD7: Language's MessageFont (+ 0x38) replaces the name, size and bold when it names a font
	if (!s.language.messageFont.name.empty())
	{
		out.font = s.language.messageFont.name;
		out.pointSize = s.language.messageFont.size;
		out.bold = s.language.messageFont.bold;
	}
	out.loaded = true;
	return true;
}
} // namespace

bool InGameUIMessageSettings::scan(const std::string &inGameUi, const std::string &language, InGameUIMessageSettings &out, std::string *error)
{
	SettingsLoad l(nullptr);
	return l.run("ingameui.ini", &inGameUi, error) && l.run("language.ini", &language, error) && finishMessages(l, out, error);
}

bool InGameUIMessageSettings::load(ArchiveFileSystem &fs, InGameUIMessageSettings &out, std::string *error)
{
	for (const char *file : { "data\\ini\\ingameui.ini", "language.ini" })
	{
		if (!fs.doesFileExist(file))
		{
			if (error)
			{
				*error = std::string("file not found in any mounted archive: ") + file;
			}
			return false;
		}
	}
	SettingsLoad l(&fs);
	return l.run("data\\ini\\ingameui.ini", nullptr, error) && l.run("language.ini", nullptr, error) && finishMessages(l, out, error);
}

template <typename T, INIFieldParseProc Parse>
static bool loadInGameUiValue(ArchiveFileSystem *fs, const std::string *memory, const char *field, T &out, std::string *error)
{
	const char *file = "data\\ini\\ingameui.ini";
	if (!memory && (!fs || !fs->doesFileExist(file)))
	{
		if (error)
		{
			*error = std::string("file not found in any mounted archive: ") + file;
		}
		return false;
	}
	struct State
	{
		T value{};
		bool seen = false, inGameUi = false;
	} state;
	const FieldParse fields[] = {
		{ field, [](INI *ini, void *instance, void *store, const void *) {
			  Parse(ini, instance, store, nullptr);
			  static_cast<State *>(instance)->seen = true;
		  }, nullptr, (int)offsetof(State, value) },
		{ "RadiusCursorTemplate", skipNestedBlock, "RadiusCursorTemplate", 0 },
		{ nullptr, skipField, "InGameUI", 0 }
	};
	INIEnvironment env;
	env.fileSystem = fs;
	INIBlockRecorder recorder;
	RegisterRecordingBlockStubs(env.blocks, recorder, { "InGameUI" }, StubExtent::Lenient);
	env.blocks.registerBlock("InGameUI", [&](INI *ini) {
		state.inGameUi = true;
		ini->initFromINI(&state, fields);
	});
	try
	{
		INI ini(env);
		if (memory)
		{
			ini.loadMemory(file, std::vector<std::uint8_t>(memory->begin(), memory->end()), INI_LOAD_OVERWRITE);
		}
		else
		{
			ini.load(file, INI_LOAD_OVERWRITE);
		}
	}
	catch (const std::exception &e)
	{
		if (error)
		{
			*error = std::string(file) + ": " + e.what();
		}
		return false;
	}
	if (!state.inGameUi || !state.seen)
	{
		if (error)
		{
			*error = std::string(file) + (!state.inGameUi ? ": no InGameUI block" : std::string(": InGameUI lacks ") + field);
		}
		return false;
	}
	out = state.value;
	return true;
}

bool loadInGameUiString(ArchiveFileSystem *fs, const std::string *memory, const char *field, std::string &out, std::string *error)
{
	return loadInGameUiValue<std::string, INI::parseAsciiString>(fs, memory, field, out, error);
}

bool loadInGameUiUnsigned(ArchiveFileSystem *fs, const std::string *memory, const char *field, unsigned &out, std::string *error)
{
	return loadInGameUiValue<unsigned, INI::parseUnsignedInt>(fs, memory, field, out, error);
}

bool loadInGameUiReal(ArchiveFileSystem *fs, const std::string *memory, const char *field, float &out, std::string *error)
{
	return loadInGameUiValue<float, INI::parseReal>(fs, memory, field, out, error);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// the text (RW 0x807A81)
// ---------------------------------------------------------------------------------------------------------------------------------
// lane HOVER-1: a ThingTemplate label row (parseLabel RW 0x73D3E0 keeps the label at the row's offset and its fetched text at offset + 4): the label as the
// row gives it, "" when the row was never set
std::string CommandButtonHelp::templateLabel(const ThingTemplate &tt, const std::string &field)
{
	const FieldValue *v = tt.findField(field);
	if (const std::string *label = v ? std::get_if<std::string>(v) : nullptr)
	{
		return *label;
	}
	if (const RawTokens *raw = v ? std::get_if<RawTokens>(v) : nullptr)
	{
		return raw->tokens.empty() ? std::string() : raw->tokens.front();
	}
	return std::string();
}

// the fetched text of a template label row ("" for an unset row; a label the string file lacks is its MISSING text, as retail fetched it at load)
UnicodeString CommandButtonHelp::templateText(const ThingTemplate &tt, const std::string &field, const GameTextSource *text)
{
	const std::string label = templateLabel(tt, field);
	return label.empty() ? UnicodeString() : fetchOrMissing(text, label);
}

namespace
{
// RW 0x8077D1 (BFME2 decomp Rva00405B8DTest.cpp OldSchoolHelpBoxContentSource::allRequiredUpgradesComplete, tier A): each needed upgrade, by its Type
// (OBJECT: the object has it, PLAYER: the object's controlling player completed it, any other type: false at once); NeededUpgradeAny = any of them
bool allRequiredUpgradesComplete(const Object &obj, const CommandButton &b, std::string *unresolved)
{
	for (const std::string &name : b.m_neededUpgrade)
	{
		const UpgradeTemplate *u = TheUpgradeCenter ? TheUpgradeCenter->findUpgrade(name) : nullptr;
		if (!u)
		{
			*unresolved = name; // retail resolves the names at parse time: an unknown name never reaches here
			continue;
		}
		bool r;
		if (u->getUpgradeType() == UPGRADE_TYPE_OBJECT)
		{
			r = obj.hasUpgrade(u);
		}
		else if (u->getUpgradeType() == UPGRADE_TYPE_PLAYER)
		{
			const Player *p = obj.getControllingPlayer();
			r = p && p->hasUpgradeComplete(u);
		}
		else
		{
			return false;
		}
		if (b.m_neededUpgradeAny)
		{
			if (r)
			{
				return true;
			}
		}
		else if (!r)
		{
			return false;
		}
	}
	return !b.m_neededUpgradeAny;
}
} // namespace

namespace
{
// lane MODS-3: edain-unchained.description-timers (its 0xEDD07B at RW 0x8086AB, every exit of RW 0x807A81): one line per button, the frames / 5 in whole
// seconds; under 5 frames, or a label the string table lacks (silent until the mod adds it), no line
void addTimerLine(CommandButtonHelp::Text &t, const CommandButton &b, const GameTextSource *text, GameLogic &logic, Player *player, Object *selection)
{
	std::string label;
	int frames = 0;
	const SpecialPowerTemplate *sp = (TheSpecialPowerStore && !b.m_specialPowerName.empty()) ? TheSpecialPowerStore->findSpecialPowerTemplate(b.m_specialPowerName) : nullptr;
	const UpgradeTemplate *upgrade = (TheUpgradeCenter && !b.m_upgradeName.empty()) ? TheUpgradeCenter->findUpgrade(b.m_upgradeName) : nullptr;
	const ThingTemplate *tmpl = b.getThingTemplate(); // RW 0x75D1DC
	if (sp)
	{
		// 1. the cooldown: the selection's module (RW 0x68C26D), else the player's SPELL_BOOK object's (RW 0x6ABABD over RW 0x6AAE3C)
		const Object *owner = selection;
		const SpecialPowerModuleInterface *m = owner ? SpecialPowerModules::findModule(*owner, sp) : nullptr;
		if (!m && player)
		{
			static const int spellBook = ObjectTemplateInfoBuilder::kindOfIndex("SPELL_BOOK");
			for (Object *o = logic.getFirstObject(); o && spellBook >= 0; o = o->getNextObject())
			{
				if (o->isKindOf((unsigned)spellBook) && o->getControllingPlayer() == player)
				{
					owner = o;
					m = SpecialPowerModules::findModule(*o, sp);
					break;
				}
			}
		}
		// the retail recharge (SpecialPowerModule::startPowerRecharge RW 0x896E31): ReloadTime x (the player's discount + 1 when the power respects it) x the
		// owner's RECHARGE_TIME product, truncated
		float playerScale = 1.0f, modifierScale = 1.0f;
		if ((sp->getFlags() & SPF_RESPECT_RECHARGE_TIME_DISCOUNT) && player)
		{
			playerScale = player->science().rechargeDiscount() + 1.0f;
		}
		if (owner)
		{
			owner->attributeModifierProduct(ATTRIBUTE_RECHARGE_TIME, nullptr, true, modifierScale);
		}
		const int total = (int)((double)sp->getReloadTime() * playerScale * modifierScale);
		if (total <= 0)
		{
			return;
		}
		// the remaining frames of a module still recharging (INFERENCE: a paused module's ready frame is read too; its 0xEDD07B tells the module kinds apart)
		const int remaining = m && !m->isReadyForDisplay() ? (int)(m->getReadyFrame() - logic.getFrame()) : 0;
		label = remaining > 0 ? "TOOLTIP:CooldownRemaining" : "TOOLTIP:Cooldown";
		frames = remaining > 0 ? remaining : total;
	}
	else if (upgrade)
	{
		// 2. the research time (RW 0x66F1A8), none for an upgrade already owned (RW 0x691421 / 0x6AC2AF)
		if (!player)
		{
			return;
		}
		const bool owned = upgrade->getUpgradeType() == UPGRADE_TYPE_OBJECT ? (selection && selection->hasUpgrade(upgrade)) : player->hasUpgradeComplete(upgrade);
		if (owned)
		{
			return;
		}
		label = "TOOLTIP:ResearchTime";
		frames = upgrade->calcTimeToBuild(player);
	}
	else if (tmpl && b.m_command != GUI_COMMAND_PURCHASE_SCIENCE && b.m_command != GUI_COMMAND_HORDE_SET_FORMATION)
	{
		// 3. the build time (RW 0x73C39E), not for a CREATE_A_HERO template
		const int cahBit = ObjectTemplateInfoBuilder::kindOfIndex("CREATE_A_HERO");
		if (cahBit >= 0 && MaskTest(ObjectTemplateInfoBuilder::build(*tmpl).kindOf, (unsigned)cahBit))
		{
			return;
		}
		label = "TOOLTIP:BuildTime";
		frames = BuildAssistant::calcTimeToBuild(*tmpl, player, selection, -1, logic.productionSettings(), logic);
	}
	if (label.empty() || frames < 5) // LOGICFRAMES_PER_SECOND (RW 0xD9F608)
	{
		return;
	}
	std::u16string fmt;
	if (!text || !text->fetch(label, fmt))
	{
		return;
	}
	if (hasText(t.description))
	{
		t.description += u"\n"; // RW 0xBDBC40
	}
	t.description += formatOne(fmt, u16(std::to_string(frames / 5)));
}
} // namespace

CommandButtonHelp::Text CommandButtonHelp::compose(const ControlBarButton &button, const GameTextSource *text, GameLogic &logic, Player *player, Player *localPlayer,
                                                   Object *selection, const std::string &faction, const std::vector<ObjectID> *selected)
{
	Text t = composeRetail(button, text, logic, player, localPlayer, selection, faction, selected);
	if (button.button && ModEngineFeatures::has(ModEngineFeature::EdainUnchainedDescriptionTimers))
	{
		// RW 0x8086AB's player: the selection's controller, else the local player (the caller's `player`)
		addTimerLine(t, *button.button, text, logic, player, selection);
	}
	return t;
}

CommandButtonHelp::Text CommandButtonHelp::composeRetail(const ControlBarButton &button, const GameTextSource *text, GameLogic &logic, Player *player, Player *localPlayer,
                                                         Object *selection, const std::string &faction, const std::vector<ObjectID> *selected)
{
	Text t;
	t.resourceSuffix = faction; // RW 0x8086B5: the Palantir's faction (+ 0xF8), when the Palantir exists
	const CommandButton *b = button.button;
	if (!b)
	{
		return t; // RW 0x807B0C: no button, only the suffix
	}
	const int cmd = b->m_command;
	const ThingTemplate *tmpl = b->getThingTemplate();                                                     // RW 0x75D1DC
	const UpgradeTemplate *upgrade = (TheUpgradeCenter && !b->m_upgradeName.empty()) ? TheUpgradeCenter->findUpgrade(b->m_upgradeName) : nullptr; // + 0x24
	const bool upgradeCommand = cmd == GUI_COMMAND_PLAYER_UPGRADE || cmd == GUI_COMMAND_OBJECT_UPGRADE || cmd == GUI_COMMAND_CASTLE_UPGRADE; // RW 0x807BE0
	const bool mp = player ? player->science().mode().skirmishOrMultiplayer : true; // RW 0x5FEC64's game mode test

	// the science (RW 0x807B29 .. 0x807BDD): with more than one, PURCHASE_SCIENCE takes the first the player lacks (the last when it has them all, or with
	// no player); any other command takes the first and locks it (no science cost); with exactly one, that one
	ScienceType science = SCIENCE_INVALID;
	bool scienceLocked = false;
	auto scienceAt = [](const std::string &name) { return TheScienceStore ? TheScienceStore->getScienceFromInternalName(name) : SCIENCE_INVALID; };
	if (b->m_science.size() > 1)
	{
		for (size_t i = 0; i < b->m_science.size(); ++i)
		{
			science = scienceAt(b->m_science[i]);
			if (cmd != GUI_COMMAND_PURCHASE_SCIENCE)
			{
				if (player && !player->hasScience(science) && i > 0)
				{
					science = scienceAt(b->m_science[i - 1]);
				}
				scienceLocked = true;
				break;
			}
			if (player && !player->hasScience(science))
			{
				break;
			}
		}
	}
	else if (b->m_science.size() == 1)
	{
		science = scienceAt(b->m_science.front());
	}

	// the repair cost (RW 0x807BFD .. 0x807CAF, START_SELF_REPAIR / START_NEIGHBORHOOD_REPAIR with a selection): the cost of the interface the object's
	// behavior modules answer at vslot 0x84 (RW 0x68C3E6), GettingBuiltBehavior's (lane ROTWK-C): vslot 0x30 rebuildCost (RW 0x85657E) for self repair, vslot 0x50
	// (RW 0x857074: a dead source's own cost when it can rebuild, RW 0x856501, plus the dead equivalent objects of contiguous IDs above, then below) for the
	// neighborhood, shown truncated (int). Both pass one gate: an object with any of FS_POWER / FS_FACTORY / FS_BASE_DEFENSE / FS_TECHNOLOGY / FS_CASH_PRODUCER
	// (RW 0x68DAE6 = Object::rva0028D491, bits 62 .. 65 and 135) shows it only at zero health, any other only below full health (body vslot 0x14 != 1)
	if ((cmd == GUI_COMMAND_START_SELF_REPAIR || cmd == GUI_COMMAND_START_NEIGHBORHOOD_REPAIR) && selection && player)
	{
		const auto *building = dynamic_cast<const GettingBuiltBehavior *>(selection->findModule("GettingBuiltBehavior"));
		const BodyModuleInterface *body = selection->getBodyModule();
		bool factionStructure = false;
		for (const char *kind : { "FS_POWER", "FS_FACTORY", "FS_BASE_DEFENSE", "FS_TECHNOLOGY", "FS_CASH_PRODUCER" })
		{
			const int bit = ObjectTemplateInfoBuilder::kindOfIndex(kind);
			factionStructure = factionStructure || (bit >= 0 && MaskTest(selection->getKindOf(), (unsigned)bit));
		}
		if (building && body && (factionStructure ? body->getHealth() == 0.0f : body->getHealth() / body->getMaxHealth() != 1.0f))
		{
			const float cost = cmd == GUI_COMMAND_START_SELF_REPAIR ? building->rebuildCost(*player) : building->neighborhoodRebuildCost(*player);
			t.cost = formatOne(fetchOrMissing(text, "TOOLTIP:Cost"), u16(std::to_string((int)cost)));
		}
	}

	// SPELL_BOOK (RW 0x807CBA .. 0x807D88): with a local player, the first of the power's RequiredSciences the local player owns; when that science is
	// disabled or hidden for it (RW 0x6AC25F / 0x6AC287) or lacks its root prerequisites (RW 0x5FF4D7 on the player's science record), the description is
	// "TOOLTIP:ScienceDisabled"
	if (localPlayer && cmd == GUI_COMMAND_SPELL_BOOK && !b->m_specialPowerName.empty() && TheSpecialPowerStore)
	{
		if (const SpecialPowerTemplate *sp = TheSpecialPowerStore->findSpecialPowerTemplate(b->m_specialPowerName)) // RW 0x6A0713 (the final override's + 0x24)
		{
			for (ScienceType st : sp->getFinalOverride()->getRequiredSciences())
			{
				if (localPlayer->hasScience(st))
				{
					if (st != SCIENCE_INVALID &&
					    (localPlayer->isScienceDisabled(st) || localPlayer->isScienceHidden(st) || !TheScienceStore ||
					     !TheScienceStore->playerHasRootPrereqsForScience(localPlayer->science(), st)))
					{
						t.description = fetchOrMissing(text, "TOOLTIP:ScienceDisabled");
					}
					break;
				}
			}
		}
	}

	// the description (RW 0x807D8F .. 0x807EB0): with no text yet and a DescriptLabel (RW 0x807DA3), the label's text; only then an upgrade command of a
	// selection whose player (+ -0x20) lacks the upgrade (RW 0x6AB2E1) gets a warning after "\n\n" (RW 0xC4F008): the selection's production queue at
	// 0x14 entries (vslot 0x44 getProductionCount, RW 0x807E29) -> "TOOLTIP:TooltipCannotPurchaseBecauseQueueFull", else, when the LOCAL player (RW 0xDE4928
	// + 0x10) cannot afford it (RW 0x66F492), "TOOLTIP:TooltipNotEnoughMoneyToBuild"
	if (t.description.empty() && !button.descriptLabel.empty())
	{
		t.description = fetchOrMissing(text, button.descriptLabel);
		if (selection && upgrade && player && !player->hasUpgradeComplete(upgrade) && upgradeCommand)
		{
			ProductionUpdateInterface *pu = selection->getProductionUpdate();
			if (pu && pu->getProductionCount() == 0x14)
			{
				t.description += u"\n\n";
				t.description += fetchOrMissing(text, "TOOLTIP:TooltipCannotPurchaseBecauseQueueFull");
			}
			else if (!UpgradeCenter::canAffordUpgrade(localPlayer, upgrade, selection))
			{
				t.description += u"\n\n";
				t.description += fetchOrMissing(text, "TOOLTIP:TooltipNotEnoughMoneyToBuild");
			}
		}
	}
	// the name (RW 0x807EB0): the TextLabel
	if (!button.textLabel.empty())
	{
		t.name = fetchOrMissing(text, button.textLabel);
	}
	if (tmpl && (!upgrade || !upgradeCommand) && cmd != GUI_COMMAND_PURCHASE_SCIENCE && cmd != GUI_COMMAND_HORDE_SET_FORMATION)
	{
		// RW 0x807F30: the build cost (BUILD_FOR_FREE, KindOf + 0x11B bit 0x20, is free)
		const int cost = BuildAssistant::calcCostToBuild(*tmpl, player, selection, -1);
		const int freeBit = ObjectTemplateInfoBuilder::kindOfIndex("BUILD_FOR_FREE");
		const bool buildForFree = freeBit >= 0 && MaskTest(ObjectTemplateInfoBuilder::build(*tmpl).kindOf, (unsigned)freeBit);
		if (!buildForFree && cost > 0)
		{
			t.cost = formatOne(fetchOrMissing(text, "TOOLTIP:Cost"), u16(std::to_string(cost)));
		}
		else
		{
			t.cost = fetchOrMissing(text, "TOOLTIP:CostFree");
		}
		const int cp = BuildAssistant::commandPoints(*tmpl); // + 0x628
		if (cp > 0)
		{
			t.commandPoints = formatOne(fetchOrMissing(text, "TOOLTIP:CommandPoints"), u16(std::to_string(cp)));
		}
		std::u16string requirements;
		for (const ProductionPrerequisite &prerequisite : tmpl->getFinalOverride()->productionPrerequisites())
		{
			const std::u16string requirement = prerequisite.getRequiresList(player, logic, text);
			if (!requirement.empty())
			{
				if (!requirements.empty())
				{
					requirements += u", ";
				}
				requirements += requirement;
			}
		}
		if (!requirements.empty())
		{
			const std::u16string requirementText = formatOne(fetchOrMissing(text, "CONTROLBAR:Requirements"), requirements);
			if (!t.description.empty())
			{
				t.description += u"\n";
			}
			t.description += requirementText;
		}
		if (localPlayer && !BuildAssistant::playerAllowedToBuild(*localPlayer, tmpl, logic)) // RW 0x8081F7 (RW 0x6AC856): the local player (+ -0x2C)
		{
			if (!t.description.empty())
			{
				t.description += u"\n";
			}
			t.description += fetchOrMissing(text, "TOOLTIP:BuildDisabled");
		}
		return t;
	}
	if (upgrade)
	{
		// an upgrade (RW 0x808144 .. 0x8083C7): `has` starts as the player's completed upgrade (RW 0x6AC2AF)
		const bool t6 = cmd == GUI_COMMAND_PLAYER_UPGRADE, t7 = cmd == GUI_COMMAND_OBJECT_UPGRADE, t8 = cmd == GUI_COMMAND_CASTLE_UPGRADE;
		bool has = player && player->hasUpgradeComplete(upgrade);
		enum
		{
			NOTHING,
			ALREADY,
			CONFLICT,
			LACKS
		} label = NOTHING;
		if (has)
		{
			label = ALREADY;
		}
		else if (selection)
		{
			std::string unresolved;
			// RW 0x80818C: NEED_UPGRADE (Options 0x40), the object lacks the upgrade and its NeededUpgrade list is not complete (RW 0x8077D1)
			if ((b->m_options & COMMAND_OPTION_NEED_UPGRADE) && !selection->hasUpgrade(upgrade) && !allRequiredUpgradesComplete(*selection, *b, &unresolved))
			{
				label = LACKS;
			}
			else
			{
				// RW 0x8081C0: one selected object: `has` = it has the upgrade; an OBJECT_UPGRADE no module of it answers (RW 0x694914) conflicts; more than
				// one: `has` = all of them have it, the conflict = none of them is affected (the walk stops when both are false)
				bool unaffected = false;
				const size_t count = selected ? selected->size() : 1;
				if (count == 1)
				{
					has = selection->hasUpgrade(upgrade);
					unaffected = t7 && !selection->affectedByUpgrade(upgrade);
				}
				else
				{
					has = true;
					unaffected = true;
					for (ObjectID id : *selected)
					{
						const Object *o = logic.findObjectByID(id);
						if (o)
						{
							has = has && o->hasUpgrade(upgrade);
							unaffected = unaffected && !o->affectedByUpgrade(upgrade);
						}
						if (!has && !unaffected)
						{
							break;
						}
					}
				}
				if (unaffected && !has)
				{
					label = CONFLICT;
				}
				else if (has)
				{
					label = ALREADY;
				}
			}
			if (!unresolved.empty())
			{
				t.unported.push_back("NeededUpgrade " + unresolved + " names no upgrade (retail resolves it at parse time)");
			}
		}
		if (label == LACKS) // RW 0x8082C7: + 0x78, else the default
		{
			t.description = b->m_lacksPrerequisiteLabel.empty() ? fetchOrMissing(text, "TOOLTIP:LacksPrerequisiteUpgradeDefault")
			                                                     : fetchOrMissing(text, b->m_lacksPrerequisiteLabel);
		}
		else if (label == CONFLICT) // RW 0x80820F: + 0x74, else the default
		{
			t.description = b->m_conflictingLabel.empty() ? fetchOrMissing(text, "TOOLTIP:HasConflictingUpgradeDefault") : fetchOrMissing(text, b->m_conflictingLabel);
		}
		else if (label == ALREADY)
		{
			if (t6 || t7 || t8) // RW 0x808329: + 0x70, else the default
			{
				const UnicodeString purchased = b->m_purchasedLabel.empty() ? fetchOrMissing(text, "TOOLTIP:AlreadyUpgradedDefault") : fetchOrMissing(text, b->m_purchasedLabel);
				if (ModEngineFeatures::has(ModEngineFeature::EdainUnchainedUpgradeDescription))
				{
					// lane MODS-3: edain-unchained.upgrade-description (its 0xEDE00A at RW 0x808371): the purchased text follows the description after a
					// blank line instead of replacing it
					if (hasText(t.description))
					{
						t.description += u"\n\n"; // RW 0xC4F008
					}
					t.description += purchased;
				}
				else
				{
					t.description = purchased;
				}
			}
			if (has)
			{
				return t; // RW 0x808386: no cost line for an upgrade the selection has
			}
		}
		const int cost = upgrade->calcCostToBuild(player, selection); // RW 0x808390 -> 0x66F2C8
		if (cost > 0)
		{
			t.cost = formatOne(fetchOrMissing(text, "TOOLTIP:Cost"), u16(std::to_string(cost)));
		}
		return t;
	}
	if (science != SCIENCE_INVALID && !scienceLocked)
	{
		// RW 0x8083CC: any command with one science (or PURCHASE_SCIENCE's pick)
		if (TheScienceStore)
		{
			t.cost = formatOne(fetchOrMissing(text, "TOOLTIP:ScienceCost"), u16(std::to_string(TheScienceStore->getSciencePurchaseCost(science, mp)))); // RW 0x5FEC64
		}
		return t;
	}
	if (cmd == GUI_COMMAND_REVIVE)
	{
		// RW 0x8083EF .. 0x80863A: the hero record (+ 0xC0, the selection's player's list + 0x758) and its template
		const HeroRecord *r = player ? player->heroes().at(button.reviveIndex) : nullptr;
		const ThingTemplate *hero = r ? player->heroes().templateAt(logic, button.reviveIndex) : nullptr;
		if (!r || !hero)
		{
			return t;
		}
		const char *label = r->dead ? "CONTROLBAR:GenericReviveHero" : "CONTROLBAR:GenericRecruitHero";
		// the description: the template's ReviveText / RecruitText (+ 0x48 / + 0x50, RW 0x73C5FE / 0x73C621), else the generic tool tip
		const UnicodeString own = templateText(*hero, r->dead ? "ReviveText" : "RecruitText", text);
		if (!own.empty())
		{
			t.description = own;
		}
		else
		{
			t.description = fetchOrMissing(text, r->dead ? "CONTROLBAR:ToolTipGenericReviveHero" : "CONTROLBAR:ToolTipGenericRecruitHero");
		}
		// the name (RW 0x8084CA): a CREATE_A_HERO template (+ 0x11F bit 0x40) with the player's created hero (RW 0x80774E -> RW 0x61B2DF) formats the label
		// with that hero's name (lane ROTWK-C); else with the template's + 0x60 label (the Hotkey row, RW 0x73C644) fetched, or its DisplayName text (+ 0x30)
		const int cahBit = ObjectTemplateInfoBuilder::kindOfIndex("CREATE_A_HERO");
		const CreateAHeroHero *cah = cahBit >= 0 && MaskTest(ObjectTemplateInfoBuilder::build(*hero).kindOf, (unsigned)cahBit) ? logic.createAHeroes().heroOf(*player) : nullptr;
		const std::string nameLabel = templateLabel(*hero, "Hotkey");
		t.name = formatOne(fetchOrMissing(text, label), cah ? cah->name : nameLabel.empty() ? templateText(*hero, "DisplayName", text) : fetchOrMissing(text, nameLabel));
		// "\n" and the rank (RW 0x80858B: APT:RankLabel with the record's + 0x0C)
		t.description += u"\n";
		t.description += formatOne(fetchOrMissing(text, "APT:RankLabel"), u16(std::to_string(r->rank)));
		// the cost (RW 0x780AD3 with the selection as the producer) and the command points (+ 0x628)
		t.cost = formatOne(fetchOrMissing(text, "TOOLTIP:Cost"), u16(std::to_string(player->heroes().costAt(logic, *player, button.reviveIndex, selection))));
		const int cp = BuildAssistant::commandPoints(*hero);
		if (cp > 0)
		{
			t.commandPoints = formatOne(fetchOrMissing(text, "TOOLTIP:CommandPoints"), u16(std::to_string(cp)));
		}
		return t;
	}
	if (cmd == GUI_COMMAND_CASTLE_UNPACK)
	{
		// RW 0x80863C: the selection's castle module (RW 0x68BDA5), when it can unpack (RW 0x7983E3, no timer test): its unpack cost (RW 0x7995FB)
		const CastleBehavior *castle = selection ? dynamic_cast<const CastleBehavior *>(selection->findModule("CastleBehavior")) : nullptr;
		if (castle && castle->canUnpack(false))
		{
			t.cost = formatOne(fetchOrMissing(text, "TOOLTIP:Cost"), u16(std::to_string(castle->unpackCost(player))));
		}
		return t;
	}
	if (cmd == GUI_COMMAND_SPECIAL_POWER)
	{
		// RotWK only (RW 0x808675 .. 0x8086A3): the power's final override's UnitCost (+ 0x80) when it is not 0, as "TOOLTIP:UnitCost"
		const SpecialPowerTemplate *sp = TheSpecialPowerStore && !b->m_specialPowerName.empty()
		                                     ? TheSpecialPowerStore->findSpecialPowerTemplate(b->m_specialPowerName)
		                                     : nullptr;
		if (sp && sp->getFinalOverride()->m_unitCost != 0)
		{
			t.cost = formatOne(fetchOrMissing(text, "TOOLTIP:UnitCost"), u16(std::to_string(sp->getFinalOverride()->m_unitCost)));
		}
	}
	return t;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// InGameCommandButtonHelp::Impl
// ---------------------------------------------------------------------------------------------------------------------------------
CommandButtonHelp::CommandButtonHelp(const Text &t, const GameTextSource *text, ImageSizeFn imageSize, void *imageCtx)
	: m_title(t.cost), m_description(t.commandPoints), m_cost(t.description), m_unported(t.unported)
{
	// RW 0x97434E: the '&' markers go, the first alphanumeric after one is the key (towupper)
	const UnicodeString &n = t.name;
	size_t amp = n.find(u'&');
	if (amp == UnicodeString::npos)
	{
		m_name = n;
	}
	else
	{
		UnicodeString out = n.substr(0, amp);
		for (size_t i = amp; i < n.size(); ++i)
		{
			char16_t c = n[i];
			if (c == u'&')
			{
				if (++i >= n.size())
				{
					break;
				}
				c = n[i];
				if (!m_key && std::iswalnum((wint_t)c))
				{
					m_key = (char16_t)std::towupper((wint_t)c);
				}
			}
			out += c;
		}
		m_name = out;
	}
	// the icons: Resource_Icon and ResourceBar_<suffix> (a mapped image with a zero size counts as none)
	int w = 0, h = 0;
	if (imageSize && imageSize(imageCtx, "Resource_Icon", w, h) && w > 0 && h > 0)
	{
		m_icon30 = "Resource_Icon";
		m_icon30W = w;
		m_icon30H = h;
	}
	if (!t.resourceSuffix.empty() && imageSize && imageSize(imageCtx, "ResourceBar_" + t.resourceSuffix, w, h) && w > 0 && h > 0)
	{
		m_icon34 = "ResourceBar_" + t.resourceSuffix;
		m_icon34W = w;
		m_icon34H = h;
	}
	if (m_key)
	{
		// the shortcut line: TOOLTIP:Shortcut formatted with the key (RW 0x97461E)
		m_shortcut = formatOne(fetchOrMissing(text, "TOOLTIP:Shortcut"), UnicodeString(1, m_key));
	}
}

CommandButtonHelp CommandButtonHelp::simple(const UnicodeString &title, const UnicodeString &text, const std::vector<std::string> &unported)
{
	CommandButtonHelp h;
	h.m_simple = true;
	h.m_unported = unported;
	for (char16_t c : title) // RW 0x94002B (BFME2 Rva00539619): every '&' goes, no key is kept
	{
		if (c != u'&')
		{
			h.m_name += c;
		}
	}
	h.m_cost = text;
	return h;
}

std::vector<UnicodeString> CommandButtonHelp::wrap(const UnicodeString &text, const GameFont &font, int width, FontMetricsSource &metrics, int *widest)
{
	std::vector<UnicodeString> lines;
	int maxW = 0;
	size_t start = 0;
	while (start <= text.size())
	{
		size_t nl = text.find(u'\n', start);
		const UnicodeString para = text.substr(start, nl == UnicodeString::npos ? UnicodeString::npos : nl - start);
		// words by spaces; a line takes words while it fits (a single long word stays whole)
		UnicodeString line;
		size_t p = 0;
		while (p < para.size())
		{
			size_t sp = para.find(u' ', p);
			const UnicodeString word = para.substr(p, sp == UnicodeString::npos ? UnicodeString::npos : sp - p);
			const UnicodeString candidate = line.empty() ? word : line + u" " + word;
			if (width > 0 && !line.empty() && metrics.textWidth(font, candidate) > width)
			{
				lines.push_back(line);
				line = word;
			}
			else
			{
				line = candidate;
			}
			if (sp == UnicodeString::npos)
			{
				break;
			}
			p = sp + 1;
		}
		// the retail texts pad the line breaks with spaces ("Level 4 \n Targeted"): the line keeps them; trailing and leading blanks do not move the centre
		while (!line.empty() && line.back() == u' ')
		{
			line.pop_back();
		}
		while (!line.empty() && line.front() == u' ')
		{
			line.erase(line.begin());
		}
		lines.push_back(line);
		if (nl == UnicodeString::npos)
		{
			break;
		}
		start = nl + 1;
	}
	for (const UnicodeString &l : lines)
	{
		const int w = metrics.textWidth(font, l);
		maxW = w > maxW ? w : maxW;
	}
	if (widest)
	{
		*widest = maxW;
	}
	return lines;
}

CommandButtonHelp::Measured CommandButtonHelp::measure(const UnicodeString &text, const HelpBoxFont &font, int wrapWidth, FontMetricsSource &metrics) const
{
	Measured m;
	m.font = font;
	if (text.empty())
	{
		return m; // an empty display string measures 0 x 0
	}
	const GameFont g = gameFont(font);
	m.lines = wrap(text, g, wrapWidth, metrics, &m.w);
	m.h = (int)m.lines.size() * metrics.fontHeight(g);
	return m;
}

int CommandButtonHelp::setWidthAndComputeHeight(int width, const HelpBoxSettings &settings, float fontScale, float iconScaleX, float iconScaleY,
                                                FontMetricsSource &metrics)
{
	m_width = width;
	m_scaleX = iconScaleX;
	m_scaleY = iconScaleY;
	auto scaled = [fontScale](HelpBoxFont f) {
		f.pointSize = (int)((float)f.pointSize * fontScale); // RW 0x97428C: FontLibrary::getFont(name, size * min(scale), bold)
		return f;
	};
	if (m_simple)
	{
		// RW 0x940159: both strings wrap at the width; the height is the sum of the non-empty ones (no padding line)
		m_nameS = measure(m_name, scaled(settings.name), m_width, metrics);
		m_costS = measure(m_cost, scaled(settings.description), m_width, metrics);
		return m_nameS.h + m_costS.h;
	}
	UnicodeString title = m_title, description = m_description;
	if (!hasText(title) && hasText(description))
	{
		title.swap(description);
	}
	const float iconH = std::max(m_icon30H * iconScaleY, m_icon34H * iconScaleY); // ComputeIconSizes: the larger of the two
	m_nameS = measure(m_name, scaled(settings.name), m_width, metrics);
	float total = (float)m_nameS.h;
	m_titleS = measure(title, scaled(settings.cost), 0, metrics);
	float titleH = (float)m_titleS.h;
	if (hasText(title) && iconH > titleH)
	{
		titleH = iconH;
	}
	m_shortcutS = measure(m_shortcut, scaled(settings.shortcut), 0, metrics);
	const float shortcutH = (float)m_shortcutS.h;
	float lineH;
	if (m_key)
	{
		lineH = hasText(title) ? (titleH > shortcutH ? titleH : shortcutH) : shortcutH;
	}
	else
	{
		lineH = titleH;
	}
	total += lineH;
	m_descriptionS = measure(description, scaled(settings.cost), 0, metrics);
	if (hasText(description))
	{
		float dh = (float)m_descriptionS.h;
		if (iconH > dh)
		{
			dh = iconH;
		}
		total += dh;
	}
	m_costS = measure(hasText(m_cost) ? m_cost : UnicodeString(u" "), scaled(settings.description), m_width, metrics);
	total += (float)m_costS.h;
	return (int)total;
}

void CommandButtonHelp::emit(const Measured &m, float x, float y, std::vector<HelpDrawOp> &out) const
{
	// the word wrap is centred (RW 0x974D9C: slot 9 (true)): each line in the string's box
	const float lineH = m.lines.empty() ? 0.0f : (float)m.h / (float)m.lines.size();
	for (size_t i = 0; i < m.lines.size(); ++i)
	{
		HelpDrawOp op;
		op.kind = HelpDrawOp::TEXT;
		op.text = loadScreenU16ToUtf8(m.lines[i]);
		op.font = m.font;
		op.x = x;
		op.y = y + lineH * (float)i;
		op.w = (float)m.w;
		op.h = lineH;
		out.push_back(op);
	}
}

void CommandButtonHelp::render(float px, float py, float sw, float sh, std::vector<HelpDrawOp> &out) const
{
	(void)sh;
	if (m_simple)
	{
		// RW 0x93FF0A: each non-empty string centred in the width, the second under the first
		float y = py;
		if (!m_nameS.lines.empty())
		{
			emit(m_nameS, rounded(px + (sw - (float)m_nameS.w) * 0.5f), rounded(y), out);
			y += (float)m_nameS.h;
		}
		if (!m_costS.lines.empty())
		{
			emit(m_costS, rounded(px + (sw - (float)m_costS.w) * 0.5f), rounded(y), out);
		}
		return;
	}
	const float icon30W = m_icon30W * m_scaleX, icon30H = m_icon30H * m_scaleY, icon34W = m_icon34W * m_scaleX, icon34H = m_icon34H * m_scaleY;
	const float maxW = std::max(icon30W, icon34W), maxH = std::max(icon30H, icon34H);
	UnicodeString title = m_title, description = m_description;
	std::string titleIcon = m_icon30, descriptionIcon = m_icon34;
	float titleIconW = icon30W, titleIconH = icon30H, descriptionIconW = icon34W, descriptionIconH = icon34H;
	const Measured *titleS = &m_titleS, *descriptionS = &m_descriptionS;
	if (!hasText(title) && hasText(description))
	{
		title.swap(description);
		titleIcon = descriptionIcon;
		titleIconW = descriptionIconW;
		titleIconH = descriptionIconH;
		// SetWidthAndComputeHeight measured the swapped strings: m_titleS holds the second line's text
	}
	auto drawIcon = [&](const std::string &image, float x, float y, float iw, float ih) {
		if (image.empty() || iw <= 0.0f || ih <= 0.0f)
		{
			return;
		}
		HelpDrawOp op; // RW 0x97422A DrawIcon: centred in the larger icon's box
		op.kind = HelpDrawOp::IMAGE;
		op.image = image;
		op.x = x + (maxW - iw) * 0.5f;
		op.y = y + (maxH - ih) * 0.5f;
		op.w = iw;
		op.h = ih;
		out.push_back(op);
	};
	float y = py;
	emit(m_nameS, rounded(px + (sw - (float)m_nameS.w) * 0.5f), rounded(y), out);
	y += (float)m_nameS.h;
	float lineH = 0.0f;
	bool centered = false;
	if (m_key)
	{
		if (hasText(title))
		{
			const float titleH = (float)titleS->h, shortcutW = (float)m_shortcutS.w, shortcutH = (float)m_shortcutS.h;
			lineH = titleH > shortcutH ? titleH : shortcutH;
			if (maxH > lineH)
			{
				lineH = maxH;
			}
			drawIcon(titleIcon, px, y + (lineH - maxH) * 0.5f, titleIconW, titleIconH);
			emit(*titleS, (float)(long)std::floor(px + maxW + 0.5f), (float)(long)std::floor(y + (lineH - titleH + 1.0f) * 0.5f), out);
			emit(m_shortcutS, rounded(px + sw - shortcutW), (float)(long)std::floor(y + (lineH - shortcutH + 1.0f) * 0.5f), out);
		}
		else
		{
			lineH = (float)m_shortcutS.h;
			emit(m_shortcutS, rounded(px + (sw - (float)m_shortcutS.w) * 0.5f), rounded(y), out);
		}
	}
	else
	{
		centered = true;
		if (hasText(title))
		{
			const float titleH = (float)titleS->h;
			const float titleW = (float)titleS->w + maxW;
			lineH = maxH > titleH ? maxH : titleH;
			const float x = px + (sw - titleW) * 0.5f;
			drawIcon(titleIcon, x, y + (lineH - maxH) * 0.5f, titleIconW, titleIconH);
			emit(*titleS, (float)(long)std::floor(x + maxW + 0.5f), (float)(long)std::floor(y + (lineH - titleH + 1.0f) * 0.5f), out);
		}
	}
	y += lineH;
	if (hasText(description))
	{
		const float dh = (float)descriptionS->h;
		lineH = maxH > dh ? maxH : dh;
		float x = px;
		if (centered)
		{
			x += (sw - ((float)descriptionS->w + maxW)) * 0.5f;
		}
		drawIcon(descriptionIcon, x, y + (lineH - maxH) * 0.5f, descriptionIconW, descriptionIconH);
		emit(*descriptionS, (float)(long)std::floor(x + maxW + 0.5f), (float)(long)std::floor(y + (lineH - dh + 1.0f) * 0.5f), out);
		y += lineH;
	}
	emit(m_costS, rounded(px + (sw - (float)m_costS.w) * 0.5f), rounded(y), out);
}

std::vector<std::string> CommandButtonHelp::acceptanceStops()
{
	return {
		"[S-2702] command button help (HUD-6, HOVER-1): ported RW 0x807A81's name, description, build cost / free, command points, build-disabled, the "
		"science pick and cost, SPELL_BOOK's science-disabled test, the upgrade's cost / already-upgraded / conflicting / lacks-prerequisite texts, the "
		"queue-full and not-enough-money warnings, the revive / recruit name (the Create-a-Hero name too), text, rank and cost, the repair costs, the castle "
		"unpack cost and the unit cost of SPECIAL_POWER, "
		"the Prerequisites text (RW 0x8080B6 -> RW 0x8F9BBF) and InGameCommandButtonHelp (RW 0x974EEB / 0x9744E4 / 0x974795); INFERENCE: the word wrap breaks at spaces and '\\n' and centres each line "
		"(Render2DSentenceClass not read), a line's leading / trailing blanks are dropped"
	};
}
