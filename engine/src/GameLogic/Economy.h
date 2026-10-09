// OpenBFME. GPL-3.0.
//
// Economy (lane ECON-1): the world-level half of the economy. GameLogic owns one; it holds the GameData values and the game context the economy rules read
// (GameLogic/EconomySettings.h), the TerrainResourceManager, and it is the PlayerEconomyHost every Player asks for what needs the world: the command point
// limit and its records, whether a player owns an object a filter allows, the cost modifier multiplier, the money handicap.
//
// Money rules (RotWK game.dat, caveat S-001; lane ECON-1 read them):
//   * every payment goes through Money::deposit / withdraw with the player's ScoreKeeper (Player::depositMoney / withdrawMoney);
//   * income from buildings: TerrainResourceBehavior (GameLogic/Module/TerrainResourceBehavior.h) pays from its claimed ground, AutoDepositUpdate pays a
//     fixed amount; AutoDepositUpdate, the bounty of a kill and CommandPoints' friends multiply by GameData MultiPlayMoneyMult[n - 1] in a LAN / skirmish /
//     internet game (n = the live playable players, RW 0x6A8630) and then apply the player's handicap (RW 0x6AA858); TerrainResourceBehavior does neither;
//   * the live playable player count n (RW 0x6A8630, args the player list, withCounters): players 0 .. 19 with a PlayerTemplate whose PlayableSide is set, that are
//     not observers (RW + 0x754) and not defeated (RW + 0x35A); with counters it adds each one's two Living World territory counters;
//   * the multiplayer test (RW 0x625456): the game mode is LAN (1), internet (5) or skirmish (2), or a replay of such a game.
//
// Seams (everything the economy reads that another lane owns; each is reported by report(), none is a silent default):
//   * attribute modifiers (the AttributeModifierPoolUpdate every object carries: RW 0x68C82D): `attributeModifierProduct(object, type)` is 1.0 times the product of
//     the active modifiers of that type; with no provider installed no modifier is active (nothing in this build applies one): stop S-253;
//   * the object's experience tracker (RW 0x79D833): a hook; without one the experience a resource building earns is dropped and counted: stop S-256;
//   * floating text and the debug overlay: IncomeEvents (a bounded log and an optional listener), not simulation state, never hashed.

#pragma once

#include "Common/PlayerEconomyHost.h"
#include "Common/StateHash.h"
#include "GameLogic/EconomySettings.h"
#include "GameLogic/ObjectTypes.h"
#include "GameLogic/System/TerrainResourceManager.h"

#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class GameLogic;
class Object;
class Player;
class ThingTemplate;

class Economy : public PlayerEconomyHost
{
public:
	// RW AttributeModifier type list (RW 0xD8AF48): the indices the economy reads (lane WEAPON-1 lists the combat ones)
	enum AttributeModifierType
	{
		ATTRIBUTE_MODIFIER_PRODUCTION = 13,        ///< RW 0xD (ProductionUpdate speed, TerrainResourceBehavior, AutoDepositUpdate)
		ATTRIBUTE_MODIFIER_BOUNTY_PERCENTAGE = 17, ///< RW 0x11
		ATTRIBUTE_MODIFIER_COMMAND_POINT_BONUS = 24 ///< RW 0x18
	};

	explicit Economy(GameLogic &logic);
	~Economy() override;
	Economy(const Economy &) = delete;
	Economy &operator=(const Economy &) = delete;

	EconomySettings &settings() { return m_settings; }
	const EconomySettings &settings() const { return m_settings; }
	EconomyContext &context() { return m_context; }
	const EconomyContext &context() const { return m_context; }
	TerrainResourceManager &resources() { return m_resources; }
	// GameData's keep-score switch: the context's and every player's score keeper (hashed)
	void setScoring(bool on);
	// a new game: the grid, the claims and the log go (the settings stay)
	void reset();

	// ---- the game's counts and multipliers ----------------------------------------------------------------------------------
	int livePlayableCount(bool withTerritoryCounters) const;       // RW 0x6A8630
	float multiPlayMoneyMult(int livePlayers) const;                 // RW 0x642002: MPn - 1, 1.0 outside [0, 20)
	bool isMultiplayerGame() const;                                  // RW 0x625456
	bool isMultiplayerCommandPointGame() const;                      // RW 0x6254A1
	// the common tail of AutoDeposit / bounty income (RW 0x89DC9C .. 0x89DCE5): `amount` as a float already times the bonuses -> the multiplayer money
	// multiplier (x87 fmul) -> cvttss2si -> the handicap
	int applyIncomeMultipliers(const Player &owner, float amountF) const;
	// RW 0x6AA858 (PlayerEconomyHost): the campaign handicap, identity in a skirmish
	int applyHandicapMoney(const Player &player, int amount) const override;

	// an integer object field of a template (CommandPoints, CommandPointBonus, BountyValue, BuildCost, ...): 0 when the template never set it (the template's
	// default), an error when the field is not an integer
	static long long templateInt(const ThingTemplate &tt, const char *field);
	// a KindOf of the template by the binary's name (false when the name is not in the binary's list)
	bool templateKindOf(const ThingTemplate &tt, const char *kindOfName) const;

	// ---- command points ------------------------------------------------------------------------------------------------------
	// RW 0x6AA432 -> 0x6A7C86 for one player / RW 0x6A84F3: the whole list; call after the players are configured (retail does it at the end of map start, and
	// per player at its reset)
	void initCommandPoints(Player &player);
	void initAllCommandPoints();
	int commandPointUsageOf(const Object &obj) const;                // RW 0x6A7FAA
	int commandPointBonusOf(const Object &obj) const;                // RW 0x6A7C01
	void objectGained(Player &player, const Object &obj);            // RW 0x6AA56D
	void objectLost(Player &player, const Object &obj);              // RW 0x6AA590
	int commandPointLimit(const Player &player) const override;      // RW 0x6A7B9F
	bool canAffordCommandPoints(const Player &player, const ThingTemplate &tt) const override; // RW 0x6A7F79
	// lane UPGRADE-1 (PlayerEconomyHost): RW 0x6AE483 / 0x6AE546 walk the player's teams' members; here the objects of the logic list the player controls, in
	// list order (INFERENCE: the order of RW's team walk was not reproduced; stop S-482)
	void updateUpgradeModulesOfPlayer(const Player &player) override;
	void removeUpgradeFromObjectsOfPlayer(const Player &player, const UpgradeTemplate &upgrade) override;

	// ---- what the players ask the world --------------------------------------------------------------------------------------
	bool hasObjectMatching(const Player &player, const ObjectFilter &filter, bool completedOnly) const override;   // RW 0x6ABD0B
	// the number of the player's objects the filter allows, an object under construction not counted (RW 0x6ABABD with the callback RW 0x885230)
	int countOwnedObjectsAllowedBy(const Player &player, const ObjectFilter &filter) const;
	float productionCostChange(const Player &player, const ThingTemplate &tt, bool slaughter) const override;    // RW 0x6AD8A7

	// ---- bounty --------------------------------------------------------------------------------------------------------------
	// RW 0x6AC06F Player::awardBounty(killer, victim) on the player that gets the credit: the number of cash paid (0 when nothing was). Nothing for a missing killer or a
	// victim still under construction (RW 0x44DDEC with status 2). value = the victim template's BountyValue (RW + 0x580); a HERO victim (KindOf bit 90, RW 0x68DDF1) is scaled
	// by its experience level: value = trunc(value + value * n / d) in x87 at 24 bits, n / d from the hook (stop S-261: without the hook the unscaled value is used and counted).
	// percent = the killer's BOUNTY_PERCENTAGE modifier sum (starts 0.0), or the player's own bounty percent when that is exactly 0.0; amount = ceil(value * percent) (x87,
	// MSVCR71 ceil, fstp dword, fistp); in a multiplayer game amount = ftol(amount * MultiPlayMoneyMult[n - 1]); then the handicap; a non-zero amount is deposited (score keeper,
	// sound) and logged as an income event (the floating text at the victim, RW 0x6AC185 .. 0x6AC1E4). The kill credit that calls it is the combat lane's (stop S-257).
	int awardBounty(Player &killerOwner, Object *killer, const Object &victim);
	typedef std::function<bool(const Object &victim, int &numerator, int &denominator)> HeroBountyScale;
	void setHeroBountyScale(HeroBountyScale scale) { m_heroBountyScale = std::move(scale); }
	unsigned long long unscaledHeroBounties() const { return m_unscaledHeroBounties; }

	// ---- seams ---------------------------------------------------------------------------------------------------------------
	// returns whether a modifier of `type` is active on the object and multiplies `product` (which the caller starts at 1.0)
	typedef std::function<bool(const Object &obj, int type, float &product)> AttributeModifierProvider;
	void setAttributeModifierProvider(AttributeModifierProvider p) { m_attributeModifiers = std::move(p); }
	bool hasAttributeModifierProvider() const { return (bool)m_attributeModifiers; }
	// RW 0x68C82D with the out value starting at 1.0 (RW 0x805007)
	float attributeModifierProduct(const Object &obj, int type) const;
	// RW 0x68C818 style: the sum form for COMMAND_POINT_BONUS (RW 0x6A7C01 starts it at 0.0)
	typedef std::function<bool(const Object &obj, int type, float &sum)> AttributeModifierSumProvider;
	void setAttributeModifierSumProvider(AttributeModifierSumProvider p) { m_attributeSums = std::move(p); }
	float attributeModifierSum(const Object &obj, int type) const;

	// whether the object has a model condition (by the binary's name, RW 0xD9FAD8): AutoDepositUpdate asks RUBBLE, POST_RUBBLE, POST_COLLAPSE, GARRISONED.
	// Object has no model conditions in this tree (PROD-1 adds them); without a provider none is set
	typedef std::function<bool(const Object &obj, const char *modelCondition)> ModelConditionProvider;
	void setModelConditionProvider(ModelConditionProvider p) { m_modelConditions = std::move(p); }
	bool objectHasModelCondition(const Object &obj, const char *modelCondition) const;

	typedef std::function<void(Object &obj, float points)> ExperienceHook;
	void setExperienceHook(ExperienceHook hook) { m_experience = std::move(hook); }
	// RW 0x79D833 on the object's experience tracker when it has one and it is trainable; without a hook the points are counted and dropped (S-256)
	void grantBuildingExperience(Object &obj, float points);
	unsigned long long droppedExperienceGrants() const { return m_droppedExperience; }

	// ---- income events (diagnostics for the client: floating text, the overlay) -------------------------------------------------
	struct IncomeEvent
	{
		ObjectID object = INVALID_ID;
		int playerIndex = 0;
		int amount = 0;
		UnsignedInt frame = 0;
	};
	typedef std::function<void(const IncomeEvent &)> IncomeListener;
	void setIncomeListener(IncomeListener l) { m_incomeListener = std::move(l); }
	void noteIncome(Object &obj, Player &owner, int amount);
	const std::deque<IncomeEvent> &incomeLog() const { return m_incomeLog; }
	void clearIncomeLog() { m_incomeLog.clear(); }

	// ---- state hash and reports -----------------------------------------------------------------------------------------------
	void crc(StateHasher &hasher) const;
	// the stop lines that apply to a run (EconomyStops.h) plus the run-time counts of what was dropped
	std::vector<std::string> report() const;

private:
	GameLogic &m_logic;
	EconomySettings m_settings;
	EconomyContext m_context;
	TerrainResourceManager m_resources;
	AttributeModifierProvider m_attributeModifiers;
	AttributeModifierSumProvider m_attributeSums;
	ModelConditionProvider m_modelConditions;
	ExperienceHook m_experience;
	HeroBountyScale m_heroBountyScale;
	unsigned long long m_unscaledHeroBounties = 0;
	IncomeListener m_incomeListener;
	std::deque<IncomeEvent> m_incomeLog;
	unsigned long long m_droppedExperience = 0;
	unsigned long long m_incomePayments = 0;
};
