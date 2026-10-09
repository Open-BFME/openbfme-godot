// OpenBFME. GPL-3.0.
// See GameClient/ControlBarCommands.h for the sources.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameClient/ControlBarCommands.h"

#include "Common/AsciiString.h"
#include "Common/ArchiveFileSystem.h"
#include "Common/INIException.h"
#include "Common/ModelState.h"
#include "Common/Thing/ThingFactory.h"

#include <algorithm>
#include <cstddef>
#include <cstring>

// the binary's name registries (GameLogic/BitFlagNames.cpp); declared here instead of including GameLogic/BitFlags.h, whose ModelConditionFlags typedef
// cannot sit next to Common/ModelState.h
extern const char *const TheKindOfNames[];
extern const char *const TheWeaponConditionNames[];
void ParseBitFlags(INI *ini, std::uint32_t *words, size_t wordCount, const char *const *names);

thread_local CommandStore *TheCommandStore = nullptr; // SMOOTH-1: per thread (the world context of each thread, RetailObjectWorld::ContextScope)

namespace
{
// RW 0xDA4D10, 61 names (value = index)
const char *const kCommandNames[] = { "NONE", "FOUNDATION_CONSTRUCT", "FOUNDATION_CONSTRUCT_CANCEL", "UNIT_BUILD", "SECONDARY_UNIT_BUILD", "CANCEL_UNIT_BUILD", "PLAYER_UPGRADE", "OBJECT_UPGRADE", "CASTLE_UPGRADE", "CANCEL_UPGRADE", "ATTACK_MOVE", "GUARD", "GUARD_WITHOUT_PURSUIT", "GUARD_FLYING_UNITS_ONLY", "STOP", "WAYPOINTS", "EXIT_CONTAINER", "EVACUATE", "EVACUATE_CONTESTED", "EXECUTE_UNUSED_01", "BEACON_DELETE", "SET_RALLY_POINT", "SELL", "FIRE_WEAPON", "SPECIAL_POWER", "PURCHASE_SCIENCE", "COMBATDROP", "SWITCH_WEAPON", "AUTOCAST_WEAPON", "HIJACK_VEHICLE", "CONVERT_TO_CARBOMB", "PLACE_BEACON", "SPECIAL_POWER_FROM_COMMAND_CENTER", "CASTLE_UNPACK", "CASTLE_PACK", "TOGGLE_WEAPON", "HORDE_TOGGLE_FORMATION", "SPECIAL_POWER_TOGGLE", "SPELL_BOOK", "ONE_RING", "CREW_EVACUATE", "BLOODTHIRSTY", "CLOSE_GATE", "OPEN_GATE", "TOGGLE_GATE", "TOGGLE_WEAPONSET", "REVIVE", "MONSTERDOCK", "TOGGLE_NO_AUTO_ACQUIRE", "WAKE_AUTO_PICKUP", "CASTLE_UNPACK_EXPLICIT_OBJECT", "START_SELF_REPAIR", "HORDE_SET_FORMATION", "DOZER_CONSTRUCT", "DOZER_CONSTRUCT_CANCEL", "PUSH_VISIBLE_COMMAND_RANGE", "POP_VISIBLE_COMMAND_RANGE", "TOGGLE_STANCE", "SET_STANCE", "START_NEIGHBORHOOD_REPAIR", "CANCEL_NEIGHBORHOOD", nullptr };
// RW 0xDA4C88, 32 names (bit = index)
const char *const kOptionNames[] = { "NEED_TARGET_ENEMY_OBJECT", "NEED_TARGET_NEUTRAL_OBJECT", "NEED_TARGET_ALLY_OBJECT", "NO_PLAY_UNIT_SPECIFIC_SOUND_FOR_AUTO_ABILITY", "ALLOW_SHRUBBERY_TARGET", "NEED_TARGET_POS", "NEED_UPGRADE", "NEED_SPECIAL_POWER_SCIENCE", "OK_FOR_MULTI_SELECT", "CONTEXTMODE_COMMAND", "CHECK_LIKE", "NEEDS_CASTLE_KINDOF", "ATTACK_OBJECTS_POSITION", "OPTION_ONE", "OPTION_TWO", "OPTION_THREE", "NOT_QUEUEABLE", "SINGLE_USE_COMMAND", "---DO-NOT-USE---", "SCRIPT_ONLY", "OK_FOR_MULTI_EXECUTE", "ALLOW_ROCK_TARGET", "HIDE_WHILE_DISABLED", "TOGGLE_IMAGE_ON_WEAPON", "TOGGLE_IMAGE_ON_WEAPONSET", "TOGGLE_IMAGE_ON_FORMATION", "MOUNTED_ONLY", "UNMOUNTED_ONLY", "NONPRESSABLE", "AUTO_ABILITY_TRIGGERED", "ON_GROUND_ONLY", "CANCELABLE", nullptr };
// RW 0xC2BA90 (ButtonBorderType)
const LookupListRec kBorderTypes[] = { { "NONE", 0 }, { "BUILD", 1 }, { "UPGRADE", 2 }, { "ACTION", 3 }, { "SYSTEM", 4 }, { "ALTERED", 5 }, { nullptr, 0 } };
// RW 0xC16928 (WeaponSlot*)
const LookupListRec kWeaponSlots[] = { { "PRIMARY", 0 }, { "SECONDARY", 1 }, { "TERTIARY", 2 }, { "QUATERNARY", 3 }, { "QUINARY", 4 }, { nullptr, 0 } };
// RW 0xC53660 (Stances)
const char *const kStanceNames[] = { "Uninitialized", "Battle", "Aggressive", "HoldGround", "Porcupine", "HoldGroundMoving", nullptr };

CommandButton *asButton(void *instance) { return static_cast<CommandButton *>(instance); }

// RW 0x75CB37 CommandButton::parseCommand: stricmp over the name array; INIException(3, "Command '%s' not found") (RW 0xC2C878)
void parseCommand(INI *ini, void *, void *store, const void *)
{
	const char *token = ini->getNextToken();
	for (int i = 0; kCommandNames[i]; ++i)
	{
		if (AsciiStringUtil::compareNoCase(kCommandNames[i], token) == 0)
		{
			*static_cast<int *>(store) = i;
			return;
		}
	}
	throw INIException(3, "Command '%s' not found", token);
}

// RW 0x42E840 parseBitString32 with the Options list. The binary throws code 2 for the mixing error (the generic INI::parseBitString32 of the
// repo says 3; INI-1's, not touched here)
void parseOptions(INI *ini, void *, void *store, const void *)
{
	std::uint32_t *bits = static_cast<std::uint32_t *>(store);
	bool foundNormal = false, foundAddOrSub = false;
	for (const char *token = ini->getNextTokenOrNull(); token; token = ini->getNextTokenOrNull())
	{
		if (AsciiStringUtil::compareNoCase(token, "NONE") == 0)
		{
			if (foundNormal || foundAddOrSub)
			{
				throw INIException(2, "you may not mix normal and +- ops in bitstring lists");
			}
			*bits = 0;
			break;
		}
		if (token[0] == '+' || token[0] == '-')
		{
			if (foundNormal)
			{
				throw INIException(2, "you may not mix normal and +- ops in bitstring lists");
			}
			const int bit = INI::scanIndexList(token + 1, kOptionNames);
			if (token[0] == '+')
			{
				*bits |= (1u << bit);
			}
			else
			{
				*bits &= ~(1u << bit);
			}
			foundAddOrSub = true;
		}
		else
		{
			if (foundAddOrSub)
			{
				throw INIException(2, "you may not mix normal and +- ops in bitstring lists");
			}
			if (!foundNormal)
			{
				*bits = 0;
			}
			*bits |= (1u << INI::scanIndexList(token, kOptionNames));
			foundNormal = true;
		}
	}
}

// RW 0x73AD1F parseThingTemplate: "None" -> NULL, a missing ThingFactory is the plain int throw 0xDEAD0001, an unknown name is NULL (no error)
void parseObject(INI *ini, void *instance, void *, const void *)
{
	CommandButton *button = asButton(instance);
	const char *token = ini->getNextToken();
	if (AsciiStringUtil::compareNoCase(token, "None") == 0)
	{
		button->m_objectName.clear();
		button->m_thingTemplate = nullptr;
		return;
	}
	if (!TheCommandStore || !TheCommandStore->thingFactory())
	{
		throw INIPlainIntError("TheThingFactory==NULL"); // RW 0xDEAD0001
	}
	button->m_objectName = token;
	button->m_thingTemplate = TheCommandStore->thingFactory()->findTemplate(token);
}

void parseUpgradeName(INI *ini, void *, void *store, const void *)
{
	*static_cast<std::string *>(store) = ini->getNextToken(); // RW 0x73AF89 / 0x73B22F resolve it (UpgradeCenter / SpecialPowerStore, S-200)
}

// RW 0x73B304: appends (never clears); unknown names are skipped in retail (S-200: not resolvable here, kept)
void parseNeededUpgrade(INI *ini, void *, void *store, const void *)
{
	std::vector<std::string> *v = static_cast<std::vector<std::string> *>(store);
	for (const char *t = ini->getNextTokenOrNull(); t; t = ini->getNextTokenOrNull())
	{
		v->push_back(t);
	}
}

// RW 0x73B4A0: clears, then names until "None" (which clears again and stops)
void parseScienceVector(INI *ini, void *, void *store, const void *)
{
	std::vector<std::string> *v = static_cast<std::vector<std::string> *>(store);
	v->clear();
	for (const char *t = ini->getNextTokenOrNull(); t; t = ini->getNextTokenOrNull())
	{
		if (AsciiStringUtil::compareNoCase(t, "None") == 0)
		{
			v->clear();
			break;
		}
		v->push_back(t);
	}
}

void parseSoundRefList(INI *ini, void *, void *store, const void *) // RW 0x73BB7D: appends one reference per token (S-200: not validated)
{
	std::vector<std::string> *v = static_cast<std::vector<std::string> *>(store);
	for (const char *t = ini->getNextTokenOrNull(); t; t = ini->getNextTokenOrNull())
	{
		v->push_back(t);
	}
}

// RW 0x75D4D0: appends; scanIndexList over RW 0xC53660; 0 <= index < 6 is stored
void parseStances(INI *ini, void *, void *store, const void *)
{
	std::vector<int> *v = static_cast<std::vector<int> *>(store);
	for (const char *t = ini->getNextTokenOrNull(); t; t = ini->getNextTokenOrNull())
	{
		const int idx = INI::scanIndexList(t, kStanceNames);
		if (idx >= 0 && idx < 6)
		{
			v->push_back(idx);
		}
	}
}

// RW 0x66F603: macro aware tokens; "None" is accepted; names are kept (the upgrade index mask needs the UpgradeCenter, S-200)
void parseUpgradeNames(INI *ini, void *, void *store, const void *)
{
	std::vector<std::string> *v = static_cast<std::vector<std::string> *>(store);
	v->clear();
	for (const char *t = ini->getNextTokenOrNull(); t; t = ini->getNextTokenOrNull())
	{
		const std::string text = ini->preprocessMacro(t);
		size_t pos = 0;
		while (pos < text.size())
		{
			const size_t b = text.find_first_not_of(" \n\r\t", pos);
			if (b == std::string::npos)
			{
				break;
			}
			size_t e = text.find_first_of(" \n\r\t", b);
			if (e == std::string::npos)
			{
				e = text.size();
			}
			const std::string piece = text.substr(b, e - b);
			if (AsciiStringUtil::compareNoCase(piece, "None") != 0)
			{
				v->push_back(piece);
			}
			pos = e;
		}
	}
}

void parseKindOf(INI *ini, void *, void *store, const void *) // RW 0x6564E7 -> 0x65621C
{
	KindOfMaskType *mask = static_cast<KindOfMaskType *>(store);
	ParseBitFlags(ini, mask->data(), mask->size(), TheKindOfNames);
}

void parseWeaponConditions(INI *ini, void *, void *store, const void *) // RW 0x6C9951 -> 0x6C949F
{
	std::array<std::uint32_t, 4> *mask = static_cast<std::array<std::uint32_t, 4> *>(store);
	ParseBitFlags(ini, mask->data(), mask->size(), TheWeaponConditionNames);
}

// RW 0x4B8C21 -> 0x4B8B37: the 19 word flag array (the same words as Common/ModelState.h's BitFlags<591>)
void parseModelConditions(INI *ini, void *, void *store, const void *)
{
	std::array<std::uint32_t, 19> *words = static_cast<std::array<std::uint32_t, 19> *>(store);
	ModelConditionFlags flags;
	static_assert(sizeof(ModelConditionFlags) == sizeof(*words), "BitFlags<591> is 19 words");
	std::memcpy(flags.words(), words->data(), sizeof(*words));
	ModelCondition::parseFromLine(ini, flags);
	std::memcpy(words->data(), flags.words(), sizeof(*words));
}

#define CB_OFF(member) (int)offsetof(CommandButton, member)
// RW 0xC2BAC8, 55 rows in the binary's order
const FieldParse kCommandButtonFieldParse[] = {
	{ "Command", parseCommand, nullptr, CB_OFF(m_command) },
	{ "Options", parseOptions, nullptr, CB_OFF(m_options) },
	{ "Object", parseObject, nullptr, 0 },
	{ "Upgrade", parseUpgradeName, nullptr, CB_OFF(m_upgradeName) },
	{ "NeededUpgrade", parseNeededUpgrade, nullptr, CB_OFF(m_neededUpgrade) },
	{ "NeededUpgradeAny", INI::parseBool, nullptr, CB_OFF(m_neededUpgradeAny) },
	{ "BuildUpgrades", INI::parseAsciiStringVector, nullptr, CB_OFF(m_buildUpgrades) },
	{ "WeaponSlot", INI::parseLookupList, kWeaponSlots, CB_OFF(m_weaponSlot) },
	{ "WeaponSlotToggle1", INI::parseLookupList, kWeaponSlots, CB_OFF(m_weaponSlotToggle1) },
	{ "WeaponSlotToggle2", INI::parseLookupList, kWeaponSlots, CB_OFF(m_weaponSlotToggle2) },
	{ "WeaponSlotToggle3", INI::parseLookupList, kWeaponSlots, CB_OFF(m_weaponSlotToggle3) },
	{ "FlagsUsedForToggle", parseWeaponConditions, nullptr, CB_OFF(m_flagsUsedForToggle) },
	{ "MaxShotsToFire", INI::parseInt, nullptr, CB_OFF(m_maxShotsToFire) },
	{ "Science", parseScienceVector, nullptr, CB_OFF(m_science) },
	{ "SpecialPower", parseUpgradeName, nullptr, CB_OFF(m_specialPowerName) },
	{ "ToggleButtonName", INI::parseAsciiString, nullptr, CB_OFF(m_toggleButtonName) },
	{ "TextLabel", INI::parseAsciiStringVector, nullptr, CB_OFF(m_textLabel) },
	{ "DescriptLabel", INI::parseAsciiStringVector, nullptr, CB_OFF(m_descriptLabel) },
	{ "PurchasedLabel", INI::parseAsciiString, nullptr, CB_OFF(m_purchasedLabel) },
	{ "ConflictingLabel", INI::parseAsciiString, nullptr, CB_OFF(m_conflictingLabel) },
	{ "LacksPrerequisiteLabel", INI::parseAsciiString, nullptr, CB_OFF(m_lacksPrerequisiteLabel) },
	{ "ButtonImage", INI::parseAsciiStringVector, nullptr, CB_OFF(m_buttonImageName) },
	{ "CursorName", INI::parseAsciiString, nullptr, CB_OFF(m_cursorName) },
	{ "InvalidCursorName", INI::parseAsciiString, nullptr, CB_OFF(m_invalidCursorName) },
	{ "ButtonBorderType", INI::parseLookupList, kBorderTypes, CB_OFF(m_commandButtonBorder) },
	{ "RadiusCursorType", INI::parseAsciiString, nullptr, CB_OFF(m_radiusCursor) },
	{ "UnitSpecificSound", parseSoundRefList, nullptr, CB_OFF(m_unitSpecificSound) },
	{ "SetAutoAbilityUnitSound", parseSoundRefList, nullptr, CB_OFF(m_setAutoAbilityUnitSound) },
	{ "UnsetAutoAbilityUnitSound", parseSoundRefList, nullptr, CB_OFF(m_unsetAutoAbilityUnitSound) },
	{ "DoubleClick", INI::parseBool, nullptr, CB_OFF(m_doubleClick) },
	{ "Radial", INI::parseBool, nullptr, CB_OFF(m_radial) },
	{ "ShowProductionCount", INI::parseBool, nullptr, CB_OFF(m_showProductionCount) },
	{ "InPalantir", INI::parseBool, nullptr, CB_OFF(m_inPalantir) },
	{ "IsClickable", INI::parseBool, nullptr, CB_OFF(m_isClickable) },
	{ "ShowButton", INI::parseBool, nullptr, CB_OFF(m_showButton) },
	{ "RequireLevel", INI::parseInt, nullptr, CB_OFF(m_requireLevel) },
	{ "RequiresValidContainer", INI::parseBool, nullptr, CB_OFF(m_requiresValidContainer) },
	{ "AutoAbility", INI::parseBool, nullptr, CB_OFF(m_autoAbility) },
	{ "AffectsKindOf", parseKindOf, nullptr, CB_OFF(m_affectsKindOf) },
	{ "TriggerWhenReady", INI::parseBool, nullptr, CB_OFF(m_triggerWhenReady) },
	{ "PresetRange", INI::parseReal, nullptr, CB_OFF(m_presetRange) },
	{ "AutoDelay", INI::parseReal, nullptr, CB_OFF(m_autoDelay) },
	{ "NeedDamagedTarget", INI::parseBool, nullptr, CB_OFF(m_needDamagedTarget) },
	{ "AutoAbilityDisallowedOnModelCondition", parseModelConditions, nullptr, CB_OFF(m_autoAbilityDisallowedOnModelCondition) },
	{ "CommandTrigger", INI::parseAsciiStringVector, nullptr, CB_OFF(m_commandTrigger) },
	{ "EnableOnModelCondition", parseModelConditions, nullptr, CB_OFF(m_enableOnModelCondition) },
	{ "DisableOnModelCondition", parseModelConditions, nullptr, CB_OFF(m_disableOnModelCondition) },
	{ "CommandRangeStart", INI::parseInt, nullptr, CB_OFF(m_commandRangeStart) },
	{ "CommandRangeCount", INI::parseInt, nullptr, CB_OFF(m_commandRangeCount) },
	{ "Stances", parseStances, nullptr, CB_OFF(m_stances) },
	{ "CreateAHeroUIPrerequisiteButtonName", INI::parseAsciiString, nullptr, CB_OFF(m_createAHeroUIPrerequisiteButtonName) },
	{ "CreateAHeroUIMinimumLevel", INI::parseInt, nullptr, CB_OFF(m_createAHeroUIMinimumLevel) },
	{ "CreateAHeroUIIconImageName", INI::parseAsciiString, nullptr, CB_OFF(m_createAHeroUIIconImageName) },
	{ "CreateAHeroUIAllowableUpgrades", parseUpgradeNames, nullptr, CB_OFF(m_createAHeroUIAllowableUpgrades) },
	{ "CreateAHeroUICostIfSelected", INI::parseInt, nullptr, CB_OFF(m_createAHeroUICostIfSelected) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef CB_OFF

// RW 0x80C9E1 CommandSet::parseCommandButton; userData = the slot index
void parseSetButton(INI *ini, void *instance, void *, const void *userData)
{
	CommandSet *set = static_cast<CommandSet *>(instance);
	const int index = (int)(std::intptr_t)userData;
	const char *token = ini->getNextToken();
	const CommandButton *button = TheCommandStore ? TheCommandStore->findCommandButton(token) : nullptr;
	if (!button)
	{
		throw INIException(3, "Unknown command '%s' found in command set. File: %s Line: %d\n", token, ini->getFilename().c_str(), ini->currentSourceLine());
	}
	set->m_command[(size_t)index] = button;
}

// RW 0xC4F3D8: keys "1" .. "33" then InitialVisible (the keys are strings with static storage)
struct SetTable
{
	std::string keys[CommandSet::MAX_BUTTONS];
	FieldParse rows[CommandSet::MAX_BUTTONS + 2];
	SetTable()
	{
		for (int i = 0; i < CommandSet::MAX_BUTTONS; ++i)
		{
			keys[i] = std::to_string(i + 1);
			rows[i] = { keys[i].c_str(), parseSetButton, (const void *)(std::intptr_t)i, 0 };
		}
		rows[CommandSet::MAX_BUTTONS] = { "InitialVisible", INI::parseInt, nullptr, (int)offsetof(CommandSet, m_initialVisible) };
		rows[CommandSet::MAX_BUTTONS + 1] = { nullptr, nullptr, nullptr, 0 };
	}
};
const FieldParse *commandSetFieldParse()
{
	static const SetTable table;
	return table.rows;
}
} // namespace

const char *GUICommandName(int command)
{
	const int count = (int)(sizeof(kCommandNames) / sizeof(kCommandNames[0])) - 1;
	return command >= 0 && command < count ? kCommandNames[command] : "";
}

CommandButton::CommandButton() = default;

const CommandButton *CommandButton::getFinalOverride() const
{
	const CommandButton *b = this;
	while (b->m_nextOverride)
	{
		b = b->m_nextOverride;
	}
	return b;
}

const ThingTemplate *CommandButton::getThingTemplate() const
{
	return getFinalOverride()->m_thingTemplate;
}

CommandButton *CommandStore::findNonConstCommandButton(const std::string &name) const
{
	auto it = m_buttons.find(name);
	return it == m_buttons.end() ? nullptr : it->second.get();
}

const CommandButton *CommandStore::findCommandButton(const std::string &name) const
{
	const CommandButton *b = findNonConstCommandButton(name);
	return b ? b->getFinalOverride() : nullptr;
}

CommandSet *CommandStore::findNonConstCommandSet(const std::string &name) const
{
	auto it = m_sets.find(name);
	return it == m_sets.end() ? nullptr : it->second.get();
}

CommandButton *CommandStore::newCommandButton(const std::string &name, bool override)
{
	auto b = std::make_unique<CommandButton>();
	b->m_name = name;
	b->m_isOverride = override;
	CommandButton *raw = b.get();
	m_buttons[name] = std::move(b);
	return raw;
}

// RW 0x72054C: a copy of the base's final override, flagged override and chained behind it
CommandButton *CommandStore::newCommandButtonOverride(CommandButton *base)
{
	CommandButton *last = const_cast<CommandButton *>(base->getFinalOverride());
	auto copy = std::make_unique<CommandButton>(*last);
	copy->m_nextOverride = nullptr;
	copy->m_isOverride = true;
	CommandButton *raw = copy.get();
	m_buttonOverrides.push_back(std::move(copy));
	last->m_nextOverride = raw;
	return raw;
}

CommandSet *CommandStore::newCommandSet(const std::string &name, bool override)
{
	auto s = std::make_unique<CommandSet>();
	s->m_name = name;
	s->m_isOverride = override;
	CommandSet *raw = s.get();
	m_sets[name] = std::move(s);
	return raw;
}

CommandSet *CommandStore::newDynamicCommandSet(const std::string &name)
{
	auto s = std::make_unique<CommandSet>();
	s->m_name = name;
	s->m_dynamic = true;
	s->m_initialVisible = 0; // RW 0x80C8DB
	CommandSet *raw = s.get();
	auto it = m_sets.find(name);
	if (it != m_sets.end())
	{
		m_deadSets.push_back(std::move(it->second));
	}
	m_sets[name] = std::move(s);
	return raw;
}

// RW 0x71C958: the copy is NOT in the map
CommandSet *CommandStore::newCommandSetOverride(CommandSet *base)
{
	auto copy = std::make_unique<CommandSet>(*base);
	copy->m_nextOverride = nullptr;
	copy->m_isOverride = true;
	CommandSet *raw = copy.get();
	m_setOverrides.push_back(std::move(copy));
	base->m_nextOverride = raw;
	return raw;
}

// RW 0x5DA711
void CommandStore::parseCommandButtonDefinition(INI *ini)
{
	const std::string name = ini->getNextToken(); // a missing token: INIException(3, "Expected additional data after '%s'") from the token reader
	CommandButton *button = findNonConstCommandButton(name);
	if (!button)
	{
		button = newCommandButton(name, ini->getLoadType() == INI_LOAD_CREATE_OVERRIDES);
	}
	else if (ini->getLoadType() == INI_LOAD_RELOAD)
	{
		// RW 0x71D875: the old button is parked in the dead list, a fresh one takes the name
		m_deadButtons.push_back(std::move(m_buttons[name]));
		m_deadButtons.back()->m_marker = 1;
		button = newCommandButton(name, false);
		button->m_marker = 0;
	}
	else if (ini->getLoadType() == INI_LOAD_CREATE_OVERRIDES)
	{
		button = newCommandButtonOverride(button);
	}
	else
	{
		// RW 0x5DA7C5: load types 1, 3, 4: parsed into a temporary and discarded (errors still thrown)
		CommandButton scratch;
		ini->initFromINI(&scratch, kCommandButtonFieldParse);
		return;
	}
	ini->initFromINI(button, kCommandButtonFieldParse);
}

// RW 0x7205B9
void CommandStore::parseCommandSetDefinition(INI *ini)
{
	const std::string name = ini->getNextToken();
	CommandSet *set = findNonConstCommandSet(name);
	if (!set)
	{
		set = newCommandSet(name, ini->getLoadType() == INI_LOAD_CREATE_OVERRIDES);
	}
	else if (ini->getLoadType() == INI_LOAD_CREATE_OVERRIDES)
	{
		set = newCommandSetOverride(set);
	}
	else if (ini->getLoadType() == INI_LOAD_RELOAD)
	{
		m_deadSets.push_back(std::move(m_sets[name]));
		m_deadSets.back()->m_marker = 1;
		set = newCommandSet(name, false);
		set->m_marker = 0;
	}
	else
	{
		throw INIException(3, "Duplicate commandset %s found!", name.c_str()); // RW 0xC233E0
	}
	ini->initFromINI(set, commandSetFieldParse());
}

void CommandStore::parseCommandButtonDefinitionGlobal(INI *ini)
{
	if (!TheCommandStore)
	{
		throw INIPlainIntError("TheControlBar==NULL");
	}
	TheCommandStore->parseCommandButtonDefinition(ini);
}

void CommandStore::parseCommandSetDefinitionGlobal(INI *ini)
{
	if (!TheCommandStore)
	{
		throw INIPlainIntError("TheControlBar==NULL");
	}
	TheCommandStore->parseCommandSetDefinition(ini);
}

// RW 0x71CE3A / 0x71CDD3
void CommandStore::loadFromFiles(INIEnvironment &env, std::vector<std::string> *errors)
{
	static const char *const files[] = { "Data\\INI\\Default\\CommandButton.ini", "Data\\INI\\CommandButton.ini", "Data\\INI\\CommandSet.ini" };
	for (const char *file : files)
	{
		if (!env.fileSystem || !env.fileSystem->doesFileExist(file))
		{
			continue; // tolerated (RW 0x71CDD3)
		}
		INI ini(env);
		try
		{
			ini.load(file, INI_LOAD_OVERWRITE);
		}
		catch (const std::exception &e)
		{
			if (errors)
			{
				errors->push_back(std::string(file) + ": " + e.what());
			}
		}
	}
}

std::vector<std::string> CommandStore::buttonNames() const
{
	std::vector<std::string> out;
	for (const auto &kv : m_buttons)
	{
		out.push_back(kv.first);
	}
	return out;
}

std::vector<std::string> CommandStore::setNames() const
{
	std::vector<std::string> out;
	for (const auto &kv : m_sets)
	{
		out.push_back(kv.first);
	}
	return out;
}

std::vector<std::string> CommandStore::acceptanceStops()
{
	return { "[S-200] CommandButton: Upgrade, NeededUpgrade, SpecialPower, Science, RadiusCursorType, the sound lists and CreateAHeroUIAllowableUpgrades are kept as names and not validated or resolved "
			 "(retail resolves them at parse time against the UpgradeCenter, SpecialPowerStore, ScienceStore, InGameUI and Audio / Eva, which are not ported: an unknown Science, sound or upgrade mask name is "
			 "an INI error in retail); the control bar's per-object CommandSet overrides (Object + 0x438 / 0x43C / 0x440) and its availability rules (RW 0x942733) are UI / upgrade lanes" };
}
