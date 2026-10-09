// OpenBFME. GPL-3.0.
//
// TheCreateAHeroSystem (lane HERO-2): the CreateAHeroSystem block of Data\INI\CreateAHeroSystem.ini (the classes, subclasses, bling, bling binders and the
// global names) and the in-game side of a Create-a-Hero: the record a player brings (Common/CreateAHeroRecord.h) becomes the upgrades, the per-rank command
// set and the build surcharge of the CreateAHero object.
//
// TARGET FACTS (RotWK game.dat, caveat S-001), read from the disassembly:
//   * the system (RW 0xDE3D84 / 0xDE3D88 are the same object; constructor RW 0x61FB68, "CreateAHeroManager") loads Data\INI\CreateAHeroSystem.ini itself
//     at its init (RW 0x61A10F, INI::load type 1), after TheUpgradeCenter and the control bar; the block's table (RW 0xBFCBD8): CreateAHeroBling RW 0x61E7EE,
//     CreateAHeroBlingBinder RW 0x61E9CD, CreateAHeroClass RW 0x61FFF2, the upgrade names (parseAsciiString RW 0x42EE5E) CreateAHeroMapModeUpgradeName +0x190,
//     CreateAHeroGameModeUpgradeName +0x194, CanBuildCreateAHeroUpgradeName +0x198, CommandSetTemplate +0x1EC, the eleven stat names (RW 0x548990, name keys)
//     +0x19C .. +0x1C0, WeaponGroupName +0x1C4, SpecialAnimPercentChance +0x1C8 (parseReal), HeroRevivalDiscount +0x1CC / SpecialPowerDiscountPerLevel +0x1D0
//     (parseInt; the constructor's 75 / 10), the animation names +0x1D4 / +0x1D8 / +0x1DC, ExamineAnimTweakValue +0x1F4 (parseInt), ModelSwapFadeDown / Up
//     +0x1F8 / +0x1FC ("FadeCreateAHeroScreenDown" / "...Up");
//   * CreateAHeroBling (table RW 0xD9EFD8; 16 bytes: NameTag +0, DescriptionTag +4, BlingUpgradeName +8, GroupName +0xC name key): an empty / "None"
//     BlingUpgradeName, an empty GroupName, an upgrade TheUpgradeCenter does not know, or a second bling of the same upgrade throws (INIException 3,
//     RW 0xBFC39C / 0xBFC3D8 / 0xBFC454 / 0xBFC418); else it is appended to the system's list (+0x164; its index is the "bling index");
//   * CreateAHeroBlingBinder (table RW 0xD9F028; GroupName +0, LabelTag +4, DescriptionTag +8, UISlot +0xC parseUnsignedInt, default -1, BlingType +0x10
//     over ATTRIBUTE / APPEARANCE / INVALID, default INVALID): a missing group / label / description / UISlot or an INVALID type throws; else the binder of
//     that group is found or appended (RW 0x61E78F, +0x170);
//   * CreateAHeroClass (table RW 0xD9F198; 0x20 bytes: NameTag +0, DescriptionTag +4, PowersDescTag +8, IconImage +0xC, UpgradeName +0x10, the subclasses +0x14):
//     the UpgradeName must name an upgrade and no other class (RW 0x61B025), else INIException 3; appended to +0x154 (its index is the class index);
//   * SubClass (RW 0x61F82B; 0xD8 bytes, constructor RW 0x61E4AE; table RW 0xD9F088): BlingUpgrades +0x24 (RW 0x61EB52), Awards +0x30 (RW 0x61A4FF: name keys,
//     each one must be an award of TheAwardSystem), Stats +0x3C (RW 0x61A5B5), UpgradeName +0x20, NameTag +0, DescriptionTag +4, IconImage +8, ButtonImage +0xC,
//     DefaultPrimaryColor / Secondary / Tertiary +0x10 / +0x14 / +0x18 (parseColorInt RW 0x42F13E; default 0xFF00FFFF), SpendableAttributePoints +0x1C
//     (parseInt, default 20), Attribute (RW 0x61DBC8), DefaultFaction +0x64 over Men / Elves / Dwarves / Isengard / Mordor / Wild / Angmar / Arnor / Neutral
//     (RW 0xD9EDD0, default 8 = Neutral), UsableFactions +0x68 (RW 0x61C0A5), ViewInfo +0x6C (RW 0x619141, table RW 0xD9EE08: 21 reals and MapLocation);
//     after the block: the UpgradeName must be unique within the class and a known upgrade (else INIException 3); then EVERY upgrade of TheUpgradeCenter whose
//     GroupName is an Attribute group of the subclass (+0x54, RW 0x61BA74) and that is some bling's upgrade is added to that group (RW 0x61E66F, not a default);
//   * BlingUpgrades (RW 0x61EB52): each token; a leading '@' marks the default; the bling of that upgrade (RW 0x61D21B: its index and group; an upgrade that is
//     no bling is skipped without an error) is added (RW 0x61E66F): the group's default (+0x48) is created as 0 when the group is new (so a '@' on a group's
//     first entry is lost) and set to the bling index when the entry is marked; the index joins the group's list (+0x24) when not there yet, and the list is
//     re-sorted by the upgrades' GroupOrder (RW 0x61E38E -> 0x61E123, compare RW 0x61AA91: Upgrade + 0x88 unsigned less);
//   * Attribute (RW 0x61DBC8, table RW 0xD9EF88): GroupName, MinValueUpgrade, MaxValueUpgrade, DefaultValueUpgrade, all required (INIException 3); the first
//     Attribute of a group wins (RW 0x61DB90, +0x54);
//   * a new CREATE_A_HERO object (Object::initObject RW 0x693EE8 .. 0x693F06, not while a save game loads): RW 0x61B17D finds the record (single player: the
//     system's current hero +0xC; else the game slot whose name key is the player's, its hero +0x64 when +0x60 is set), computes the record's bling choices
//     (RW 0x80C3AB: for every group of the subclass, its whole list; the award bling RW 0x8096B0 .. is not ported), loads its statistics file (RW 0x80B339,
//     not ported), sets the record's object id (RW 0x80967A), RW 0x80AA38 (the per-level power availability, not ported), and applies it (vtable slot 0x10 =
//     RW 0x80ACE3 with 0x2FF): outside the Create-a-Hero screen (+0x18C) the object gets the game mode upgrade and loses the map mode one (RW 0x69388B / 0x691438);
//     with flag 4 every (group, index) of the record gives the upgrade of the bling the record's list of that group holds at that index (RW 0x619B87; out of
//     range: none) and the flags become (flags & ~4) | 8; with flag 1 or 2 the class's upgrade and, when the class upgrade exists, the subclass's are given
//     and the flags become (flags & ~3) | 0x80; with flag 8 the colours go to the drawable (RW 0x80959A, client); then the client fields +0x13C / +0x140;
//     in a multiplayer game the player loses CanBuildCreateAHeroUpgradeName (RW 0x6AE60C);
//   * the game start (RW 0x6315F5 -> 0x61B103): every player whose slot has a hero gets CanBuildCreateAHeroUpgradeName as a COMPLETE player upgrade
//     (RW 0x6AEE22(upgrade, 2, 1)) (INFERENCE: the slot test reads slot + 0x50, whose meaning was not read; a slot with a hero is taken);
//   * a level grant of a CREATE_A_HERO (RW 0x821383 .. 0x8213A9) and a hero revive (RW 0x7815AE) build the command set of the level's Rank (RW 0x809FFB):
//     "CommandSet_<object name>_<unique id>_rank_<rank>" (RW 0xC4F270) is made new (RW 0x72028B(name, 1): a dynamic set replaces a set of that name) and
//     cleared (RW 0x80C8D2), gets the 33 buttons of CommandSetTemplate (RW 0x71EFA2, then RW 0x80C837 / 0x80C8EF), then for every slot 0 .. 32 every power
//     with a button whose ButtonIndex is that slot: the first one always, a later one only when its ExpLevel is below the rank, replaces the slot's button
//     (RW 0x71D6EA; an unknown button is skipped); slot 16 becomes Command_AttackMove; the object's command set override (+0x438) is the set's name and the
//     control bar is marked dirty (+0x28);
//   * the build surcharge (ThingTemplate::calcCostToBuild RW 0x73C28F .. 0x73C2DA): a CREATE_A_HERO template built by a player with a hero, with the template's
//     own cost: + the record's power cost (+0x134, computed by RW 0x809CA6 when 0): for every power i with a known button, the button's cost RW 0x75CD7A at
//     level i + 1: CreateAHeroUICostIfSelected (0 when not above 0); at a level not below CreateAHeroUIMinimumLevel, d = (level - minimum) * SpecialPower
//     DiscountPerLevel and cost - cost * d / 100 (unsigned) when d is not 0.
// The slot's record comes with the game setup (GameNetwork/GameInfo.h SkirmishGameSlot::createAHero, the whole record on the wire) and LiveGame::load installs
// it on every peer at the game start (RW 0x6315F5, after the computer players' AI), checked by validateHero; nothing reads a .cah file during play.
// NOT PORTED (stop S-1226, counted): the Create-a-Hero builder screen (createahero.apt, AptCreateAHero), the lobby screen's hero choice, the award bling, the statistics
// file, the colours and model swaps of the drawable, the Create-a-Hero map mode.

#pragma once

#include "Common/CreateAHeroRecord.h"
#include "Common/INI.h"

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

class CommandSet;
class CommandStore;
class GameLogic;
class Object;
class Player;
class ThingTemplate;
class UpgradeCenter;

struct CreateAHeroBling ///< RW 0xD9EFD8 (16 bytes)
{
	std::string nameTag;     ///< +0
	std::string descriptionTag; ///< +4
	std::string upgradeName; ///< +8 BlingUpgradeName
	std::string groupName;   ///< +0xC (a name key in retail)
};

enum CreateAHeroBlingType
{
	CAH_BLING_ATTRIBUTE = 0,
	CAH_BLING_APPEARANCE = 1,
	CAH_BLING_INVALID = 2
};

struct CreateAHeroBlingBinder ///< RW 0xD9F028
{
	std::string groupName;      ///< +0
	std::string labelTag;       ///< +4
	std::string descriptionTag; ///< +8
	std::uint32_t uiSlot = 0xFFFFFFFFu; ///< +0xC
	int blingType = CAH_BLING_INVALID;  ///< +0x10
};

struct CreateAHeroAttribute ///< RW 0xD9EF88 (+0x54 of the subclass, by group)
{
	std::string groupName, minValueUpgrade, maxValueUpgrade, defaultValueUpgrade;
};

struct CreateAHeroViewInfo ///< RW 0xD9EE08 (client: the builder screen's cameras)
{
	float nearPitch = 0, nearZoom = 0, nearFloor = 0, nearDist = 0, nearShift = 0;            ///< +0x00 .. +0x10
	float farPitch = 0, farZoom = 0, farFloor = 0, farDist = 0, farShift = 0;                 ///< +0x18 .. +0x28
	float closeUpPitch = 0, closeUpZoom = 0, closeUpFloor = 0, closeUpDist = 0, closeUpShift = 0; ///< +0x30 .. +0x40
	float portraitPitch = 0, portraitZoom = 0, portraitFloor = 0, portraitDist = 0, portraitShift = 0; ///< +0x48 .. +0x58
	float normalCam = 0, cameraAngle = 0; ///< +0x60 / +0x64
	int mapLocation = 0;                  ///< +0x68
};

struct CreateAHeroSubClass ///< RW 0x61E4AE (0xD8 bytes)
{
	std::string nameTag, descriptionTag, iconImage, buttonImage; ///< +0 .. +0xC
	std::uint32_t defaultPrimaryColor = 0xFF00FFFFu, defaultSecondaryColor = 0xFF00FFFFu, defaultTertiaryColor = 0xFF00FFFFu; ///< +0x10 .. +0x18
	int spendableAttributePoints = 20; ///< +0x1C
	std::string upgradeName;           ///< +0x20
	std::vector<std::pair<std::string, std::vector<int>>> blingGroups; ///< +0x24: group -> bling indices sorted by GroupOrder (retail: a map by name key)
	std::vector<std::string> awards;   ///< +0x30
	std::vector<std::string> stats;    ///< +0x3C
	std::vector<std::pair<std::string, int>> blingDefaults; ///< +0x48: group -> the default bling index
	std::vector<CreateAHeroAttribute> attributes; ///< +0x54 (first per group)
	int defaultFaction = 8;            ///< +0x64 (Neutral)
	std::vector<int> usableFactions;   ///< +0x68
	CreateAHeroViewInfo viewInfo;      ///< +0x6C

	const std::vector<int> *findGroup(const std::string &group) const;
	const CreateAHeroAttribute *findAttribute(const std::string &group) const;
};

struct CreateAHeroClass ///< RW 0xD9F198 (0x20 bytes)
{
	std::string nameTag, descriptionTag, powersDescTag, iconImage, upgradeName; ///< +0 .. +0x10
	std::vector<CreateAHeroSubClass> subClasses;                               ///< +0x14
};

extern const char *const TheCreateAHeroBlingTypeNames[];  ///< RW 0xD9EDB8
extern const char *const TheCreateAHeroFactionNames[];    ///< RW 0xD9EDD0

class CreateAHeroSystem
{
public:
	// the global names (RW 0xBFCC08 ..)
	std::string mapModeUpgradeName, gameModeUpgradeName, canBuildUpgradeName, commandSetTemplate; ///< +0x190 / +0x194 / +0x198 / +0x1EC
	std::array<std::string, 11> statNames;    ///< +0x19C .. +0x1C0
	std::string weaponGroupName;              ///< +0x1C4
	float specialAnimPercentChance = 0.0f;    ///< +0x1C8
	int heroRevivalDiscount = 75;             ///< +0x1CC
	int specialPowerDiscountPerLevel = 10;    ///< +0x1D0
	std::string selectedCheerAnimName, examineWeaponAnimName, examineSelfAnimName; ///< +0x1D4 / +0x1D8 / +0x1DC
	int examineAnimTweakValue = 0;            ///< +0x1F4
	std::string modelSwapFadeDown = "FadeCreateAHeroScreenDown", modelSwapFadeUp = "FadeCreateAHeroScreenUp"; ///< +0x1F8 / +0x1FC

	void registerBlock(INIBlockRegistry &registry);
	// RW 0x61A10F: Data\INI\CreateAHeroSystem.ini, type 1. Needs the upgrades (TheUpgradeCenter) of the same world.
	void load(INIEnvironment &env);
	void parseSystem(INI *ini);
	bool loaded() const { return m_loaded; }

	const std::vector<CreateAHeroBling> &blings() const { return m_blings; }
	const std::vector<CreateAHeroBlingBinder> &binders() const { return m_binders; }
	const std::vector<CreateAHeroClass> &classes() const { return m_classes; }
	const CreateAHeroBling *bling(int index) const; ///< RW 0x619ADD
	int findBlingByUpgrade(const std::string &upgradeName) const; ///< RW 0x61D21B: the index, -1 when none
	const CreateAHeroSubClass *subClass(std::uint32_t cls, std::uint32_t sub) const; ///< RW 0x619F0E
	const CreateAHeroBlingBinder *binder(const std::string &group) const;
	// lane HERO-2: a game setup's slot hero (GameInfo.h SkirmishGameSlot::createAHero) is checked field by field before it is installed (OpenBFME: retail's
	// lobby message is not ported, S-724): version 8 with a matching checksum and the load flags, a name, a class and subclass of this system, every power
	// empty (no button, rank 0, slot 0) or a known CommandButton at a rank 0 .. 14 on a command set slot 0 .. 32 (CommandSet::MAX_BUTTONS), every bling
	// group once, of the subclass, with an index within its list, a unique id of printable ASCII without spaces (it names the per-rank command sets and
	// levels). False + *why naming the field otherwise
	bool validateHero(const CreateAHeroHero &hero, const CommandStore &commands, std::string *why) const;

	// the parse internals (used by the field parsers)
	void addBling(CreateAHeroBling b, INI *ini);
	void addBinder(CreateAHeroBlingBinder b);
	void addClass(CreateAHeroClass c, INI *ini);
	void addBlingToSubClass(CreateAHeroSubClass &s, int blingIndex, bool isDefault) const; ///< RW 0x61E66F
	void finishSubClass(CreateAHeroSubClass &s, const CreateAHeroClass &cls, INI *ini) const; ///< the tail of RW 0x61F82B

private:
	std::vector<CreateAHeroBling> m_blings;          ///< +0x164
	std::vector<CreateAHeroBlingBinder> m_binders;   ///< +0x170
	std::vector<CreateAHeroClass> m_classes;         ///< +0x154
	bool m_loaded = false;
};

extern thread_local CreateAHeroSystem *TheCreateAHeroSystem;

// The heroes of a game (the system's slot records, RW 0x61B083 / 0x61B2DF): one per player, the in-game side of RW 0x61B17D / 0x80ACE3 / 0x809FFB.
class CreateAHeroGame
{
public:
	explicit CreateAHeroGame(GameLogic &logic) : m_logic(logic) {}

	// the slot's hero of a player (the lobby's choice); RW 0x61B103 at the game start gives the player CanBuildCreateAHeroUpgradeName
	void assign(Player &player, const CreateAHeroHero &hero);
	void startGame(); ///< RW 0x61B103
	CreateAHeroHero *heroOf(const Player &player);              ///< RW 0x61B2DF
	CreateAHeroHero *heroOfObject(std::uint32_t objectID);       ///< RW 0x619562
	const std::map<int, CreateAHeroHero> &heroes() const { return m_heroes; }

	void onCreated(Object &obj);                                  ///< RW 0x61B17D (Object::initObject)
	void buildCommandSet(Object &obj, int rank);                  ///< RW 0x809FFB (level grant, revive)

	// the per-group choices of a record (RW 0x80C3AB, + 0x74), computed by onCreated
	const std::vector<std::pair<std::string, std::vector<int>>> &choicesOf(std::uint32_t objectID) const;

	struct Stats
	{
		std::uint64_t created = 0, upgradesGiven = 0, missingUpgrades = 0, commandSets = 0, unknownButtons = 0, noRecord = 0;
	};
	const Stats &stats() const { return m_stats; }
	std::uint32_t crc() const;
	static std::vector<std::string> stopLines();

private:
	void apply(CreateAHeroHero &hero, Object &obj, std::uint32_t flags); ///< RW 0x80ACE3
	void buildLevels(CreateAHeroHero &hero, Object &obj);                 ///< RW 0x80AA38
	bool powerLevel(const CreateAHeroHero &hero, int power, const std::string &level, const std::string &newLevel, const std::string &target, Object &obj); ///< RW 0x80A190
	int powerCost(const CreateAHeroHero &hero) const;             ///< RW 0x809CA6

	GameLogic &m_logic;
	std::map<int, CreateAHeroHero> m_heroes; ///< by player index
	std::map<int, std::vector<std::pair<std::string, std::vector<int>>>> m_choices; ///< + 0x74 by player index
	Stats m_stats;
};

namespace CreateAHeroLibrary
{
// The system heroes of the mounted archives: every data\systemheroes\*.cah (Data1.big). A file that fails to load is an error in *errors.
std::vector<CreateAHeroHero> systemHeroes(class ArchiveFileSystem &fs, std::vector<std::string> *errors);
} // namespace CreateAHeroLibrary
