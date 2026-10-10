// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// CommandButton and CommandSet (ZH Include/GameClient/ControlBar.h, Source/GameClient/GUI/ControlBar/ControlBar.cpp parseCommandButtonDefinition /
// parseCommandSetDefinition, Source/Common/INI/INICommandButton.cpp), lane PROD-1. This is the data half of the control bar: the INI blocks and
// the store the control bar owns (ZH ControlBar::m_commandButtons / m_commandSets); no UI.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; full notes: workspace/rebuild/specs/production.md and the CommandButton research):
//   * CommandButton block parser RW 0x5DA711, field table RW 0xC2BAC8 (55 rows, no catch-all), sizeof 0x2E0, constructor RW 0x75D516. A duplicate
//     name under load types 1 / 3 / 4 is parsed into a throw-away button and DISCARDED (RW 0x5DA7C5; ZH parses into the existing one); load type 2
//     makes an override copy chained from the base (RW 0x72054C), load type 5 replaces the button (RW 0x71D875).
//   * CommandSet block parser RW 0x7205B9, table RW 0xC4F3D8: 33 slots (keys "1" .. "33", index 0 .. 32) plus InitialVisible (default 33); a duplicate
//     set under load types 1 / 3 / 4 throws INIException(3, "Duplicate commandset %s found!"); a button name inside a set is resolved at PARSE time
//     (findCommandButton, case sensitive, follows the override chain) and an unknown one throws INIException(3, "Unknown command '%s' found in command
//     set. File: %s Line: %d\n") (RW 0x80C9E1). The ControlBar loads Data\INI\Default\CommandButton.ini, Data\INI\CommandButton.ini and
//     Data\INI\CommandSet.ini itself (RW 0x71CE3A), not through the subsystem legend.
//   * An Object's CommandSet is only a NAME (template field @112); the control bar looks the set up with findNonConstCommandSet (RW 0x71EFA2: exact,
//     case sensitive, the BASE set, no override chain).
//   * Name matches (button, set, template) are case sensitive; Command, Options, lookup and stance names are case insensitive.
//
// Stop S-200 (docs/STOPS.md): the stores a button's fields resolve against at parse time in retail (UpgradeCenter, SpecialPowerStore, ScienceStore,
// Audio / EVA, InGameUI radius cursors) are not ported: Upgrade / NeededUpgrade / SpecialPower / Science / the radius cursor / the sound references
// are kept as NAMES and are not validated (retail: an unknown Science, sound or CreateAHeroUIAllowableUpgrades name is an INI error; an unknown
// Upgrade / SpecialPower / NeededUpgrade is silently NULL). `Object` IS resolved (ThingFactory, an unknown name is NULL as in retail).

#pragma once

#include "Common/INI.h"
#include "GameLogic/ObjectTypes.h"

#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

class ThingFactory;
class ThingTemplate;
class ArchiveFileSystem;

// RW 0xDA4D10 (TheGuiCommandNames, 61 names, value = index)
enum GUICommandType
{
	GUICOMMANDMODE_NONE = 0,
	GUI_COMMAND_FOUNDATION_CONSTRUCT,
	GUI_COMMAND_FOUNDATION_CONSTRUCT_CANCEL,
	GUI_COMMAND_UNIT_BUILD,
	GUI_COMMAND_SECONDARY_UNIT_BUILD,
	GUI_COMMAND_CANCEL_UNIT_BUILD,
	GUI_COMMAND_PLAYER_UPGRADE,
	GUI_COMMAND_OBJECT_UPGRADE,
	GUI_COMMAND_CASTLE_UPGRADE,
	GUI_COMMAND_CANCEL_UPGRADE,
	GUI_COMMAND_ATTACK_MOVE,
	GUI_COMMAND_GUARD,
	GUI_COMMAND_GUARD_WITHOUT_PURSUIT,
	GUI_COMMAND_GUARD_FLYING_UNITS_ONLY,
	GUI_COMMAND_STOP,
	GUI_COMMAND_WAYPOINTS,
	GUI_COMMAND_EXIT_CONTAINER,
	GUI_COMMAND_EVACUATE,
	GUI_COMMAND_EVACUATE_CONTESTED,
	GUI_COMMAND_EXECUTE_UNUSED_01,
	GUI_COMMAND_BEACON_DELETE,
	GUI_COMMAND_SET_RALLY_POINT,
	GUI_COMMAND_SELL,
	GUI_COMMAND_FIRE_WEAPON,
	GUI_COMMAND_SPECIAL_POWER,
	GUI_COMMAND_PURCHASE_SCIENCE,
	GUI_COMMAND_COMBATDROP,
	GUI_COMMAND_SWITCH_WEAPON,
	GUI_COMMAND_AUTOCAST_WEAPON,
	GUI_COMMAND_HIJACK_VEHICLE,
	GUI_COMMAND_CONVERT_TO_CARBOMB,
	GUI_COMMAND_PLACE_BEACON,
	GUI_COMMAND_SPECIAL_POWER_FROM_COMMAND_CENTER,
	GUI_COMMAND_CASTLE_UNPACK,
	GUI_COMMAND_CASTLE_PACK,
	GUI_COMMAND_TOGGLE_WEAPON,
	GUI_COMMAND_HORDE_TOGGLE_FORMATION,
	GUI_COMMAND_SPECIAL_POWER_TOGGLE,
	GUI_COMMAND_SPELL_BOOK,
	GUI_COMMAND_ONE_RING,
	GUI_COMMAND_CREW_EVACUATE,
	GUI_COMMAND_BLOODTHIRSTY,
	GUI_COMMAND_CLOSE_GATE,
	GUI_COMMAND_OPEN_GATE,
	GUI_COMMAND_TOGGLE_GATE,
	GUI_COMMAND_TOGGLE_WEAPONSET,
	GUI_COMMAND_REVIVE,
	GUI_COMMAND_MONSTERDOCK,
	GUI_COMMAND_TOGGLE_NO_AUTO_ACQUIRE,
	GUI_COMMAND_WAKE_AUTO_PICKUP,
	GUI_COMMAND_CASTLE_UNPACK_EXPLICIT_OBJECT,
	GUI_COMMAND_START_SELF_REPAIR,
	GUI_COMMAND_HORDE_SET_FORMATION,
	GUI_COMMAND_DOZER_CONSTRUCT,
	GUI_COMMAND_DOZER_CONSTRUCT_CANCEL,
	GUI_COMMAND_PUSH_VISIBLE_COMMAND_RANGE,
	GUI_COMMAND_POP_VISIBLE_COMMAND_RANGE,
	GUI_COMMAND_TOGGLE_STANCE,
	GUI_COMMAND_SET_STANCE,
	GUI_COMMAND_START_NEIGHBORHOOD_REPAIR,
	GUI_COMMAND_CANCEL_NEIGHBORHOOD,
	GUI_COMMAND_NUM_COMMANDS
};
// lane QA-1: the retail name of a GUICommandType value (the Command token of CommandButton INI, RW 0xDA4D10); "" when out of range
const char *GUICommandName(int command);


// RW 0xDA4C88 (TheCommandOptionNames: bit = index)
enum CommandOption : std::uint32_t
{
	COMMAND_OPTION_NEED_TARGET_ENEMY_OBJECT = 0x1,
	COMMAND_OPTION_NEED_TARGET_NEUTRAL_OBJECT = 0x2,
	COMMAND_OPTION_NEED_TARGET_ALLY_OBJECT = 0x4,
	COMMAND_OPTION_ALLOW_SHRUBBERY_TARGET = 0x10, ///< lane HUD-5: index 4 (RW 0xDA4C88)
	COMMAND_OPTION_NEED_TARGET_POS = 0x20,
	COMMAND_OPTION_NEED_UPGRADE = 0x40,
	COMMAND_OPTION_CONTEXTMODE_COMMAND = 0x200,
	COMMAND_OPTION_NOT_QUEUEABLE = 0x10000,
	COMMAND_OPTION_ALLOW_ROCK_TARGET = 0x200000,         ///< lane HUD-5: index 21 (RW 0xDA4C88)
	COMMAND_OPTION_TOGGLE_IMAGE_ON_WEAPONSET = 0x1000000, ///< lane HUD-4: index 24
	COMMAND_OPTION_NONPRESSABLE = 0x10000000,            ///< lane HUD-5: index 28 (RW 0xDA4C88)
	COMMAND_OPTION_CANCELABLE = 0x80000000u
};

class CommandButton
{
public:
	CommandButton();

	// ---- identity ----
	std::string m_name;
	CommandButton *m_nextOverride = nullptr;  ///< RW +4: the chain findCommandButton follows
	bool m_isOverride = false;                ///< RW +8
	int m_marker = -1;                        ///< RW +0xC (reload bookkeeping)

	// ---- INI fields (RW offsets in the table of ControlBarCommands.cpp) ----
	int m_command = GUICOMMANDMODE_NONE;      ///< RW +0x14 Command
	std::uint32_t m_options = 0;              ///< +0x1C Options
	std::string m_objectName;                 ///< +0x20 Object (the name as written; "" for None)
	const ThingTemplate *m_thingTemplate = nullptr; ///< +0x20 resolved at parse time (NULL when unknown)
	std::string m_upgradeName;                ///< +0x24 Upgrade (S-200: not resolved)
	std::vector<std::string> m_neededUpgrade; ///< +0x28 NeededUpgrade (appends; S-200)
	bool m_neededUpgradeAny = false;          ///< +0x34
	std::vector<std::string> m_buildUpgrades; ///< +0x38 BuildUpgrades (names, resolved at click time)
	std::string m_specialPowerName;           ///< +0x44 SpecialPower (S-200)
	std::string m_toggleButtonName;           ///< +0x48
	std::string m_radiusCursor;               ///< +0x4C RadiusCursorType (S-200)
	std::string m_cursorName, m_invalidCursorName; ///< +0x50 / +0x54
	std::vector<std::string> m_textLabel, m_descriptLabel; ///< +0x58 / +0x64
	std::string m_purchasedLabel, m_conflictingLabel, m_lacksPrerequisiteLabel; ///< +0x70 .. +0x78
	int m_weaponSlot = 0;                     ///< +0x80
	int m_weaponSlotToggle1 = 5, m_weaponSlotToggle2 = 5, m_weaponSlotToggle3 = 5; ///< +0x84 .. +0x8C (5 = none)
	std::array<std::uint32_t, 4> m_flagsUsedForToggle{}; ///< +0x90 (WeaponCondition names RW 0xDA1328)
	int m_maxShotsToFire = 0x7FFFFFFF;        ///< +0xA0
	std::vector<std::string> m_science;       ///< +0xA4 Science (S-200)
	int m_commandButtonBorder = 0;            ///< +0xB0 ButtonBorderType
	std::vector<std::string> m_buttonImageName; ///< +0xB4
	std::vector<std::string> m_unitSpecificSound, m_setAutoAbilityUnitSound, m_unsetAutoAbilityUnitSound; ///< +0xC8 / +0xD4 / +0xE0 (S-200)
	bool m_doubleClick = false, m_radial = false, m_inPalantir = false, m_showProductionCount = false; ///< +0x100 .. +0x104
	bool m_isClickable = true, m_showButton = true, m_requiresValidContainer = false; ///< +0x105 .. +0x107
	int m_requireLevel = 0;                   ///< +0x108
	bool m_autoAbility = false;               ///< +0x10C
	KindOfMaskType m_affectsKindOf{};         ///< +0x110
	bool m_triggerWhenReady = false;          ///< +0x12C
	float m_presetRange = 0.0f, m_autoDelay = 0.0f; ///< +0x130 / +0x134
	bool m_needDamagedTarget = false;         ///< +0x138
	std::array<std::uint32_t, 19> m_autoAbilityDisallowedOnModelCondition{}; ///< +0x13C (ModelCondition names RW 0xD9FAD8)
	std::vector<std::string> m_commandTrigger; ///< +0x188
	std::array<std::uint32_t, 19> m_enableOnModelCondition{}, m_disableOnModelCondition{}; ///< +0x194 / +0x1E0
	int m_commandRangeStart = 0, m_commandRangeCount = 33; ///< +0x22C / +0x230
	std::vector<int> m_stances;               ///< +0x234
	std::string m_createAHeroUIPrerequisiteButtonName; ///< +0x240
	int m_createAHeroUIMinimumLevel = 0;      ///< +0x244
	std::string m_createAHeroUIIconImageName; ///< +0x248
	std::vector<std::string> m_createAHeroUIAllowableUpgrades; ///< +0x24C (names; the retail mask needs the UpgradeCenter, S-200)
	int m_createAHeroUICostIfSelected = 0;    ///< +0x2DC

	bool hasOption(std::uint32_t option) const { return (m_options & option) != 0; }
	// RW 0x75D1DC: the template of the final override
	const ThingTemplate *getThingTemplate() const;
	const CommandButton *getFinalOverride() const;
};

class CommandSet
{
public:
	enum
	{
		MAX_BUTTONS = 33 ///< RW 0xC4F3D8: keys "1" .. "33"
	};
	CommandSet() = default;

	std::string m_name;
	CommandSet *m_nextOverride = nullptr; ///< RW +4
	bool m_isOverride = false;            ///< RW +8
	int m_marker = -1;                    ///< RW +0xC
	std::array<const CommandButton *, MAX_BUTTONS> m_command{}; ///< RW +0x14: the FINAL OVERRIDE button at parse time
	int m_initialVisible = MAX_BUTTONS;   ///< RW +0x98

	const std::string &getName() const { return m_name; }
	// RW 0x80C837 without the GameLogic control bar override hook (RW 0x62FF03, not ported); null for an out of range index
	const CommandButton *getCommandButton(int i) const { return i >= 0 && i < MAX_BUTTONS ? m_command[(size_t)i] : nullptr; }
	// lane HERO-2: a dynamic set (+0x9C = 1, the Create-a-Hero per-rank sets) takes buttons at run time (RW 0x80C8EF: an index below 33; + 0x98 grows to it)
	bool m_dynamic = false; ///< RW +0x9C
	void setDynamicButton(int i, const CommandButton *b)
	{
		if (m_dynamic && i >= 0 && i < MAX_BUTTONS)
		{
			m_command[(size_t)i] = b;
			if (i + 1 > m_initialVisible)
			{
				m_initialVisible = i + 1;
			}
		}
	}
};

// ZH ControlBar's command lists. One store per game; the INI block handlers find it through TheCommandStore (like TheLocomotorStore).
class CommandStore
{
public:
	CommandStore() = default;
	CommandStore(const CommandStore &) = delete;
	CommandStore &operator=(const CommandStore &) = delete;

	// the ThingFactory `Object = Name` resolves against (RW: TheThingFactory; a missing one is the plain int throw 0xDEAD0001)
	void setThingFactory(const ThingFactory *things) { m_things = things; }
	const ThingFactory *thingFactory() const { return m_things; }

	// RW 0x71D6B5 (the base) / 0x71D6EA (the final override); exact name match
	CommandButton *findNonConstCommandButton(const std::string &name) const;
	const CommandButton *findCommandButton(const std::string &name) const;
	// RW 0x71EFA2: the BASE set, exact name match
	CommandSet *findNonConstCommandSet(const std::string &name) const;
	const CommandSet *findCommandSet(const std::string &name) const { return findNonConstCommandSet(name); }

	// the blocks (RW 0x5DA711 / 0x7205B9); the block header is already read
	void parseCommandButtonDefinition(INI *ini);
	void parseCommandSetDefinition(INI *ini);
	// registered as the block handlers of the INI environment (TheCommandStore must be set; a null store is the retail "store missing" fault)
	static void parseCommandButtonDefinitionGlobal(INI *ini);
	static void parseCommandSetDefinitionGlobal(INI *ini);

	// RW 0x71CE3A: Data\INI\Default\CommandButton.ini, Data\INI\CommandButton.ini, Data\INI\CommandSet.ini with load type 1; a missing file is
	// tolerated (as retail). Errors are appended to *errors ("file: message") and the load continues with the next file.
	void loadFromFiles(INIEnvironment &env, std::vector<std::string> *errors);

	// lane HERO-2, RW 0x72028B(name, 1) then RW 0x80C8D2: a new dynamic set of that name takes the name's slot (a set it replaces stays alive: retail leaks
	// it), with no button and 0 visible
	CommandSet *newDynamicCommandSet(const std::string &name);

	// the acceptance stops of this store as report lines ("[S-200] ...")
	static std::vector<std::string> acceptanceStops();

	size_t buttonCount() const { return m_buttons.size(); }
	size_t setCount() const { return m_sets.size(); }
	// names in lexical order (tests, tools)
	std::vector<std::string> buttonNames() const;
	std::vector<std::string> setNames() const;

private:
	CommandButton *newCommandButton(const std::string &name, bool override);
	CommandButton *newCommandButtonOverride(CommandButton *base);
	CommandSet *newCommandSet(const std::string &name, bool override);
	CommandSet *newCommandSetOverride(CommandSet *base);

	const ThingFactory *m_things = nullptr;
	std::map<std::string, std::unique_ptr<CommandButton>> m_buttons;
	std::map<std::string, std::unique_ptr<CommandSet>> m_sets;
	std::deque<std::unique_ptr<CommandButton>> m_buttonOverrides; ///< owned, chained from the bases
	std::deque<std::unique_ptr<CommandSet>> m_setOverrides;
	std::vector<std::unique_ptr<CommandButton>> m_deadButtons;    ///< RW ControlBar + 0x244
	std::vector<std::unique_ptr<CommandSet>> m_deadSets;          ///< RW ControlBar + 0x238
};

extern thread_local CommandStore *TheCommandStore; ///< RW 0xDE7744 is TheControlBar; this is the command part of it
