// OpenBFME. GPL-3.0.
// See CommandButtonHelp.h (lane HUD-6). Client-only: the help reads the logic, it never changes it.

#include "GameClient/CommandButtonHelp.h"

#include "Common/ArchiveFileSystem.h"
#include "Common/AsciiString.h"
#include "Common/BuildAssistant.h"
#include "Common/INI.h"
#include "Common/INIException.h"
#include "Common/INI/INIBlockStubs.h"
#include "Common/Player.h"
#include "Common/Science.h"
#include "Common/SpecialPower.h"
#include "Common/Thing/ThingTemplate.h"
#include "Common/Upgrade.h"
#include "GameClient/GUI/GameTextSource.h"
#include "GameClient/GUI/GameWindowManager.h"
#include "GameClient/GUI/LoadScreenInfo.h"
#include "GameClient/GlobalLanguage.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/CastleModules.h"
#include "GameLogic/Module/ProductionUpdate.h"
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
	GlobalLanguage language; // INTEG-4: UI-4's GlobalLanguage reads the Language block (its full RW 0xBF4C20 table)
	unsigned seen = 0;
	bool inGameUi = false;
};

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
		if (firstTokenIsEnd(line) && indentOf(line) <= headerIndent)
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

// UnicodeString::format with one %d or %s (the retail labels carry one)
UnicodeString formatOne(const UnicodeString &fmt, const UnicodeString &value)
{
	UnicodeString out;
	bool done = false;
	for (size_t i = 0; i < fmt.size(); ++i)
	{
		if (fmt[i] == u'%' && i + 1 < fmt.size())
		{
			const char16_t c = fmt[i + 1];
			if (c == u'%')
			{
				out += u'%';
				++i;
				continue;
			}
			if (!done && (c == u'd' || c == u's' || c == u'S' || c == u'u' || c == u'i'))
			{
				out += value;
				done = true;
				++i;
				continue;
			}
		}
		out += fmt[i];
	}
	return out;
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

CommandButtonHelp::Text CommandButtonHelp::compose(const ControlBarButton &button, const GameTextSource *text, GameLogic &logic, Player *player, Player *localPlayer,
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
	// behavior modules answer at vslot 0x84 (RW 0x68C3E6), vslot 0x30 / 0x50
	if ((cmd == GUI_COMMAND_START_SELF_REPAIR || cmd == GUI_COMMAND_START_NEIGHBORHOOD_REPAIR) && selection)
	{
		t.unported.push_back("the repair cost of RW 0x807C14 (the behavior interface RW 0x68C3E6 answers is not ported, S-4612)");
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
		// RW 0x8080B6: the prerequisites into CONTROLBAR:Requirements (retail templates have none, S-207)
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
				t.description = b->m_purchasedLabel.empty() ? fetchOrMissing(text, "TOOLTIP:AlreadyUpgradedDefault") : fetchOrMissing(text, b->m_purchasedLabel);
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
		// the name (RW 0x8084CA): a CREATE_A_HERO template's created hero (RW 0x80774E) is not ported (S-4612); else the label formatted with the template's
		// + 0x60 label (the Hotkey row, RW 0x73C644) fetched, or its DisplayName text (+ 0x30)
		const int cahBit = ObjectTemplateInfoBuilder::kindOfIndex("CREATE_A_HERO");
		if (cahBit >= 0 && MaskTest(ObjectTemplateInfoBuilder::build(*hero).kindOf, (unsigned)cahBit))
		{
			t.unported.push_back("the Create-a-Hero name of RW 0x80774E (S-4612)");
		}
		const std::string nameLabel = templateLabel(*hero, "Hotkey");
		t.name = formatOne(fetchOrMissing(text, label), nameLabel.empty() ? templateText(*hero, "DisplayName", text) : fetchOrMissing(text, nameLabel));
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
		"queue-full and not-enough-money warnings, the revive / recruit name, text, rank and cost, the castle unpack cost and the unit cost of SPECIAL_POWER, "
		"and InGameCommandButtonHelp (RW 0x974EEB / 0x9744E4 / 0x974795); not ported: the repair cost (S-4612), the Create-a-Hero revive name, the "
		"Prerequisites text (no retail template has one, S-207); INFERENCE: the word wrap breaks at spaces and '\\n' and centres each line "
		"(Render2DSentenceClass not read), a line's leading / trailing blanks are dropped"
	};
}
