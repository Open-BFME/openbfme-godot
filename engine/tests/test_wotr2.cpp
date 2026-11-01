// OpenBFME unit tests. GPL-3.0.
// Lane WOTR-2: retail's War of the Ring rules: the AutoResolve data (the blocks' parsers and lookups), the STLport ordering helpers, the autoresolve
// battle (rounds, targets, misses, reinforcements, leadership, results, determinism) and the retail data.

#include "doctest.h"

#include "StartTestUtil.h"

#include "Common/INI.h"
#include "Common/INIException.h"
#include "Common/RandomValue.h"
#include "GameLogic/LivingWorld/LivingWorldAutoResolveBattle.h"
#include "GameLogic/LivingWorld/LivingWorldData.h"
#include "GameLogic/LivingWorld/LivingWorldLogic.h"
#include "GameLogic/LivingWorld/LivingWorldUnits.h"
#include "GameLogic/LivingWorld/StlportAlgorithm.h"
#include "GameEngineDevice/W3DDevice/GameClient/W3DObjectLighting.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace LivingWorld;

namespace
{
void parseInto(AutoResolveStores &stores, const std::string &text)
{
	INIEnvironment env;
	stores.registerBlocks(env.blocks);
	INI ini(env);
	std::vector<std::uint8_t> bytes(text.begin(), text.end());
	ini.loadMemory("wotr2-test.ini", bytes, INI_LOAD_OVERWRITE);
}

std::string parseError(const std::string &text)
{
	AutoResolveStores s;
	try
	{
		parseInto(s, text);
	}
	catch (const INIException &e)
	{
		return e.message();
	}
	return std::string();
}

const char *kData = R"(
AutoResolveWeapon AutoResolve_DefaultWeapon
	DamagePerRound = Damage:50 Against:AutoResolveUnit_Soldier
	DamagePerRound = Damage:50 Against:AutoResolveUnit_Archer
	DamagePerRound = Damage:50 Against:AutoResolveUnit_Hero
	ReduceAttackWhenHurt = Yes
	MissPercentChance = 50
End
AutoResolveWeapon SureShot
	DamagePerRound = Damage:100 Against:AutoResolveUnit_Soldier
	DamagePerRound = Damage:100 Against:AutoResolveUnit_Archer
	LevelBonus = Level:2 Bonus:10%
	ReduceAttackWhenHurt = No
	MissPercentChance = 0
End
AutoResolveWeapon Never
	MissPercentChance = 100
End
AutoResolveArmor AutoResolve_DefaultArmor
	Armor = AutoResolveUnit_Archer 100%
End
AutoResolveArmor Heavy
	Armor = AutoResolveUnit_Soldier 50%
END
AutoResolveArmor Heavy
	Armor = AutoResolveUnit_Soldier 10%
END
AutoResolveBody AutoResolve_DefaultBody
	CanBeAttacked = Yes
End
AutoResolveBody Grunt
	HitpointsAtLevel = Hitpoints:300 Level:1
	HitpointsAtLevel = Hitpoints:450 Level:2
	RegenerateAtLevel = Regenerate:10 Level:1
End
AutoResolveBody Hero
	HitpointsAtLevel = Hitpoints:1000 Level:1
	LeaveInArmySummary = Yes
End
AutoResolveCombatChain AutoResolve_DefaultCombatChain
End
AutoResolveCombatChain Archers
	Target = Target:AutoResolveUnit_Soldier Priority:50
	Target = Target:AutoResolveUnit_Hero Priority:25
	Target = Target:AutoResolveUnit_Cavalry Priority:-50
	Target = Target:AutoResolveUnit_Archer Priority:0
End
AutoResolveLeadership Captain
	Affects = AutoResolveUnit_Soldier AutoResolveUnit_Archer
	AffectsHigherLevelFirst = Yes
	BonusForLevel
		MinLevel = 1
		WeaponMultiplier = 200%
		MaximumUnitsAffected = 1
	End
	BonusForLevel
		MinLevel = 3
		WeaponMultiplier = 300%
	End
End
AutoResolveHandicapLevel
	GUIDisplayedLevel = 0
End
AutoResolveHandicapLevel
	GUIDisplayedLevel = 10
	WeaponMultiplier = 0.9
	ArmorMultiplier = #DIVIDE( 1.0, 0.9 )
End
AutoResolveReinforcementSchedule Defender
	1 = 0
	2 = 3
	EachRemaining = +2
End
AutoResolveReinforcementSchedule Attacker
	1 = 0
	2 = 5
	EachRemaining = 1
End
LivingWorldAutoResolveResourceBonus
	Sides = PlayerGood PlayerEvil
	Bonus
		MinResourceBonus = 0
		WeaponMultiplier = 1.0
	End
	Bonus
		MinResourceBonus = 100
		WeaponMultiplier = 1.5
		ArmorMultiplier = 0.5
	End
End
LivingWorldAutoResolveSciencePurchasePointBonus
	Sides = PlayerGood
	Bonus
		MinSciencePurchasePointsForBonus = 0
	End
End
)";

const AutoResolveStores &stores()
{
	static AutoResolveStores s;
	static bool loaded = false;
	if (!loaded)
	{
		parseInto(s, kData);
		loaded = true;
	}
	return s;
}

UnitInfo unit(int type, const std::string &body, const std::string &weapon = std::string(), const std::string &armor = std::string())
{
	UnitInfo u;
	u.known = true;
	u.autoResolveUnitType = type;
	u.autoResolveBody = body;
	if (!weapon.empty())
	{
		u.autoResolveWeapon.push_back(UnitInfo::AutoResolveSet{ weapon, {}, {} });
	}
	if (!armor.empty())
	{
		u.autoResolveArmor.push_back(UnitInfo::AutoResolveSet{ armor, {}, {} });
	}
	u.levels = { { 1, 1, 10 }, { 2, 100, 20 }, { 3, 1000, 40 } };
	return u;
}

UnitInfo testUnits(const std::string &name)
{
	if (name == "Swordsman")
	{
		return unit(AR_SOLDIER, "Grunt", "SureShot");
	}
	if (name == "Bowman")
	{
		UnitInfo u = unit(AR_ARCHER, "Grunt");
		u.autoResolveCombatChain = "Archers";
		return u;
	}
	if (name == "Captain")
	{
		UnitInfo u = unit(AR_HERO, "Hero", "SureShot");
		u.autoResolveLeadership = "Captain";
		return u;
	}
	if (name == "Statue")
	{
		return unit(AR_SOLDIER, "Grunt", "Never");
	}
	return UnitInfo();
}

AutoResolveUnitSpec spec(const std::string &t, int army, float xp = 0.0f)
{
	AutoResolveUnitSpec s;
	s.thingTemplate = t;
	s.armyId = army;
	s.experience = xp;
	return s;
}

AutoResolvePlayerSpec player(int p, int side, std::vector<AutoResolveArmySpec> armies)
{
	AutoResolvePlayerSpec s;
	s.player = p;
	s.side = side;
	s.armies = std::move(armies);
	return s;
}
} // namespace

TEST_CASE("wotr2 data: the AutoResolve blocks parse with retail's defaults, copies and lookups")
{
	const AutoResolveStores &s = stores();
	REQUIRE(s.weapons.size() == 3);
	const AutoResolveWeapon *sure = s.findWeapon("SureShot");
	REQUIRE(sure);
	CHECK(sure->damage[AR_SOLDIER] == 100.0f);
	CHECK(sure->damage[AR_HERO] == 50.0f); // copied from the default weapon
	CHECK(sure->missPercentChance == 0);
	CHECK_FALSE(sure->reduceAttackWhenHurt);
	CHECK(sure->canDamage());
	CHECK_FALSE(s.findWeapon("Never")->canDamage()); // MissPercentChance 100
	CHECK(sure->damageForRound(1, 10.0f, 300.0f, AR_SOLDIER) == 100.0f);
	CHECK(sure->damageForRound(2, 10.0f, 300.0f, AR_SOLDIER) == doctest::Approx(110.0f)); // (10% + 1) * 100
	CHECK(sure->damageForRound(5, 10.0f, 300.0f, AR_CAVALRY) == 0.0f);
	const AutoResolveWeapon &def = s.defaultWeapon();
	CHECK(def.damageForRound(1, 150.0f, 300.0f, AR_SOLDIER) == 25.0f); // ReduceAttackWhenHurt: (hp / max) * damage
	CHECK(s.findArmor("Heavy")->scalar[AR_SOLDIER] == 0.5f); // the second definition is dropped
	CHECK(s.findArmor("Heavy")->scalar[AR_ARCHER] == 1.0f);
	const AutoResolveBody *grunt = s.findBody("Grunt");
	CHECK(grunt->canBeAttacked);
	CHECK(grunt->hitpointsForLevel(0) == 1.0f);
	CHECK(grunt->hitpointsForLevel(1) == 300.0f);
	CHECK(grunt->hitpointsForLevel(7) == 450.0f);
	CHECK(grunt->regenForLevel(0) == 0.0f);
	CHECK(grunt->regenForLevel(4) == 10.0f);
	CHECK(s.findBody("Hero")->leaveInArmySummary);
	const AutoResolveLeadership *cap = s.findLeadership("Captain");
	REQUIRE(cap);
	CHECK(cap->affects == ((1u << AR_SOLDIER) | (1u << AR_ARCHER)));
	CHECK(cap->bonusForLevel(0) == nullptr);
	CHECK(cap->bonusForLevel(2)->weapon == 2.0f);
	CHECK(cap->bonusForLevel(2)->priority == 10);                // RW 0x828389 defaults
	CHECK(cap->bonusForLevel(9)->maximumUnitsAffected == 1000000);
	CHECK(s.handicapFor(0).weapon == 1.0f);
	CHECK(s.handicapFor(5).weapon == doctest::Approx(0.9f)); // lower_bound
	CHECK(s.handicapFor(50).armor == doctest::Approx(1.0f / 0.9f)); // past the end: the last level
	CHECK(s.defenderSchedule.roundForArmyNumber(0) == 0);
	CHECK(s.defenderSchedule.roundForArmyNumber(1) == 3);
	CHECK(s.defenderSchedule.roundForArmyNumber(4) == 3 + 2 * 3);
	CHECK(s.attackerSchedule.roundForArmyNumber(2) == 6);
	const AutoResolveBonusSchedule *rb = s.resourceScheduleFor("PlayerEvil");
	REQUIRE(rb);
	CHECK(rb->bonusFor(99)->weapon == 1.0f);
	CHECK(rb->bonusFor(100)->armor == 0.5f);
	CHECK(rb->bonusFor(-1) == nullptr);
	CHECK(s.scienceScheduleFor("PlayerEvil") == nullptr);
	CHECK(s.scienceScheduleFor("PlayerGood")->bonusFor(0) != nullptr);
}

TEST_CASE("wotr2 data: retail's refusals")
{
	CHECK(parseError("AutoResolveWeapon W\nEnd\nAutoResolveWeapon W\nEnd\n").rfind("Duplicate AutoResolveWeapon entries", 0) == 0);
	CHECK(parseError("AutoResolveBody B\n HitpointsAtLevel = Hitpoints:1 Level:1\n HitpointsAtLevel = Hitpoints:2 Level:1\nEnd\n").rfind("Duplicate hitpoint entries", 0) == 0);
	CHECK(parseError("AutoResolveCombatChain C\n Target = Target:AutoResolveUnit_Hero Priority:5\n Target = Target:AutoResolveUnit_Hero Priority:6\nEnd\n")
			  .rfind("Cannot redefine attack priority", 0) == 0);
	CHECK(parseError("AutoResolveReinforcementSchedule Middle\nEnd\n").rfind("Unknown AutoResolveReinforcementSchedule", 0) == 0);
	CHECK(parseError("AutoResolveReinforcementSchedule Attacker\n 0 = 4\nEnd\n").rfind("Unacceptable army index", 0) == 0);
	CHECK(parseError("AutoResolveReinforcementSchedule Attacker\n 1 = 4\n 1 = 5\nEnd\n").rfind("AutoResolveReinforcementSchedule has duplicate", 0) == 0);
	CHECK(parseError("AutoResolveLeadership L\n BonusForLevel\n  MinLevel = 2\n End\n BonusForLevel\n  MinLevel = 2\n End\nEnd\n").rfind("Two BonusForLevel", 0) == 0);
	CHECK(parseError("AutoResolveArmor A\n Armor = AutoResolveUnit_Dragon 50%\nEnd\n") != "");
}

TEST_CASE("wotr2 data: the combat chain's target order (highest priority, then down; the default types between 0 and the negatives)")
{
	const AutoResolveCombatChain *c = stores().findCombatChain("Archers");
	REQUIRE(c);
	CHECK((c->defaultTargets & (1u << AR_SOLDIER)) == 0);
	CHECK((c->defaultTargets & (1u << AR_ARCHER)) != 0); // priority 0: stays a default target
	AutoResolveCombatChain::Target t;
	REQUIRE(c->highestPriorityTarget(&t));
	CHECK(t.type == AR_SOLDIER);
	std::vector<int> order = { t.type };
	while (c->nextTarget(t, &t))
	{
		order.push_back(t.type);
	}
	// 50 Soldier, 25 Hero, then the default types in index order (Archer, Pikemen, Monster, Fortress, Siege, Support, INVALID), then -50 Cavalry
	const std::vector<int> expected = { AR_SOLDIER, AR_HERO, AR_ARCHER, AR_PIKEMEN, AR_MONSTER, AR_FORTRESS, AR_SIEGE, AR_SUPPORT, AR_INVALID, AR_CAVALRY };
	CHECK(order == expected);
	const AutoResolveCombatChain &d = stores().defaultCombatChain();
	REQUIRE(d.highestPriorityTarget(&t));
	CHECK(t.type == AR_SOLDIER); // no entries: the first default type
}

TEST_CASE("wotr2 stlport: the SGI sort and heap give sorted output and valid heaps")
{
	std::vector<int> v;
	std::uint32_t x = 12345;
	for (int i = 0; i < 200; ++i)
	{
		x = x * 1103515245u + 12345u;
		v.push_back((int)(x >> 24) % 17);
	}
	std::vector<int> a = v;
	Stlport::sort(a, [](int p, int q) { return p > q; });
	CHECK(std::is_sorted(a.begin(), a.end(), [](int p, int q) { return p > q; }));
	std::vector<int> h;
	for (int e : v)
	{
		h.push_back(e);
		Stlport::pushHeap(h, [](int p, int q) { return p > q; });
	}
	std::vector<int> popped;
	while (!h.empty())
	{
		popped.push_back(h.front());
		Stlport::popHeap(h, [](int p, int q) { return p > q; });
		h.pop_back();
	}
	CHECK(std::is_sorted(popped.begin(), popped.end())); // a min-heap ("greater") pops in ascending order
	// a small input stays with the insertion sort (stable for <= 16 elements)
	std::vector<std::pair<int, int>> small = { { 1, 0 }, { 2, 1 }, { 1, 2 }, { 2, 3 }, { 1, 4 } };
	Stlport::sort(small, [](const std::pair<int, int> &p, const std::pair<int, int> &q) { return p.first > q.first; });
	CHECK(small == std::vector<std::pair<int, int>>{ { 2, 1 }, { 2, 3 }, { 1, 0 }, { 1, 2 }, { 1, 4 } });
}

TEST_CASE("wotr2 battle: two sure-shot swordsmen kill each other in the same round; side 0 wins the tie")
{
	GameLogicRandom rng(RandomAlgorithm::ZH_CarryChain);
	rng.initGameLogicRandom(1, -1);
	UnitInfoSource units = testUnits;
	std::vector<AutoResolvePlayerSpec> players;
	players.push_back(player(0, 0, { AutoResolveArmySpec{ 1, false, { spec("Swordsman", 1) } } }));
	players.push_back(player(1, 1, { AutoResolveArmySpec{ 2, false, { spec("Swordsman", 2) } } }));
	AutoResolveBattle b(stores(), units, players, rng);
	CHECK(b.winner() == -1);
	b.update();
	CHECK(b.round() == 1);
	b.runToEnd();
	// 300 hp at level 1, 100 damage per round, regenerate 10 after each round: 300 -> 210 -> 120 -> 30 -> dead in round 4
	CHECK(b.round() == 4);
	CHECK(b.winner() == 0);
	CHECK(b.survivors().empty());
	CHECK(b.killCount(0) == 1);
	CHECK(b.killCount(1) == 1);
	CHECK(b.randomDraws() == 8); // every attack rolls (MissPercentChance 0 still draws)
	CHECK(b.enteredArmies() == std::vector<int>{ 1, 2 });
}

// lane WOTR-2: retail rank.ini's first ranks with their campaign counts (SkillPointsNeededCampaign: 0, 100, then PLAYER_SKILL_POINTS_DELTA_CAMPAIGN
// 100 * (n - 1); SciencePurchasePointsGranted 5, then PLAYER_PURCHASE_POINTS_GRANTED 1)
static std::vector<SciencePurchasePointTracker::RankRow> retailRanks()
{
	return { { true, 5u, 0 }, { true, 1u, 100 }, { true, 1u, 200 }, { true, 1u, 300 } };
}

TEST_CASE("wotr2 tracker: the base SciencePurchasePointTracker (RW 0x782A0C, 0x782774, 0x782865, 0x782764, 0x782AA4, 0x7827C8)")
{
	const std::vector<SciencePurchasePointTracker::RankRow> ranks = retailRanks();
	SciencePurchasePointTracker t(ranks);
	CHECK(t.getRankLevel() == 1);
	CHECK(t.getSciencePurchasePoints() == 5); // the reset in the constructor: rank 1's grant
	CHECK(t.getSkillPoints() == 0.0f);
	CHECK(t.getMaxRankLevel() == 4);          // no template: the rank count
	CHECK(t.addSkillPoints(250.0f, false));   // ranks 2 and 3 (100, 200)
	CHECK(t.getRankLevel() == 3);
	CHECK(t.getSciencePurchasePoints() == 7);
	CHECK(t.getSkillPoints() == 250.0f);
	CHECK_FALSE(t.addSkillPoints(0.0f, true));
	t.addSciencePurchasePoints(-100);          // RW 0x782764: 0 when negative
	CHECK(t.getSciencePurchasePoints() == 0);
	t.setTemplate(201, 0);                     // retail FactionMen: MaxLevelSP = #DIVIDE(201 1)
	CHECK(t.getMaxRankLevel() == 4);
	CHECK(t.addSkillPoints(1000.0f, false));   // clamped to the max rank's count
	CHECK(t.getSkillPoints() == 300.0f);
	CHECK(t.getRankLevel() == 4);
	CHECK(t.getSciencePurchasePoints() == 1);
	// a template cap of 2: the skill points stop at rank 2's count; the reset ran before the template, so IntrinsicSciencePurchasePoints is not added
	SciencePurchasePointTracker capped(ranks);
	capped.setTemplate(2, 50);
	CHECK(capped.addSkillPoints(250.0f, false));
	CHECK(capped.getSkillPoints() == 100.0f);
	CHECK(capped.getRankLevel() == 2);
	CHECK(capped.getSciencePurchasePoints() == 6);
	// setRankLevel raises the skill points to the rank's count (RW 0x7828F6 .. 0x782915)
	SciencePurchasePointTracker raised(ranks);
	CHECK(raised.setRankLevel(3));
	CHECK(raised.getSkillPoints() == 200.0f);
	CHECK(raised.getSciencePurchasePoints() == 7);
	CHECK_FALSE(raised.setRankLevel(3));
	// the max rank without a Rank block: RW 0x782B47 would read a null entry
	const std::vector<SciencePurchasePointTracker::RankRow> gap = { { true, 5u, 0 }, { false, 0u, 0 } };
	SciencePurchasePointTracker broken(gap);
	CHECK_THROWS(broken.addSkillPoints(1.0f, false));
}

TEST_CASE("wotr2 battle: a record's tracker picks its science bonus at the purchase points, kills add skill points, the bonus is read again after the round")
{
	// ranks: rank 2 at 10 skill points, so one kill of a level-1 swordsman (ExperienceAward 10, record experience 1.0) is a rank
	const std::vector<SciencePurchasePointTracker::RankRow> ranks = { { true, 5u, 0 }, { true, 1u, 10 }, { true, 1u, 1000 } };
	AutoResolveBonusSchedule sched;
	sched.bonuses[0] = AutoResolveBonusSchedule::Bonus{ 0, 1.0f, 1.0f };
	sched.bonuses[6] = AutoResolveBonusSchedule::Bonus{ 6, 0.0f, 1.0f }; // from 6 purchase points the record deals no damage
	UnitInfoSource units = testUnits;
	{
		GameLogicRandom rng(RandomAlgorithm::ZH_CarryChain);
		rng.initGameLogicRandom(1, -1);
		std::vector<AutoResolvePlayerSpec> players;
		players.push_back(player(0, 0, { AutoResolveArmySpec{ 1, false, { spec("Swordsman", 1), spec("Swordsman", 1) } } }));
		players.push_back(player(1, 1, { AutoResolveArmySpec{ 2, false, { spec("Swordsman", 2), spec("Swordsman", 2) } } }));
		players[0].scienceSchedule = &sched;
		players[0].rankTracker.emplace(ranks); // 5 purchase points
		players[1].scienceSchedule = &sched;   // no tracker: no bonus (RW 0x8FB695 needs both)
		AutoResolveBattle b(stores(), units, players, rng);
		REQUIRE(b.scienceBonus(0));
		CHECK(b.scienceBonus(0)->weapon == 1.0f);
		CHECK(b.recordTracker(1) == nullptr);
		CHECK(b.scienceBonus(1) == nullptr);
		b.runToEnd();
		CHECK(b.round() == 4); // the same fight as two 1v1s: every unit dies in round 4
		CHECK(b.killCount(0) == 2);
		REQUIRE(b.recordTracker(0));
		CHECK(b.recordTracker(0)->getSkillPoints() == 20.0f); // two kills of 10
		CHECK(b.recordTracker(0)->getRankLevel() == 2);
		CHECK(b.recordTracker(0)->getSciencePurchasePoints() == 6);
		REQUIRE(b.scienceBonus(0));
		CHECK(b.scienceBonus(0)->weapon == 0.0f); // read again at 6 after the round (RW 0x8FEAB5)
		CHECK(players[0].rankTracker->getSkillPoints() == 0.0f); // the battle works on its copy (RW 0x8FAD90)
	}
	{
		// 6 purchase points from the start: the bonus from the constructor's read (RW 0x8FF1C2) takes the record's damage away
		GameLogicRandom rng(RandomAlgorithm::ZH_CarryChain);
		rng.initGameLogicRandom(1, -1);
		std::vector<AutoResolvePlayerSpec> players;
		players.push_back(player(0, 0, { AutoResolveArmySpec{ 1, false, { spec("Swordsman", 1) } } }));
		players.push_back(player(1, 1, { AutoResolveArmySpec{ 2, false, { spec("Swordsman", 2) } } }));
		players[0].scienceSchedule = &sched;
		players[0].rankTracker.emplace(ranks);
		players[0].rankTracker->addSciencePurchasePoints(1);
		AutoResolveBattle b(stores(), units, players, rng);
		b.runToEnd();
		CHECK(b.winner() == 1);
		CHECK(b.killCount(0) == 0);
		CHECK(b.killCount(1) == 1);
	}
}

TEST_CASE("wotr2 battle: a unit whose weapon cannot damage never attacks; the reinforcement army waits for its round")
{
	GameLogicRandom rng(RandomAlgorithm::ZH_CarryChain);
	rng.initGameLogicRandom(1, -1);
	UnitInfoSource units = testUnits;
	std::vector<AutoResolvePlayerSpec> players;
	// side 0: a statue now, a swordsman army at round 3 (the defender schedule's second army); side 1: one swordsman
	players.push_back(player(0, 0, { AutoResolveArmySpec{ 1, false, { spec("Statue", 1) } }, AutoResolveArmySpec{ 3, false, { spec("Swordsman", 3) } } }));
	players.push_back(player(1, 1, { AutoResolveArmySpec{ 2, false, { spec("Swordsman", 2) } } }));
	AutoResolveBattle b(stores(), units, players, rng);
	b.update();
	b.update();
	CHECK(b.winner() == -1);
	CHECK(b.enteredArmies() == std::vector<int>{ 1, 2 }); // army 3 joins at round 3
	b.update();
	CHECK(b.enteredArmies() == std::vector<int>{ 1, 3, 2 });
	b.runToEnd();
	CHECK(b.winner() == 0);
}

TEST_CASE("wotr2 battle: a hero with LeaveInArmySummary stays with its army; experience levels a killer up; the same seed repeats the battle")
{
	auto run = [](std::uint32_t seed, std::vector<AutoResolveBattle::Survivor> *out, int *rounds, int *draws) {
		GameLogicRandom rng(RandomAlgorithm::ZH_CarryChain);
		rng.initGameLogicRandom(seed, -1);
		UnitInfoSource units = testUnits;
		std::vector<AutoResolvePlayerSpec> players;
		players.push_back(player(0, 0, { AutoResolveArmySpec{ 1, false, { spec("Captain", 1), spec("Bowman", 1), spec("Bowman", 1, 150.0f) } } }));
		players.push_back(player(1, 1, { AutoResolveArmySpec{ 2, false, { spec("Swordsman", 2), spec("Swordsman", 2), spec("Swordsman", 2), spec("Swordsman", 2) } } }));
		AutoResolveBattle b(stores(), units, players, rng);
		b.runToEnd();
		*out = b.survivors();
		*rounds = b.round();
		*draws = b.randomDraws();
		return b.winner();
	};
	std::vector<AutoResolveBattle::Survivor> s1, s2;
	int r1 = 0, r2 = 0, d1 = 0, d2 = 0;
	const int w1 = run(77, &s1, &r1, &d1);
	const int w2 = run(77, &s2, &r2, &d2);
	CHECK(w1 == w2);
	CHECK(r1 == r2);
	CHECK(d1 == d2);
	REQUIRE(s1.size() == s2.size());
	for (size_t i = 0; i < s1.size(); ++i)
	{
		CHECK(s1[i].spec.thingTemplate == s2[i].spec.thingTemplate);
		CHECK(s1[i].hp == s2[i].hp);
		CHECK(s1[i].spec.experience == s2[i].spec.experience);
	}
	CHECK(d1 > 0);
	if (w1 == 1)
	{
		// the hero lost every hit point but is still a survivor (LeaveInArmySummary)
		bool hero = false;
		for (const AutoResolveBattle::Survivor &s : s1)
		{
			hero = hero || s.spec.thingTemplate == "Captain";
		}
		CHECK(hero);
	}
	// some unit gained experience from its kills (the level-1 award 10 each)
	bool gained = false;
	for (const AutoResolveBattle::Survivor &s : s1)
	{
		gained = gained || (s.spec.thingTemplate != "Captain" && s.spec.experience > 150.0f) || (s.spec.experience > 1.0f && s.spec.experience < 150.0f);
	}
	CHECK(gained);
}

// ---- retail ----

TEST_CASE("wotr2 retail: the AutoResolve stores load from the retail INI and a battle between two real armies is deterministic")
{
	OPENBFME_REQUIRE_START(shared);
	for (const SubsystemLoadReport::FileError &e : shared->world->report().errors)
	{
		const bool autoResolve = e.file.find("autoresolve") != std::string::npos || e.file.find("AutoResolve") != std::string::npos;
		CHECK_MESSAGE(!autoResolve, (e.file + ": " + e.message));
	}
	const AutoResolveStores &s = shared->world->livingWorld().autoResolve;
	CHECK(s.weapons.size() == 150);
	CHECK(s.armors.size() >= 120);
	CHECK(s.bodies.size() == 102);
	CHECK(s.combatChains.size() == 9);
	CHECK(s.leaderships.size() == 13);
	CHECK(s.handicaps.size() >= 10);
	CHECK(s.defenderSchedule.roundForArmyNumber(1) == 15);
	CHECK(s.attackerSchedule.roundForArmyNumber(5) == 47); // the last key 3 is round 45, + EachRemaining 1 per army after it
	CHECK(s.resourceBonuses.size() == 1);
	CHECK(s.defaultWeapon().missPercentChance == 50);
	const UnitInfoSource units = makeUnitInfoSource(shared->world->things(), &shared->world->experienceLevels());
	const UnitInfo fighter = units("GondorFighterHorde");
	REQUIRE(fighter.known);
	CHECK(fighter.autoResolveUnitType == AR_SOLDIER);
	CHECK_FALSE(fighter.autoResolveWeapon.empty());
	CHECK_FALSE(fighter.levels.empty());
	CHECK(fighter.horde);
	const UnitInfo orcs = units("MordorFighterHorde");
	REQUIRE(orcs.known);
	auto run = [&](std::vector<AutoResolveBattle::Survivor> *out) {
		GameLogicRandom rng(RandomAlgorithm::ZH_CarryChain);
		rng.initGameLogicRandom(2024, -1);
		std::vector<AutoResolvePlayerSpec> players;
		AutoResolveArmySpec good{ 1, false, {} };
		for (int i = 0; i < 4; ++i)
		{
			good.units.push_back(spec("GondorFighterHorde", 1));
		}
		good.units.push_back(spec("GondorArcherHorde", 1));
		AutoResolveArmySpec evil{ 2, false, {} };
		for (int i = 0; i < 6; ++i)
		{
			evil.units.push_back(spec("MordorFighterHorde", 2));
		}
		players.push_back(player(0, 0, { good }));
		players.push_back(player(1, 1, { evil }));
		AutoResolveBattle b(s, units, players, rng);
		b.runToEnd();
		*out = b.survivors();
		MESSAGE("retail battle: winner side " << b.winner() << " after " << b.round() << " rounds, " << b.randomDraws() << " rolls, survivors " << out->size());
		CHECK(b.droppedUnits().empty());
		return std::make_pair(b.winner(), b.round());
	};
	std::vector<AutoResolveBattle::Survivor> s1, s2;
	const auto a = run(&s1);
	const auto b = run(&s2);
	CHECK(a == b);
	CHECK(a.first >= 0);
	REQUIRE(s1.size() == s2.size());
	for (size_t i = 0; i < s1.size(); ++i)
	{
		CHECK(s1[i].hp == s2[i].hp);
	}
}

TEST_CASE("wotr2 retail: the scenarios' victory types (the Rules tab's LivingWorldVictoryType blocks) and a two-player start")
{
	OPENBFME_REQUIRE_START(shared);
	const LivingWorldStores &s = shared->world->livingWorld();
	const Campaign *c = s.findCampaign("WOTRScenario001");
	REQUIRE(c);
	REQUIRE(c->scenario);
	CHECK(c->scenario->useMpRulesVictoryCondition);
	REQUIRE(c->victoryTypes.size() >= 2);
	bool strongholds = false, domination = false;
	for (const VictoryType &vt : c->victoryTypes)
	{
		for (const Condition &cond : vt.teamVictory)
		{
			strongholds = strongholds || !cond.controlledRegions.empty();
			domination = domination || cond.numControlledRegionsGreaterOrEqualTo >= 20;
		}
	}
	CHECK(strongholds);
	CHECK(domination);
	LivingWorldLogic lw(s, makeUnitInfoSource(shared->world->things(), &shared->world->experienceLevels()));
	GameSetup setup;
	setup.campaign = "WOTRScenario001";
	setup.seed = 5;
	setup.victoryType = 1;
	SetupSlot a;
	a.name = "A";
	a.faction = "FactionMen";
	a.human = true;
	a.team = 1;
	SetupSlot b;
	b.name = "B";
	b.faction = "FactionMordor";
	b.team = 2;
	setup.slots = { a, b };
	std::string err;
	REQUIRE_MESSAGE(lw.start(setup, &err), err);
	CHECK_FALSE(lw.finished());
}

TEST_CASE("wotr2: the strategic map's light set from LivingWorldMapInfo (retail livingworld.ini)")
{
	// RotWK livingworld.ini LivingWorldMapInfo: Ambient R:0 G:0 B:0, SunDir X:45 Y:45, SunRGB R:115 G:95 B:88, Accent1Dir X:19 Y:140,
	// Accent1RGB R:99 G:99 B:99, Accent2Dir X:27 Y:300, Accent2RGB R:44 G:34 B:51 (parseRGBColor: / 255)
	const float ambient[3] = { 0, 0, 0 };
	const float rgb[3][3] = { { 115 / 255.0f, 95 / 255.0f, 88 / 255.0f }, { 99 / 255.0f, 99 / 255.0f, 99 / 255.0f }, { 44 / 255.0f, 34 / 255.0f, 51 / 255.0f } };
	const float dirs[3][2] = { { 45, 45 }, { 19, 140 }, { 27, 300 } };
	W3DObjectLighting env;
	W3DObjectLightingUtil::fromLivingWorldMap(ambient, rgb, dirs, env);
	CHECK(env.colorScale == 2.0f);
	REQUIRE(env.objects.count == 3);
	CHECK(env.objects.ambient[0] == 0.0f);
	CHECK(env.objects.color[0][0] == doctest::Approx(230 / 255.0f)); // tolerance: float division; SunRGB red doubled
	CHECK(env.objects.color[2][2] == doctest::Approx(102 / 255.0f)); // tolerance: float division; Accent2RGB blue doubled
	// the sun: elevation 45, heading 45 -> (1/2, 1/2, sqrt(2)/2), toward the light, SAGE Z up
	CHECK(env.objects.toLight[0][0] == doctest::Approx(0.5f)); // tolerance: float trigonometry
	CHECK(env.objects.toLight[0][1] == doctest::Approx(0.5f)); // tolerance: float trigonometry
	CHECK(env.objects.toLight[0][2] == doctest::Approx(0.70710678f)); // tolerance: float trigonometry
	// Accent2: elevation 27 above the horizon, heading 300
	CHECK(env.objects.toLight[2][2] == doctest::Approx(0.45399f)); // tolerance: float trigonometry, sin 27 degrees to 5 places
	CHECK(env.objects.toLight[2][1] < 0.0f);
	// the infantry set is the same light set
	CHECK(env.infantry.count == 3);
	CHECK(env.infantry.color[1][1] == env.objects.color[1][1]);
	// a black light is dropped (ZH LightEnvironmentClass::Add_Light, below 0.05 in every channel)
	const float dark[3][3] = { { 0.5f, 0.5f, 0.5f }, { 0.04f, 0.04f, 0.04f }, { 0.3f, 0.3f, 0.3f } };
	W3DObjectLightingUtil::fromLivingWorldMap(ambient, dark, dirs, env);
	CHECK(env.objects.count == 2);
	CHECK(env.objects.color[1][0] == doctest::Approx(0.6f)); // tolerance: float multiply
}
