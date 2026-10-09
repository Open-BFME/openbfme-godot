// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (Include/Common/Upgrade.h, Source/Common/System/Upgrade.cpp).
//
// UpgradeTemplate / UpgradeCenter (lane UPGRADE-1): the Upgrade blocks of data\ini\default\upgrade.ini and data\ini\upgrade.ini (the TheUpgradeCenter entry of
// the subsystem legend; the include data\ini\createaheroupgrades.inc brings the Create-A-Hero upgrades) and the upgrade masks the objects and players carry.
//
// TARGET FACTS (RotWK game.dat, caveat S-001), read from the disassembly / decompile:
//   * TheUpgradeCenter is RW 0xDE45A0 (allocated 0xC4 bytes at RW 0x63B26D). Its init (RW 0x66FDD3) makes three upgrades before any INI is read:
//     newUpgrade("", assignBit) then friend_makeVeterancyUpgrade(level) (RW 0x66F51D) for VETERAN, ELITE, HEROIC: type OBJECT, name
//     "Upgrade_Veterancy_" + TheVeterancyNames[level] (RW 0x66F403, names RW 0xD9F5E4 REGULAR VETERAN ELITE HEROIC), cost 0, time 0. They take mask bits 0, 1, 2.
//   * newUpgrade(name, assignBit) (RW 0x66FC27): a 0x9C byte UpgradeTemplate (constructor RW 0x66F888), a copy of the upgrade named "DefaultUpgrade" when it
//     exists (findUpgrade RW 0x66F5E5, operator= RW 0x66FA3C), the name and its name key (+0xC); with assignBit the mask bit (+0x38) is the center's counter
//     (+0x10), which then grows by one; linked at the head of the list (RW 0x66F248).
//   * parseUpgradeDefinition (RW 0x66FCDA): the name is the next token; the upgrade is found by name key (RW 0x66F230). A new name is newUpgrade(name, true).
//     For an existing name: load type 5 (the developer reload) unlinks and frees the old template and makes a new one that keeps the old mask bit; every other
//     load type parses the block into a temporary template that is thrown away (THE FIRST DEFINITION WINS; a later block is still parsed, so its errors throw).
//   * the field table is RW 0xC107F8 (26 rows, below), parsed with INI::initFromINI (RW 0x42DB80);
//   * the constructor (RW 0x66F888): Type PLAYER (0), BuildTime 0.0, BuildCost 0, the mask bit -1, every Eva event -1 (None), SkirmishAIHeuristic -1, the bools false,
//     RequiredObjectFilter the default ObjectFilter (RW 0x76406F);
//   * the upgrade mask (Object + 0x28C, Player + 0x14C completed and + 0xBC in production, module data + 8 TriggeredBy / + 0x98 ConflictsWith) is 0x24 words = 1152
//     bits (the walks of RW 0x8D2810 / 0x8D292B stop at bit 0x480);
//   * an upgrade mask field (TriggeredBy, ConflictsWith, ... RW 0x66F603) clears the mask and ORs the bit of every named upgrade; a name that is not an upgrade
//     and is not "None" (any case) is INIException(3, "An upgrade mask references %s, which is not an Upgrade") (RW 0xC10C90). The lookup is by name key
//     (case sensitive).
//   * Object::giveUpgrade (RW 0x693817): when the template's resolved SubUpgradeTemplateNames list (+0x1C .. +0x20) is empty its own bit is set, otherwise
//     THE BITS OF THE SUB UPGRADES (not its own); then updateUpgradeModules (RW 0x6936FE).
// DONOR FACTS (ZH Upgrade.cpp): calcTimeToBuild = BuildTime * LOGICFRAMES_PER_SECOND, calcCostToBuild = BuildCost (RotWK's versions are UPGRADE-1's
// ProductionUpdate port: Production/UpgradeProduction.h).

#pragma once

#include "Common/INI.h"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

struct ObjectFilter;
class Object;
class Player;
class ThingFactory;
class ThingTemplate;

// RW: 0x24 words (1152 bits)
struct UpgradeMaskType
{
	static constexpr unsigned WORDS = 0x24;
	static constexpr unsigned BITS = WORDS * 32;
	std::array<std::uint32_t, WORDS> words{};

	void set(unsigned bit) { words[bit >> 5] |= 1u << (bit & 31); }
	void clear(unsigned bit) { words[bit >> 5] &= ~(1u << (bit & 31)); }
	bool test(unsigned bit) const { return bit < BITS && ((words[bit >> 5] >> (bit & 31)) & 1u) != 0; }
	bool any() const
	{
		for (std::uint32_t w : words)
		{
			if (w)
			{
				return true;
			}
		}
		return false;
	}
	// RW 0x8097D6 testForAny: some bit of `m` is set here
	bool testForAny(const UpgradeMaskType &m) const
	{
		for (unsigned i = 0; i < WORDS; ++i)
		{
			if (words[i] & m.words[i])
			{
				return true;
			}
		}
		return false;
	}
	// RW 0x6AACB3 testForAll: every bit of `m` is set here
	bool testForAll(const UpgradeMaskType &m) const
	{
		for (unsigned i = 0; i < WORDS; ++i)
		{
			if ((words[i] & m.words[i]) != m.words[i])
			{
				return false;
			}
		}
		return true;
	}
	void orWith(const UpgradeMaskType &m)
	{
		for (unsigned i = 0; i < WORDS; ++i)
		{
			words[i] |= m.words[i];
		}
	}
	bool operator==(const UpgradeMaskType &o) const { return words == o.words; }
};

enum UpgradeType
{
	UPGRADE_TYPE_PLAYER = 0,
	UPGRADE_TYPE_OBJECT = 1
};

class UpgradeTemplate
{
public:
	UpgradeTemplate(); // RW 0x66F888

	const std::string &getUpgradeName() const { return m_name; }
	UpgradeType getUpgradeType() const { return (UpgradeType)m_type; }
	int getMaskBit() const { return m_maskBit; }
	float getBuildTimeSeconds() const { return m_buildTime; }
	int getBuildCost() const { return m_cost; }
	// the sub upgrades (resolved after the load); empty: the upgrade grants its own bit
	const std::vector<const UpgradeTemplate *> &getSubUpgrades() const { return m_subUpgrades; }
	// the bits Object::giveUpgrade sets (RW 0x693817)
	UpgradeMaskType grantMask() const;
	// RW 0x66F2C8: UseObjectTemplateForCostDiscount names a template -> that template's ThingTemplate::calcCostToBuild(player, producer, BuildCost); otherwise
	// BuildCost, and with a player: the sum of the sub upgrades' calcCostToBuild(null, null) when there are sub upgrades (the skip of a sub upgrade that the
	// producer's contained object answers for, RW 0x694BF8 / slot 0xAC, is not ported: S-486), times (an OBJECT upgrade without NoUpgradeDiscount)
	// fstp(1.0 + Player::getUpgradeCostChange(name)) (RW 0x6AE7CB), as cvttss2si(cvtsi2ss(cost) * factor). The Brutal AI scaling (RW 0x6AA61B) is S-204's.
	int calcCostToBuild(const Player *player, const Object *producer) const;
	// RW 0x66F1A8: cvttss2si(cvtsi2ss(LOGICFRAMES_PER_SECOND = 5) * BuildTime) (the Brutal AI branch is S-204's)
	int calcTimeToBuild(const Player *player) const;
	const ThingTemplate *costDiscountTemplate() const { return m_costDiscountTemplate; }

	// the fields of the table RW 0xC107F8, in table order (offsets of the RW template)
	std::string m_displayName;                     ///< +0x28 DisplayName (parseAsciiString RW 0x42EE5E)
	std::string m_tooltip;                         ///< +0x2C Tooltip
	int m_type = UPGRADE_TYPE_PLAYER;              ///< +0x4 Type (parseIndexList RW 0x42E956 over RW 0xDA05C8 PLAYER / OBJECT)
	float m_buildTime = 0.0f;                      ///< +0x30 BuildTime (parseReal RW 0x42ED00: seconds)
	int m_cost = 0;                                ///< +0x34 BuildCost (parseInt RW 0x42EC5E)
	std::string m_buttonImage;                     ///< +0x6C ButtonImage
	std::string m_researchSound;                   ///< +0x40 ResearchSound (RW 0x73B217: "NoSound" clears, else the audio event must exist)
	int m_researchCompleteEvaEvent = -1;           ///< +0x44 ResearchCompleteEvaEvent (RW 0x5DE588: "None" -1, else a known Eva event)
	std::string m_unitSpecificSound;               ///< +0x48 UnitSpecificSound
	std::string m_upgradeFX;                       ///< +0x3C UpgradeFX (a plain string in RW; resolved by its users)
	int m_localPlayerGainsUpgradeEvaEvent = -1;    ///< +0x4C
	int m_alliedPlayerGainsUpgradeEvaEvent = -1;   ///< +0x50
	int m_enemyPlayerGainsUpgradeEvaEvent = -1;    ///< +0x54
	int m_localPlayerLosesUpgradeEvaEvent = -1;    ///< +0x58
	int m_alliedPlayerLosesUpgradeEvaEvent = -1;   ///< +0x5C
	int m_enemyPlayerLosesUpgradeEvaEvent = -1;    ///< +0x60
	std::string m_cursor;                          ///< +0x74 Cursor
	bool m_persistsInCampaign = false;             ///< +0x78 (parseBool RW 0x42E558)
	bool m_noUpgradeDiscount = false;              ///< +0x79
	std::vector<std::string> m_subUpgradeTemplateNames; ///< +0x10 (parseAsciiStringVector RW 0x42EED6)
	std::string m_useObjectTemplateForCostDiscount;     ///< +0x7C
	std::shared_ptr<const ObjectFilter> m_requiredObjectFilter; ///< +0x80 RequiredObjectFilter (RW 0x76392F); null = the constructor's default filter (RW 0x76406F)
	std::string m_groupName;                       ///< +0x84 GroupName (RW 0x548990: the name key of the next string)
	unsigned m_groupOrder = 0;                     ///< +0x88 GroupOrder (parseUnsignedInt RW 0x42ECB2)
	std::string m_strategicIcon;                   ///< +0x8C StrategicIcon
	int m_skirmishAIHeuristic = -1;                ///< +0x98 SkirmishAIHeuristic (RW 0x8E02AF: index into RW 0xDB4F44, case sensitive; an unknown name is -1)

	static const FieldParse *fieldParse();

private:
	friend class UpgradeCenter;
	std::string m_name;                            ///< +0x8
	int m_maskBit = -1;                            ///< +0x38
	std::vector<const UpgradeTemplate *> m_subUpgrades; ///< +0x1C .. +0x20
	const ThingTemplate *m_costDiscountTemplate = nullptr; ///< UseObjectTemplateForCostDiscount resolved (RW looks it up per call: TheThingFactory RW 0x6D1305)
};

// parse-time lookups into stores the upgrade lane does not own (TheAudio, TheEva); an unset hook is a loud INIException (code 8), never an accepted name
struct UpgradeParseServices
{
	std::function<bool(const std::string &)> audioEventExists;
	std::function<int(const std::string &)> evaEventIndex; ///< -1 when unknown
};

class UpgradeCenter
{
public:
	UpgradeCenter();
	UpgradeCenter(const UpgradeCenter &) = delete;
	UpgradeCenter &operator=(const UpgradeCenter &) = delete;

	// RW 0x66FDD3: the three veterancy upgrades (bits 0, 1, 2)
	void init();
	// RW 0x66FCDA (the "Upgrade" block)
	void parseUpgradeDefinition(INI *ini);
	static void parseUpgradeDefinitionGlobal(INI *ini); ///< into TheUpgradeCenter (a missing center is a code 8 INIException)
	void registerBlock(INIBlockRegistry &registry);
	// resolve every SubUpgradeTemplateNames list (call after the load); false + *error for a name that is not an upgrade
	bool resolveSubUpgrades(std::string *error);
	// resolve UseObjectTemplateForCostDiscount against the loaded templates (an unknown name keeps the upgrade's own cost, as RW 0x66F2ED does)
	void resolveTemplates(const ThingFactory &things);

	const UpgradeTemplate *findUpgrade(const std::string &name) const; ///< RW 0x66F5E5, case sensitive; null when unknown
	const UpgradeTemplate *findUpgradeByMaskBit(int bit) const;       ///< RW 0x66F218
	const UpgradeTemplate *findVeterancyUpgrade(int level) const;     ///< "Upgrade_Veterancy_" + REGULAR / VETERAN / ELITE / HEROIC
	size_t size() const { return m_byBit.size(); }
	int nextMaskBit() const { return m_nextBit; }
	// the templates in mask bit order (creation order)
	const std::vector<UpgradeTemplate *> &templates() const { return m_byBit; }

	void setParseServices(const UpgradeParseServices &services) { m_services = services; }
	const UpgradeParseServices &parseServices() const { return m_services; }

	// RW 0x66F492 canAffordUpgrade: the player's money covers calcCostToBuild(player, producer) (the "GUI:NotEnoughMoneyToUpgrade" message is the UI's)
	static bool canAffordUpgrade(const Player *player, const UpgradeTemplate *upgrade, const Object *producer);
	// RW 0x66F603: an upgrade mask field (TriggeredBy, ConflictsWith, ...) against TheUpgradeCenter. `names` (optional) receives the names as written
	static void parseUpgradeMask(INI *ini, UpgradeMaskType &mask, std::vector<std::string> *names);

private:
	UpgradeTemplate *newUpgrade(const std::string &name, bool assignBit); // RW 0x66FC27
	std::map<std::string, std::unique_ptr<UpgradeTemplate>> m_byName;
	std::vector<UpgradeTemplate *> m_byBit;
	std::vector<std::unique_ptr<UpgradeTemplate>> m_retired; ///< reloaded templates (RW frees them; kept so stale pointers stay valid)
	int m_nextBit = 0;                                      ///< RW + 0x10
	UpgradeParseServices m_services;
};

// RW 0xDE45A0. The world that loads the INI installs its center (GlobalOwnerChain, like TheCommandStore); null outside a world
extern thread_local UpgradeCenter *TheUpgradeCenter;
