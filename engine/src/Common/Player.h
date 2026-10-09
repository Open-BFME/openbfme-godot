// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Player (ZH Include/Common/Player.h, Source/Common/RTS/Player.cpp), lane LOGIC-1: a side of a game as the object layer sees it: its
// index, name, faction (PlayerTemplate), type, colour, money, default team and relationships. Everything else ZH's Player carries
// (energy, science and upgrade state, build list, AI, squads, radar, stats, battle plans, tunnel system, resource gathering) belongs
// to the lanes that port those systems and is not here.
//
// SOURCES: ZH Player::init(const PlayerTemplate*) (Player.cpp:360-470) sets the side, colour and money from the template (a template
// money of 0 means "the game's starting cash"); Player::initFromDict (Player.cpp:808-) reads playerFaction, playerName, playerDisplayName,
// playerIsHuman, playerIsSkirmish, multiplayerStartIndex; Player::getRelationship (Player.cpp:574) and setPlayerRelationship (:607).
// TARGET: the RotWK template fields are PlayerTemplate.h's table (RW 0x5FDF75). Stop S-146: which RotWK Player fields beyond these a
// faction needs at map start (science points, command points, spell book) is the lanes' to port.

#pragma once

#include "Common/CommandPoints.h"
#include "Common/Money.h"
#include "Common/NameKeyGenerator.h"
#include "Common/PlayerHeroList.h"
#include "Common/PlayerScience.h"
#include "Common/PlayerTemplate.h"
#include "Common/ScoreKeeper.h"
#include "Common/Upgrade.h"
#include "GameLogic/ObjectTypes.h"

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <algorithm>
#include <vector>

class StateHasher;
class Team;
class Object;
class ThingTemplate;
class GameLogic;
class PlayerEconomyHost;
class TunnelTracker;
struct ObjectFilter;

enum PlayerType
{
	PLAYER_HUMAN = 0,
	PLAYER_COMPUTER
};

class Player
{
public:
	explicit Player(int index)
		: m_index(index)
	{
	}

	int getPlayerIndex() const { return m_index; }
	// lane HERO-2: the Create-a-Hero build surcharge of the player's slot hero (the record's power cost + 0x134, RW 0x809CA6; -1 = the player has no hero),
	// set by GameLogic/CreateAHeroSystem.h's CreateAHeroGame::assign and read by BuildAssistant::calcCostToBuild (RW 0x73C2AF .. 0x73C2DA)
	int getCreateAHeroSurcharge() const { return m_createAHeroSurcharge; }
	void setCreateAHeroSurcharge(int cost) { m_createAHeroSurcharge = cost; }
	const std::string &getPlayerName() const { return m_name; }
	NameKeyType getPlayerNameKey() const { return m_nameKey; }
	const std::string &getPlayerDisplayName() const { return m_displayName; }
	// the faction the player plays: the PlayerTemplate's Side, "" for the neutral player
	const std::string &getSide() const { return m_side; }
	const PlayerTemplate *getPlayerTemplate() const { return m_template; }
	PlayerType getPlayerType() const { return m_type; }
	bool isSkirmishAI() const { return m_skirmish; }
	bool isObserver() const { return m_observer; }
	// RW Player + 0x35A / + 0x754 (the victory lane's): a defeated player is not counted among the live players
	bool isDefeated() const { return m_defeated; }
	void setDefeated(bool defeated) { m_defeated = defeated; }
	// RW Player + 0x4CC (VictoryConditions::update RW 0x809098): the logic frame the victory rules eliminated the player, 0 until then
	unsigned getDefeatFrame() const { return m_defeatFrame; }
	void setDefeatFrame(unsigned frame) { m_defeatFrame = frame; }
	// RW Player + 0x334 (set by RW 0x6AA847 for a script, 0.0 at init RW 0x6B0346): the share of a victim's BountyValue this player's kills pay when the killer carries no
	// BOUNTY_PERCENTAGE modifier (Economy::awardBounty, RW 0x6AC06F)
	float getBountyPercent() const { return m_bountyPercent; }
	void setBountyPercent(float percent) { m_bountyPercent = percent; }
	int getMultiplayerStartIndex() const { return m_mpStartIndex; }
	// 0xAARRGGBB (ZH Player::m_color)
	std::uint32_t getPlayerColor() const { return m_color; }
	void setPlayerColor(std::uint32_t argb)
	{
		m_color = argb;
		m_colorExplicit = true;
	}
	// 0xAARRGGBB: the colour at night (playerNightColor of the side, RGBNightColor of the lobby colour; ZH Player::m_nightColor, RW +0x2A4); 0 when the side sets none
	std::uint32_t getPlayerNightColor() const { return m_nightColor; }
	void setPlayerNightColor(std::uint32_t argb) { m_nightColor = argb; }
	// the skirmish AI level of the side (skirmishDifficulty: 0 easy .. 3 brutal), -1 for a side without one; the AI lane reads it
	int getSkirmishDifficulty() const { return m_skirmishDifficulty; }
	void setSkirmishDifficulty(int d) { m_skirmishDifficulty = d; }
	// the colour tints the player's models: the map or the lobby chose it, or the faction is one that can be played (a template with a
	// StartingBuilding; Civilian, Neutral and Observer have no team colour). MAPOBJ-1's rule for side colours (stop S-119).
	bool hasTeamColor() const { return m_colorExplicit || (m_template && m_template->hasStartingBuilding()); }

	Money *getMoney() { return &m_money; }
	const Money *getMoney() const { return &m_money; }

	Team *getDefaultTeam() const { return m_defaultTeam; }
	void setDefaultTeam(Team *team) { m_defaultTeam = team; }

	// ZH Player::getRelationship(const Team *): a team override, then one for the team's player, else NEUTRAL
	Relationship getRelationship(const Team *that) const;
	Relationship getRelationship(const Player *that) const;
	void setPlayerRelationship(const Player *that, Relationship r);
	void setTeamRelationship(const Team *that, Relationship r);
	void removeTeamRelationship(const Team *that);

	// ZH Player::init(const PlayerTemplate *): `defaultStartingCash` is GameInfo::getStartingCash (used when the template has 0)
	void init(const PlayerTemplate *pt, std::uint32_t defaultStartingCash);
	void friend_setIdentity(const std::string &name, NameKeyType key, const std::string &displayName)
	{
		m_name = name;
		m_nameKey = key;
		m_displayName = displayName;
	}
	void setPlayerType(PlayerType type, bool skirmish)
	{
		m_type = type;
		m_skirmish = skirmish;
	}
	void setMultiplayerStartIndex(int i) { m_mpStartIndex = i; }

	void crc(StateHasher &hasher) const;

	// ---- lane PROD-1: what production reads of a player ------------------------------------------------------------------------
	// (the command points are the economy's, lane ECON-1: commandPoints() above, Economy::canAffordCommandPoints for the production checks)
	// RW 0x6AC856 Player::allowedToBuild's inputs: +0x358 units, +0x359 structures (both true at start) and the set of template ids at +0x720 the scripts forbade
	void setCanBuildUnits(bool v) { m_canBuildUnits = v; }
	void setCanBuildBase(bool v) { m_canBuildBase = v; }
	bool canBuildUnits() const { return m_canBuildUnits; }
	bool canBuildBase() const { return m_canBuildBase; }
	void setTemplateBuildable(unsigned short templateId, bool buildable)
	{
		if (buildable)
		{
			m_disabledTemplateIds.erase(templateId);
		}
		else
		{
			m_disabledTemplateIds.insert(templateId);
		}
	}
	bool isTemplateDisabled(unsigned short templateId) const { return m_disabledTemplateIds.count(templateId) != 0; }
	// lane SCRIPT-3: the scripts' science availability (PLAYER_SCIENCE_AVAILABILITY): RW + 0x31C the disabled sciences, + 0x328 the hidden ones (vectors).
	// RW 0x6AE412(science, availability 0 Available / 1 Disabled / 2 Hidden): the first entry in the disabled list is erased, else the first in the
	// hidden list; then Disabled appends to the disabled list, Hidden to the hidden list. The spell book reads them (RW 0x6AC25F / 0x6AC287)
	void setScienceAvailability(ScienceType st, int availability)
	{
		auto d = std::find(m_disabledSciences.begin(), m_disabledSciences.end(), st);
		if (d != m_disabledSciences.end())
		{
			m_disabledSciences.erase(d);
		}
		else
		{
			auto h = std::find(m_hiddenSciences.begin(), m_hiddenSciences.end(), st);
			if (h != m_hiddenSciences.end())
			{
				m_hiddenSciences.erase(h);
			}
		}
		if (availability == 1)
		{
			m_disabledSciences.push_back(st);
		}
		else if (availability == 2)
		{
			m_hiddenSciences.push_back(st);
		}
	}
	bool isScienceDisabled(ScienceType st) const { return std::find(m_disabledSciences.begin(), m_disabledSciences.end(), st) != m_disabledSciences.end(); }
	bool isScienceHidden(ScienceType st) const { return std::find(m_hiddenSciences.begin(), m_hiddenSciences.end(), st) != m_hiddenSciences.end(); }
	// The player's current selection as an ordered list of object ids (ZH: the player's selected group, set by the selection messages of the command
	// list; RW 0x6AAB85 gives a command the issuing player's selection). Simulation state: hashed, because the first selected object is the
	// producer of a production command.
	std::vector<ObjectID> &selection() { return m_selection; }
	const std::vector<ObjectID> &selection() const { return m_selection; }
	// ZH Player::m_squads (NUM_HOTKEY_SQUADS = 10): the control groups, in membership order. Simulation state (hashed): the team messages create, select and
	// add them (lane HUD-1, GameLogic/PlayerCommands.cpp).
	enum { NUM_HOTKEY_SQUADS = 10 };
	std::vector<ObjectID> &hotkeySquad(int n) { return m_hotkeySquads.at((size_t)n); }
	const std::vector<ObjectID> &hotkeySquad(int n) const { return m_hotkeySquads.at((size_t)n); }

	// ---- lane ECON-1: money, command points, cost modifiers -------------------------------------------------------------------
	// Money flows the way the retail callers make them (RW 0x7B17EF / 0x7B18B8): through the player's score keeper (Player + 0x3DC)
	bool canAfford(std::uint32_t amount) const { return m_money.countMoney() >= amount; }
	std::uint32_t withdrawMoney(std::uint32_t amount, bool playSound = true) { return m_money.withdraw(amount, &m_score, playSound); }
	void depositMoney(std::uint32_t amount, bool playSound = true) { m_money.deposit(amount, &m_score, playSound); }
	ScoreKeeper &getScoreKeeper() { return m_score; }
	const ScoreKeeper &getScoreKeeper() const { return m_score; }

	// the command points (RW Player + 0x60); the limit needs the world (the records' filters), so these ask the economy host
	CommandPoints &commandPoints() { return m_commandPoints; }
	const CommandPoints &commandPoints() const { return m_commandPoints; }
	int commandPointLimit() const;                                  // RW 0x6A7B9F
	int commandPointsAvailable() const;                              // RW 0x6A7F6A
	bool canAffordCommandPoints(const ThingTemplate &tt) const;      // RW 0x6A7F79
	bool hasObjectMatching(const ObjectFilter &filter, bool completedOnly) const; // RW 0x6ABD0B
	float getProductionCostChangeBasedOnKindOf(const ThingTemplate &tt, bool slaughter = false) const; // RW 0x6AD8A7
	int applyHandicapMoney(int amount) const;                        // RW 0x6AA858

	// ---- lane UPGRADE-1: the player's upgrades -------------------------------------------------------------------------------------
	// RW Player + 0x9C: the Upgrade records (status IN_PRODUCTION 1 / COMPLETE 2, newest first), + 0xBC the in-production mask, + 0x14C the completed mask.
	enum UpgradeStatus
	{
		UPGRADE_STATUS_INVALID = 0,
		UPGRADE_STATUS_IN_PRODUCTION = 1,
		UPGRADE_STATUS_COMPLETE = 2
	};
	// RW 0x6AEE22 addUpgrade(template, status, silent): the record is found or made (at the head); IN_PRODUCTION sets the in-production bit; COMPLETE clears it,
	// sets the completed bit and runs onUpgradeCompleted (RW 0x6AE483: updateUpgradeModules on every object of the player through the host, then the Eva gain
	// event unless silent, which is the audio lane's: counted in upgradeEvaEventsNotPlayed())
	void addUpgrade(const UpgradeTemplate *upgrade, UpgradeStatus status, bool silent = false);
	// RW 0x6AE60C removeUpgrade(template, silent): a PLAYER upgrade's record leaves and both bits clear, and only a record that was COMPLETE goes on; an OBJECT
	// upgrade always goes on: RW 0x6AE546 removes the upgrade from every object of the player (Object::removeUpgrade RW 0x691438), then the Eva lose event
	void removeUpgrade(const UpgradeTemplate *upgrade, bool silent = false);
	bool hasUpgradeComplete(const UpgradeTemplate *upgrade) const { return upgrade && m_upgradesCompleted.test((unsigned)upgrade->getMaskBit()); }       // RW 0x6AC2AF
	bool hasUpgradeInProduction(const UpgradeTemplate *upgrade) const { return upgrade && m_upgradesInProgress.test((unsigned)upgrade->getMaskBit()); } // RW 0x6AB2E1
	const UpgradeMaskType &getCompletedUpgradeMask() const { return m_upgradesCompleted; }
	int upgradeStatus(const UpgradeTemplate *upgrade) const; ///< the record's status, UPGRADE_STATUS_INVALID when there is none
	unsigned upgradeEvaEventsNotPlayed() const { return m_upgradeEvaNotPlayed; }
	// by name through TheUpgradeCenter (std::logic_error when no center is installed). A name that is not an upgrade is RW's NULL template: the queries answer
	// false (RW 0x6AC2AF / 0x691421 return false for NULL), a grant or removal by such a name throws (no RW caller grants a NULL template)
	void addCompletedUpgrade(const std::string &name) { addUpgrade(resolveUpgrade(name, true), UPGRADE_STATUS_COMPLETE); }
	void removeCompletedUpgrade(const std::string &name) { removeUpgrade(resolveUpgrade(name, true)); }
	bool hasUpgradeComplete(const std::string &name) const { return hasUpgradeComplete(resolveUpgrade(name, false)); }
	static const UpgradeTemplate *resolveUpgrade(const std::string &name, bool mustExist);

	// RW Player + 0x714: the CostModifierUpgrade entries in the order they were added (RW 0x6AD845 push_back / 0x6AE74C removes the first match)
	struct CostModifier
	{
		std::shared_ptr<const ObjectFilter> filter; ///< a copy of the module's ObjectFilter (null = unset)
		std::vector<float> percents;                ///< a copy of the module's Percentage list
		ObjectID source = INVALID_ID;               ///< the object whose module made it
		bool slaughter = false;                     ///< the module's Slaughter flag
	};
	void addCostModifier(std::shared_ptr<const ObjectFilter> filter, const std::vector<float> &percents, ObjectID source, bool slaughter);
	bool removeCostModifier(const ObjectFilter *filter, const std::vector<float> &percents, ObjectID source);
	const std::vector<CostModifier> &costModifiers() const { return m_costModifiers; }
	// RW Player + 0x3D0: the UpgradeDiscount entries (RW 0x6B2C67 add / 0x6B1992 remove, reference counted by (source template name, percents, ApplyToTheseUpgrades))
	struct UpgradeDiscount
	{
		std::string sourceName;
		int count = 0;
		std::vector<float> percents;
		std::vector<std::string> applyTo;
		float current = 0.0f; ///< RW + 0x20 = percents[min(count - 1, size - 1)], 0 when count is 0 (RW 0x6AB5E0)
	};
	void addUpgradeDiscount(const std::string &sourceName, const std::vector<float> &percents, const std::vector<std::string> &applyTo);
	void removeUpgradeDiscount(const std::string &sourceName, const std::vector<float> &percents, const std::vector<std::string> &applyTo);
	const std::vector<UpgradeDiscount> &upgradeDiscounts() const { return m_upgradeDiscounts; }
	// RW 0x6AE7CB: the sum over the entries of their percentage for this upgrade (an entry without an ApplyToTheseUpgrades list, or one that names the
	// upgrade, contributes its current value)
	float getUpgradeCostChange(const std::string &upgradeName) const;

	// ---- lane SPELL-1: rank, skill points, science purchase points and sciences (RW Player + 8 .. + 0x30, + 0x310) ----------------------
	PlayerScience &science() { return m_science; }
	const PlayerScience &science() const { return m_science; }
	bool hasScience(ScienceType st) const { return m_science.hasScience(st); }
	// binds the rank / science state to the template and the game mode and resets it (RW Player::init -> vslot 3 RW 0x6AEE11); the PlayerList calls it
	// right after init() (the stores TheScienceStore / TheRankInfoStore must be installed for the rank 1 points and the intrinsic sciences)
	void initScience(const SpellGameMode &mode)
	{
		m_science.bind(m_template, mode, &m_score);
		m_science.reset();
	}

	// ---- lane HERO-1: the heroes the player can recruit or revive (RW Player + 0x758; Common/PlayerHeroList.h) ----------------------------------
	PlayerHeroList &heroes() { return m_heroes; }
	const PlayerHeroList &heroes() const { return m_heroes; }

	// lane GARRISON-2: RW Player + 0x308, the player's tunnel network (GameLogic/Object/Contain/TunnelContainRuntime.h, defined there); made on first use. Its state
	// is hashed by the tunnels (TunnelContain::crc)
	TunnelTracker &tunnelTracker();
	const TunnelTracker *tunnelTrackerIfAny() const { return m_tunnelTracker.get(); }

	void setEconomyHost(PlayerEconomyHost *host) { m_host = host; }
	PlayerEconomyHost *economyHost() const { return m_host; }

private:
	PlayerEconomyHost &host() const;

	int m_index;
	std::string m_name;
	NameKeyType m_nameKey = NAMEKEY_INVALID;
	int m_createAHeroSurcharge = -1; // lane HERO-2
	std::string m_displayName;
	std::string m_side;
	const PlayerTemplate *m_template = nullptr;
	PlayerType m_type = PLAYER_COMPUTER;
	bool m_skirmish = false;
	bool m_observer = false;
	bool m_defeated = false;
	unsigned m_defeatFrame = 0;
	float m_bountyPercent = 0.0f;
	int m_mpStartIndex = 0;
	std::uint32_t m_color = 0;
	std::uint32_t m_nightColor = 0;
	int m_skirmishDifficulty = -1;
	bool m_colorExplicit = false;
	Money m_money;
	ScoreKeeper m_score;
	CommandPoints m_commandPoints;
	struct UpgradeRecord
	{
		const UpgradeTemplate *upgrade;
		int status;
	};
	std::vector<UpgradeRecord> m_upgradeRecords; ///< RW + 0x9C (newest first)
	UpgradeMaskType m_upgradesInProgress;        ///< RW + 0xBC
	UpgradeMaskType m_upgradesCompleted;         ///< RW + 0x14C
	unsigned m_upgradeEvaNotPlayed = 0;          ///< not state: Eva events the audio lane would play
	PlayerScience m_science; // SPELL-1
	PlayerHeroList m_heroes; // HERO-1 (RW + 0x758)
	std::vector<CostModifier> m_costModifiers;
	std::vector<UpgradeDiscount> m_upgradeDiscounts;
	PlayerEconomyHost *m_host = nullptr; ///< not state: set by the PlayerList
	Team *m_defaultTeam = nullptr;
	std::map<int, Relationship> m_playerRelations;
	std::map<std::uint32_t, Relationship> m_teamRelations;
	bool m_canBuildUnits = true, m_canBuildBase = true;
	std::set<unsigned short> m_disabledTemplateIds;
	std::vector<ScienceType> m_disabledSciences, m_hiddenSciences; ///< lane SCRIPT-3 (RW + 0x31C / + 0x328)
	std::vector<ObjectID> m_selection;
	std::array<std::vector<ObjectID>, NUM_HOTKEY_SQUADS> m_hotkeySquads;
	std::shared_ptr<TunnelTracker> m_tunnelTracker; ///< lane GARRISON-2 (RW + 0x308)
};
