// File names: the HUD tests and these share ONE retail world (hudtest::shared()); doctest runs files in name order and a retail world is current for the process-wide stores until
// the next one loads, so the retail combat tests sit with the other test_hud_* files, after nothing that loads its own world in between.
// OpenBFME retail tests for combat (lane COMBAT-1). They run only when ROTWK_INSTALL and BFME2_INSTALL are set (otherwise SKIP): pure RotWK 2.01 + BFME2 1.06, the object
// world loaded once, hordes of the real templates fight each other on a flat arena.

#include "doctest.h"
#include "HudTestUtil.h"

#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Economy.h"
#include "GameLogic/EconomySettings.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BannerCarrierUpdate.h"
#include "GameLogic/Module/HordeContain.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/Object/RetailObjectWorld.h"

#include "PathfindTestUtil.h"
#include "RetailTestMount.h"

#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponNugget.h"
#include "GameLogic/AI/AIPathfind.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <cstdlib>
#include <memory>

namespace
{
using hudtest::SharedWorld;
using hudtest::shared;
// one retail world per process for the HUD and the combat tests: a second RetailObjectWorld would take the process-wide stores over from the HUD tests' world
bool haveWorld()
{
	return hudtest::haveWorld("combat retail");
}

// two sides (enemies) of the named factions on a flat arena, the retail economy and the AI of the install
struct Arena
{
	std::unique_ptr<RetailObjectWorld::ContextScope> context; // the stores (weapons, armour, locomotors) are read inside the world's context scope
	TeamFactory teams;
	PlayerList players;
	GameLogic logic;
	AIWorldConfig cfg;
	pathtest::SyntheticTerrain terrain;
	std::unique_ptr<AIWorld> ai;

	Arena(SharedWorld &s, const char *factionA, const char *factionB)
		: context(s.world->enterContext())
		, players(s.world->nameKeys(), s.world->playerTemplates(), teams)
		, logic(s.world->things(), s.world->modules(), players, RandomAlgorithm::ZH_CarryChain)
		, terrain(120, 120)
	{
		SkirmishSetup setup;
		setup.startingMoney = 0;
		setup.players.push_back({ "A", factionA, true, 0, 0, 1 });
		setup.players.push_back({ "B", factionB, false, 1, 0, 2 });
		const std::vector<std::string> errors = players.setupSkirmish(setup);
		REQUIRE_MESSAGE(errors.empty(), (errors.empty() ? "" : errors[0]));
		std::string err;
		REQUIRE_MESSAGE(GameLogicSettingsLoader::load(*s.mount->fs, logic.settings(), &err), err);
		REQUIRE_MESSAGE(EconomySettings::load(*s.mount->fs, logic.economy().settings(), &err), err);
		logic.random().seedRandom(7);
		logic.economy().initAllCommandPoints();
		REQUIRE_MESSAGE(AIWorldConfigLoader::load(*s.mount->fs, cfg, &err), err);
		ai = std::make_unique<AIWorld>(logic, cfg, s.world->iniMacros());
		ai->attach();
		ai->newMap(terrain);
	}
	~Arena()
	{
		logic.reset();
		ai.reset();
	}
	Player *player(int i) { return players.findPlayerWithName(i == 0 ? "A" : "B"); }
	Object *place(const char *templateName, int side, float x, float y)
	{
		const ThingTemplate *t = shared().world->things().findTemplate(templateName);
		REQUIRE_MESSAGE(t != nullptr, "retail template " << templateName);
		Object *o = logic.newObject(t, player(side)->getDefaultTeam(), ObjectStatusMaskType{});
		REQUIRE_MESSAGE(o != nullptr, templateName);
		Coord3D p{ x, y, 0.0f };
		o->setPosition(&p);
		o->setOrientation(side == 0 ? 0.0f : 3.14159265f);
		return o;
	}
	size_t members(Object *horde) { return horde->getContain() ? horde->getContain()->getContainCount() : 0; }
	// the pathfinder keeps no reservation of the object (any cell, any kind)
	bool inPathfinder(ObjectID id)
	{
		for (int x = 0; x < 120; ++x)
		{
			for (int y = 0; y < 120; ++y)
			{
				PathfindCell *c = ai->pathfinder().getCell(LAYER_GROUND, x, y);
				if (!c || !c->hasInfo())
				{
					continue;
				}
				for (int kind = 0; kind < (int)OCC_KIND_COUNT; ++kind)
				{
					for (const PathfindOccupant *oc = c->occupants((PathfindOccupantKind)kind); oc; oc = oc->next)
					{
						if ((ObjectID)oc->owner == id)
						{
							return true;
						}
					}
				}
			}
		}
		return false;
	}
};

// what one fight leaves behind: the per-frame state hashes and the numbers the assertions use
struct FightOutcome
{
	std::vector<std::uint32_t> hashes;
	int endFrame = -1;             // the frame the last member of the losing horde died
	unsigned long long hitsOnLoser = 0;   // damage applications while the loser lived (counters delta at the end of the fight)
	unsigned long long applications = 0;
	float winnerHealthStart = 0.0f, winnerHealthEnd = 0.0f;
	std::vector<float> winnerLost;       // per winner member: the health it lost by the end of the fight (a dead one lost all 200)
	size_t winnersKilled = 0;
	float loserHealthStart = 0.0f;
	size_t winnerMembersStart = 0, winnerMembersEnd = 0, loserMembersStart = 0;
	std::vector<int> deathFrames;        // per loser member
	std::vector<int> minShotGap;         // per member of either side: the smallest gap between two of its shots
	std::vector<float> loserHitSizes;    // every positive health step of a loser member in one frame
	unsigned long long kills = 0;
	std::uint32_t cashWinner = 0, cashLoser = 0;
	int usageLoserBefore = 0, usageLoserAfter = 0;
	bool loserInPathfinder = true;
	bool loserHordeGone = false, winnerHordeAlive = false;
	int firstHitFrame = -1;
};



// one melee horde of `factionA` ordered onto one melee horde of `factionB` (the second one is NOT ordered: it acquires the attacker on its own)
FightOutcome fightMelee(const char *factionA, const char *templateA, const char *factionB, const char *templateB, float bountyPercent)
{
	SharedWorld &s = shared();
	FightOutcome out;
	Arena a(s, factionA, factionB);
	a.player(1)->setBountyPercent(bountyPercent);
	a.player(0)->setBountyPercent(bountyPercent); // a skirmish pays no bounty at the default percent 0 (economy.md section 7): the script value is set
	Object *h0 = a.place(templateA, 0, 500.0f, 500.0f);
	Object *h1 = a.place(templateB, 1, 640.0f, 500.0f);
	const ObjectID id0 = h0->getID(), id1 = h1->getID();
	a.logic.runLogicFrame();
	a.logic.runLogicFrame();
	std::vector<ObjectID> m0, m1;
	for (const Object *m : *h0->getContain()->getContainedItemsList())
	{
		m0.push_back(m->getID());
	}
	for (const Object *m : *h1->getContain()->getContainedItemsList())
	{
		m1.push_back(m->getID());
	}
	auto healthOf = [&](const std::vector<ObjectID> &ids) {
		float h = 0.0f;
		for (ObjectID id : ids)
		{
			if (const Object *o = a.logic.findObjectByID(id))
			{
				h += o->isEffectivelyDead() ? 0.0f : o->getBodyModule()->getHealth();
			}
		}
		return h;
	};
	out.winnerHealthStart = healthOf(m0);
	out.loserHealthStart = healthOf(m1);
	out.winnerMembersStart = m0.size();
	out.loserMembersStart = m1.size();
	out.usageLoserBefore = a.player(1)->commandPoints().getUsage();
	std::vector<float> lastHealth;
	for (ObjectID id : m1)
	{
		lastHealth.push_back(a.logic.findObjectByID(id)->getBodyModule()->getHealth());
	}
	out.deathFrames.assign(m1.size(), -1);
	std::vector<unsigned> lastFire(m0.size() + m1.size(), 0u);
	out.minShotGap.assign(m0.size() + m1.size(), 1 << 30);
	h0->getAIUpdateInterface()->aiAttackObject(h1, CMD_FROM_PLAYER);
	out.hashes.push_back(a.logic.computeStateHash());
	const unsigned long long applicationsBefore = a.logic.combat().counters().damageApplications;
	for (int f = 0; f < 600; ++f)
	{
		a.logic.runLogicFrame();
		out.hashes.push_back(a.logic.computeStateHash());
		for (size_t k = 0; k < m1.size(); ++k)
		{
			const Object *o = a.logic.findObjectByID(m1[k]);
			const float now = o && !o->isEffectivelyDead() ? o->getBodyModule()->getHealth() : 0.0f;
			if (now < lastHealth[k])
			{
				out.loserHitSizes.push_back(lastHealth[k] - now);
				out.firstHitFrame = out.firstHitFrame < 0 ? f : out.firstHitFrame;
			}
			if (now <= 0.0f && lastHealth[k] > 0.0f)
			{
				out.deathFrames[k] = f;
			}
			lastHealth[k] = now;
		}
		for (size_t k = 0; k < m0.size() + m1.size(); ++k)
		{
			const Object *o = a.logic.findObjectByID(k < m0.size() ? m0[k] : m1[k - m0.size()]);
			const Weapon *w = o && o->getWeapons() ? o->getWeapons()->currentWeapon() : nullptr;
			if (w && w->lastFireFrame() != lastFire[k])
			{
				if (lastFire[k] != 0u)
				{
					out.minShotGap[k] = std::min(out.minShotGap[k], (int)(w->lastFireFrame() - lastFire[k]));
				}
				lastFire[k] = w->lastFireFrame();
			}
		}
		if (out.endFrame < 0 && healthOf(m1) <= 0.0f)
		{
			out.endFrame = f;
			out.applications = a.logic.combat().counters().damageApplications - applicationsBefore;
			out.winnerHealthEnd = healthOf(m0);
			out.winnerMembersEnd = 0;
			for (ObjectID id : m0)
			{
				const Object *o = a.logic.findObjectByID(id);
				const bool alive = o && !o->isEffectivelyDead();
				out.winnerMembersEnd += alive ? 1u : 0u;
				out.winnersKilled += alive ? 0u : 1u;
				out.winnerLost.push_back(alive ? o->getBodyModule()->getMaxHealth() - o->getBodyModule()->getHealth() : 200.0f);
			}
		}
		if (out.endFrame >= 0 && f > out.endFrame + 150)
		{
			break; // the corpses have sunk and been removed by now
		}
	}
	out.kills = a.logic.combat().counters().kills;
	out.cashWinner = a.player(0)->getMoney()->countMoney();
	out.cashLoser = a.player(1)->getMoney()->countMoney();
	out.usageLoserAfter = a.player(1)->commandPoints().getUsage();
	out.loserInPathfinder = a.inPathfinder(id1);
	for (ObjectID id : m1)
	{
		out.loserInPathfinder = out.loserInPathfinder || a.inPathfinder(id) || a.logic.findObjectByID(id) != nullptr;
	}
	out.loserHordeGone = a.logic.findObjectByID(id1) == nullptr;
	out.winnerHordeAlive = a.logic.findObjectByID(id0) != nullptr;
	return out;
}

// ---- the seven factions ---------------------------------------------------------------------------------------------------------------------------------------------
// One row per basic horde (melee and archer of each faction), read by hand from the 2.01 INI (INI.big + _patch201ini.big; #defines of gamedata.ini resolved):
//   member template's MaxHealth, BountyValue, CommandPoints and Armor (armor.ini percentages), the first weapon of its WeaponSet: AttackRange, PreAttackDelay (ms),
//   DelayBetweenShots (ms; for a clip weapon the minimum ClipReloadTime), the first DamageNugget's Damage and DamageType (an archer's: the ProjectileNugget's WarheadTemplate
//   weapon), HitPercentage. Frames: a logic frame is 200 ms, so ms -> ceil(ms * 0.005) (RW 0x73A429).
struct UnitRow
{
	const char *faction, *horde, *member;
	int count;
	float health, damage;
	const char *damageType;
	float range;
	int preAttackMs, gapMs;
	float hitPercent;
	int bounty, commandPoints;
	bool ranged;
	std::vector<std::pair<const char *, float>> armor; // armor.ini: damage type -> fraction of the damage that gets through
};

const std::vector<UnitRow> &unitRows()
{
	static const std::vector<UnitRow> rows = {
		{ "FactionMen", "GondorFighterHorde", "GondorFighter", 15, 200.0f, 40.0f, "SLASH", 11.5f, 500, 1000, 1.0f, 4, 4, false,
		   { {"DEFAULT", 0.85f}, {"SLASH", 0.85f}, {"PIERCE", 0.7f}, {"SPECIALIST", 0.25f}, {"CRUSH", 1.25f}, {"CAVALRY", 1.75f}, {"SIEGE", 1.0f}, {"FLAME", 1.0f} } }, // armor SoldierArmor
		{ "FactionMen", "GondorArcherHorde", "GondorArcher", 15, 150.0f, 35.0f, "PIERCE", 300.0f, 1000, 1500, 1.0f, 5, 4, true,
		   { {"DEFAULT", 0.85f}, {"SLASH", 1.35f}, {"PIERCE", 0.6f}, {"SPECIALIST", 1.35f}, {"CAVALRY", 1.85f}, {"CRUSH", 2.0f}, {"SIEGE", 1.0f}, {"FLAME", 1.0f} } }, // armor ArcherArmor
		{ "FactionElves", "ElvenLorienWarriorHorde", "ElvenLorienWarrior", 12, 200.0f, 80.0f, "SLASH", 11.5f, 700, 800, 1.0f, 6, 5, false,
		   { {"DEFAULT", 1.0f}, {"SLASH", 1.0f}, {"PIERCE", 0.75f}, {"SPECIALIST", 0.4f}, {"CRUSH", 1.25f}, {"SIEGE", 1.0f}, {"FLAME", 1.0f}, {"CAVALRY", 2.0f} } }, // armor LorienWarriorArmor
		{ "FactionElves", "ElvenLorienArcherHorde", "ElvenLorienArcher", 12, 220.0f, 40.0f, "PIERCE", 350.0f, 900, 1250, 1.0f, 6, 5, true,
		   { {"DEFAULT", 1.0f}, {"SLASH", 1.5f}, {"PIERCE", 0.75f}, {"SPECIALIST", 1.5f}, {"CRUSH", 2.0f}, {"SIEGE", 1.0f}, {"FLAME", 1.0f}, {"CAVALRY", 2.0f} } }, // armor LorienArcherArmor
		{ "FactionDwarves", "DwarvenPhalanxHorde", "DwarvenPhalanx", 15, 325.0f, 60.0f, "SPECIALIST", 36.0f, 600, 900, 1.0f, 7, 4, false,
		   { {"DEFAULT", 0.75f}, {"SLASH", 1.3f}, {"PIERCE", 0.9f}, {"SPECIALIST", 0.8f}, {"CRUSH", 0.1f}, {"SIEGE", 1.0f}, {"FLAME", 1.0f}, {"CAVALRY", 0.2f} } }, // armor DwarvenPikemenArmor
		{ "FactionDwarves", "DwarvenAxeThrowerHorde", "DwarvenAxeThrower", 12, 240.0f, 45.0f, "SLASH", 250.0f, 200, 2000, 0.8f, 5, 5, true,
		   { {"DEFAULT", 0.8f}, {"SLASH", 1.25f}, {"PIERCE", 0.7f}, {"SPECIALIST", 0.9f}, {"CRUSH", 2.0f}, {"SIEGE", 1.0f}, {"FLAME", 1.0f}, {"CAVALRY", 1.75f} } }, // armor DwarvenAxeThrowerArmor
		{ "FactionIsengard", "IsengardFighterHorde", "IsengardFighter", 15, 300.0f, 80.0f, "SLASH", 11.5f, 1300, 633, 1.0f, 7, 5, false,
		   { {"DEFAULT", 0.7f}, {"SLASH", 0.7f}, {"PIERCE", 0.6f}, {"SPECIALIST", 0.25f}, {"CAVALRY", 1.55f}, {"CRUSH", 1.25f}, {"SIEGE", 1.0f}, {"FLAME", 1.0f} } }, // armor UrukHaiArmor
		{ "FactionIsengard", "IsengardUrukCrossbowHorde", "IsengardUrukCrossbow", 15, 180.0f, 35.0f, "PIERCE", 450.0f, 500, 3500, 1.0f, 5, 4, true,
		   { {"DEFAULT", 0.8f}, {"SLASH", 1.25f}, {"PIERCE", 0.5f}, {"SPECIALIST", 1.25f}, {"CRUSH", 1.8f}, {"SIEGE", 1.0f}, {"FLAME", 1.0f}, {"CAVALRY", 2.0f} } }, // armor CrossbowArmor
		{ "FactionMordor", "MordorFighterHorde", "MordorFighter", 20, 75.0f, 20.0f, "SLASH", 11.5f, 633, 1000, 1.0f, 1, 2, false,
		   { {"DEFAULT", 1.0f}, {"SLASH", 1.0f}, {"PIERCE", 0.85f}, {"SPECIALIST", 0.4f}, {"CAVALRY", 1.9f}, {"CRUSH", 1.4f}, {"SIEGE", 1.0f}, {"FLAME", 1.0f} } }, // armor MordorOrcArmor
		{ "FactionMordor", "MordorArcherHorde", "MordorArcher", 20, 125.0f, 25.0f, "PIERCE", 320.0f, 1000, 1500, 1.0f, 2, 2, true,
		   { {"DEFAULT", 1.1f}, {"SLASH", 1.7f}, {"PIERCE", 0.9f}, {"SPECIALIST", 1.65f}, {"CRUSH", 2.0f}, {"SIEGE", 0.5f}, {"FLAME", 0.5f}, {"CAVALRY", 2.0f} } }, // armor MordorArcherArmor
		{ "FactionWild", "GoblinFighterHorde", "GoblinFighter", 20, 50.0f, 15.0f, "SLASH", 11.5f, 522, 245, 1.0f, 1, 2, false,
		   { {"DEFAULT", 1.1f}, {"SLASH", 1.25f}, {"PIERCE", 0.9f}, {"SPECIALIST", 0.5f}, {"CAVALRY", 2.0f}, {"CRUSH", 1.5f}, {"SIEGE", 1.0f}, {"FLAME", 1.0f} } }, // armor GoblinFighterArmor
		{ "FactionWild", "GoblinArcherHorde", "GoblinArcher", 20, 100.0f, 25.0f, "PIERCE", 275.0f, 1000, 1500, 1.0f, 2, 2, true,
		   { {"DEFAULT", 1.1f}, {"SLASH", 1.7f}, {"PIERCE", 0.9f}, {"SPECIALIST", 1.65f}, {"CRUSH", 2.0f}, {"SIEGE", 1.0f}, {"FLAME", 1.0f}, {"CAVALRY", 2.0f} } }, // armor GoblinArcherArmor
		{ "FactionAngmar", "AngmarDarkDunedainHorde", "AngmarDarkDunedain", 10, 400.0f, 100.0f, "SLASH", 11.5f, 1250, 1000, 1.0f, 11, 8, false,
		   { {"DEFAULT", 0.8f}, {"SLASH", 0.8f}, {"PIERCE", 0.9f}, {"SPECIALIST", 0.3f}, {"CRUSH", 2.0f}, {"SIEGE", 1.0f}, {"FLAME", 0.8f}, {"CAVALRY", 2.0f} } }, // armor AngmarDDArmor
		{ "FactionAngmar", "AngmarDarkRangerHorde", "AngmarDarkRanger", 12, 250.0f, 100.0f, "PIERCE", 400.0f, 1000, 1500, 1.0f, 10, 6, true,
		   { {"DEFAULT", 0.8f}, {"SLASH", 0.8f}, {"PIERCE", 0.4f}, {"SPECIALIST", 0.8f}, {"CRUSH", 2.0f}, {"SIEGE", 1.0f}, {"FLAME", 0.9f}, {"CAVALRY", 2.0f} } }, // armor AngmarDDRangerArmor
	};
	return rows;
}

const UnitRow &rowOf(const char *horde)
{
	for (const UnitRow &r : unitRows())
	{
		if (std::string(r.horde) == horde)
		{
			return r;
		}
	}
	throw std::logic_error(std::string("no unit row for ") + horde);
}

int framesOf(int ms)
{
	return (int)std::ceil((float)ms * 0.005f - 1e-4f); // RW 0x73A429
}

// what one hit of `from` takes off a member of `to`: the damage times the armor percentage of its damage type (DEFAULT when the armor names no such type)
float perHit(const UnitRow &from, const UnitRow &to)
{
	float pct = -1.0f, def = 1.0f;
	for (const auto &a : to.armor)
	{
		if (std::string(a.first) == from.damageType)
		{
			pct = a.second;
		}
		if (std::string(a.first) == "DEFAULT")
		{
			def = a.second;
		}
	}
	return from.damage * (pct >= 0.0f ? pct : def);
}

struct SideLog
{
	const UnitRow *row = nullptr;
	std::vector<ObjectID> ids;
	std::vector<float> lost;          // at the end of the fight
	std::vector<int> deathFrame;
	std::vector<int> minGap;          // smallest number of frames between two shots of the member
	std::vector<unsigned> lastFire;
	int firstDamageFrame = -1;        // the first frame a member of this side lost health
	bool offStep = false;             // a member's health was not hp - k * hit at some frame
	int veteranFrame = -1;            // lane XP-1: the first frame a member of this side reached veterancy rank 2 (its level bonuses change the arithmetic)
	std::string offStepWhat;
	size_t alive = 0;
	int usageBefore = 0, usageAfter = 0;
	std::uint32_t cash = 0;
	// lane INTEG-1: a side whose horde reached a rank above BannerCarrierMinLevel got a banner carrier (RW 0x873ACF -> 0x8719E4(1)); out of combat the carrier refills
	// the horde (BannerCarrierUpdate RW 0x89ACE4 / 0x89A392) in the 150 frames after the fight: those members and the carrier's own CommandPoints are counted too
	int replenished = 0, carrierPoints = 0;
};

struct FactionFight
{
	SideLog side[2];
	std::vector<std::uint32_t> hashes;
	int endFrame = -1;                // the frame one side had no member left
	int loserSide = -1;
	unsigned long long applications = 0;
	unsigned long long kills = 0;
	bool loserGone = false, loserInPathfinder = true;
	unsigned long long unportedNuggets = 0;
	int flankFrame = -1; // the first frame a flank test answered yes (HORDE-2's FlankingBonus, RW 0x876FC4: that hit's damage is scaled)
};

FactionFight fightFactions(const char *factionA, const char *hordeA, const char *factionB, const char *hordeB, float bountyPercent, int maxFrames)
{
	SharedWorld &s = shared();
	FactionFight out;
	Arena a(s, factionA, factionB);
	a.player(0)->setBountyPercent(bountyPercent);
	a.player(1)->setBountyPercent(bountyPercent);
	Object *h[2] = { a.place(hordeA, 0, 500.0f, 500.0f), a.place(hordeB, 1, 640.0f, 500.0f) };
	const ObjectID hid[2] = { h[0]->getID(), h[1]->getID() };
	a.logic.runLogicFrame();
	a.logic.runLogicFrame();
	const char *names[2] = { hordeA, hordeB };
	for (int k = 0; k < 2; ++k)
	{
		out.side[k].row = &rowOf(names[k]);
		for (const Object *m : *h[k]->getContain()->getContainedItemsList())
		{
			out.side[k].ids.push_back(m->getID());
		}
		const size_t n = out.side[k].ids.size();
		out.side[k].deathFrame.assign(n, -1);
		out.side[k].minGap.assign(n, 1 << 30);
		out.side[k].lastFire.assign(n, 0u);
		out.side[k].usageBefore = a.player(k)->commandPoints().getUsage();
	}
	const unsigned long long applicationsBefore = a.logic.combat().counters().damageApplications;
	const unsigned long long flanksBefore = a.logic.combat().counters().flanks;
	h[0]->getAIUpdateInterface()->aiAttackObject(h[1], CMD_FROM_PLAYER); // the other horde is not ordered: it acquires its attacker on its own
	out.hashes.push_back(a.logic.computeStateHash());
	for (int f = 0; f < maxFrames; ++f)
	{
		a.logic.runLogicFrame();
		out.hashes.push_back(a.logic.computeStateHash());
		if (out.flankFrame < 0 && a.logic.combat().counters().flanks != flanksBefore)
		{
			out.flankFrame = f;
		}
		size_t aliveCount[2] = { 0, 0 };
		for (int k = 0; k < 2; ++k)
		{
			SideLog &sl = out.side[k];
			const UnitRow &enemy = *out.side[1 - k].row;
			const float hit = perHit(enemy, *sl.row);
			for (size_t i = 0; i < sl.ids.size(); ++i)
			{
				const Object *o = a.logic.findObjectByID(sl.ids[i]);
				REQUIRE_MESSAGE((!o || o->getBodyModule() != nullptr), "object " << sl.ids[i] << " (" << sl.row->member << ") is now a " << (o ? o->getTemplate()->getName() : std::string()) << " without a body");
				const bool dead = !o || o->isEffectivelyDead();
				const float health = dead ? 0.0f : o->getBodyModule()->getHealth();
				aliveCount[k] += dead ? 0u : 1u;
				if (health < sl.row->health && sl.firstDamageFrame < 0)
				{
					sl.firstDamageFrame = f;
				}
				if (dead && sl.deathFrame[i] < 0)
				{
					sl.deathFrame[i] = f;
				}
				if (!dead && sl.veteranFrame < 0 && o->getExperienceTracker() && o->getExperienceTracker()->getRank() > 1)
				{
					sl.veteranFrame = f;
				}
				// the health is always the member's health minus a whole number of hits (or 0 once the clipped last hit killed it), until a level-up of either side
				// (lane XP-1: the level's HEALTH / DAMAGE_ADD bonuses change both numbers)
				// (and until a flank: lane MODULES-2's merge, the FlankingBonus scales that hit)
				if (!dead && !sl.offStep && sl.veteranFrame < 0 && out.side[1 - k].veteranFrame < 0 && out.flankFrame < 0)
				{
					const float steps = (sl.row->health - health) / hit;
					if (std::fabs(steps - std::round(steps)) > 1e-3f)
					{
						sl.offStep = true;
						sl.offStepWhat = std::string(sl.row->member) + " health " + std::to_string(health) + " is not " + std::to_string(sl.row->health) + " - k * " + std::to_string(hit);
					}
				}
				const Weapon *w = o && o->getWeapons() ? o->getWeapons()->currentWeapon() : nullptr;
				if (w && w->lastFireFrame() != sl.lastFire[i])
				{
					if (sl.lastFire[i] != 0u)
					{
						sl.minGap[i] = std::min(sl.minGap[i], (int)(w->lastFireFrame() - sl.lastFire[i]));
					}
					sl.lastFire[i] = w->lastFireFrame();
				}
			}
		}
		if (out.endFrame < 0 && (aliveCount[0] == 0 || aliveCount[1] == 0))
		{
			out.endFrame = f;
			out.loserSide = aliveCount[0] == 0 ? 0 : 1;
			out.applications = a.logic.combat().counters().damageApplications - applicationsBefore;
			for (int k = 0; k < 2; ++k)
			{
				SideLog &sl = out.side[k];
				sl.alive = aliveCount[k];
				for (ObjectID id : sl.ids)
				{
					const Object *o = a.logic.findObjectByID(id);
					const bool dead = !o || o->isEffectivelyDead();
					sl.lost.push_back(dead ? sl.row->health : o->getBodyModule()->getMaxHealth() - o->getBodyModule()->getHealth());
				}
			}
		}
		if (out.endFrame >= 0 && f > out.endFrame + 150)
		{
			break; // the corpses of the loser have sunk and been removed by now
		}
	}
	out.kills = a.logic.combat().counters().kills;
	out.unportedNuggets = a.logic.combat().counters().unportedNuggets;
	for (int k = 0; k < 2; ++k)
	{
		out.side[k].usageAfter = a.player(k)->commandPoints().getUsage();
		out.side[k].cash = a.player(k)->getMoney()->countMoney();
		Object *horde = a.logic.findObjectByID(hid[k]);
		ContainModuleInterface *c = horde ? horde->getContain() : nullptr;
		HordeContain *hc = c && c->getHordeContainInterface() ? dynamic_cast<HordeContain *>(c) : nullptr;
		Object *carrier = hc && hc->bannerCarrier() != 0 ? a.logic.findObjectByID(hc->bannerCarrier()) : nullptr;
		if (carrier && !carrier->isEffectivelyDead())
		{
			const BannerCarrierUpdate *b = dynamic_cast<const BannerCarrierUpdate *>(carrier->findModule("BannerCarrierUpdate"));
			REQUIRE(b != nullptr);
			out.side[k].replenished = (int)b->membersSpawned();
			out.side[k].carrierPoints = (int)Economy::templateInt(*static_cast<const ThingTemplate *>(carrier->getTemplate()), "CommandPoints");
			std::printf("  info: %s: banner carrier %s, %d members refilled, its CommandPoints %d\n", out.side[k].row->horde, carrier->getTemplate()->getName().c_str(),
				out.side[k].replenished, out.side[k].carrierPoints);
		}
	}
	if (out.loserSide >= 0)
	{
		const SideLog &l = out.side[out.loserSide];
		out.loserGone = a.logic.findObjectByID(hid[out.loserSide]) == nullptr;
		out.loserInPathfinder = a.inPathfinder(hid[out.loserSide]);
		for (ObjectID id : l.ids)
		{
			out.loserGone = out.loserGone && a.logic.findObjectByID(id) == nullptr;
			out.loserInPathfinder = out.loserInPathfinder || a.inPathfinder(id);
		}
	}
	return out;
}

// lane HERO-2: a member with DualWeaponBehavior (RW 0x85DF88) takes its close range weapon set (CLOSE_RANGE) once an enemy is within
// SwitchWeaponOnCloseRangeDistance: its hits are no longer the row's weapon's, so the rules that derive from that weapon (the hit size, the hit count, the
// first hit and the gaps) do not hold for the damage it deals
bool switchesWeapons(const UnitRow &r)
{
	const ThingTemplate *t = shared().world->things().findTemplate(r.member);
	if (!t)
	{
		return false;
	}
	for (const ThingTemplate::Nugget &n : t->getFinalOverride()->behaviorModules().nuggets())
	{
		if (n.name == "DualWeaponBehavior")
		{
			return true;
		}
	}
	return false;
}

// every asserted rule of a fight, derived from the rows above
void checkFight(const FactionFight &f, const char *label)
{
	INFO(label);
	REQUIRE_MESSAGE(f.endFrame > 0, "neither horde was wiped out");
	const UnitRow &ra = *f.side[0].row;
	const UnitRow &rb = *f.side[1].row;
	REQUIRE(f.side[0].ids.size() == (size_t)ra.count);
	REQUIRE(f.side[1].ids.size() == (size_t)rb.count);
	// (1) the damage per hit: every member's health stays hp - k * (enemy damage * armor), the last hit being clipped
	const bool dual[2] = { switchesWeapons(ra), switchesWeapons(rb) };
	for (int k = 0; k < 2; ++k)
	{
		if (dual[1 - k])
		{
			MESSAGE(std::string(label) << ": " << f.side[1 - k].row->member << " switches to its close range weapon (DualWeaponBehavior): its hits are not checked");
			continue;
		}
		CHECK_MESSAGE(!f.side[k].offStep, f.side[k].offStepWhat);
	}
	// (2) the hits: each application takes one hit's worth, the killing hit is clipped to the health left, so applications = sum over members of ceil(lost / hit)
	unsigned long long hits = 0;
	for (int k = 0; k < 2; ++k)
	{
		const float hit = perHit(*f.side[1 - k].row, *f.side[k].row);
		for (float lost : f.side[k].lost)
		{
			hits += (unsigned long long)std::ceil(lost / hit - 1e-3f);
		}
	}
	if (f.flankFrame >= 0)
	{
		// lane MODULES-2 (merge with PHYS-1): the retail random draws of the emotion trackers and the group bonus move the melee machine's positions, and a
		// fight can now contain a flank (FlankingBonus scales that hit): the hit count is not the plain one then
		MESSAGE(std::string(label) << ": a flank at frame " << f.flankFrame << " (FlankingBonus): the hit count is not checked");
	}
	else if (dual[0] || dual[1])
	{
		MESSAGE(std::string(label) << ": a DualWeaponBehavior side (lane HERO-2): the hit count is not checked");
	}
	else if (f.side[0].veteranFrame < 0 && f.side[1].veteranFrame < 0)
	{
		CHECK(f.applications == hits);
	}
	else
	{
		MESSAGE(std::string(label) << ": a member reached veterancy rank 2 at frame " << std::max(f.side[0].veteranFrame, f.side[1].veteranFrame) << " (lane XP-1): the hit count is not checked");
	}
	// (3) the kills: the loser has no member left, the winner lost the members that reached 0
	size_t dead = 0;
	for (int k = 0; k < 2; ++k)
	{
		dead += (size_t)f.side[k].ids.size() - f.side[k].alive;
	}
	CHECK(f.side[f.loserSide].alive == 0);
	CHECK(f.side[1 - f.loserSide].alive > 0);
	CHECK(f.kills == dead);
	// (4) timing: a hit needs its pre-attack first, nobody shoots faster than DelayBetweenShots (a clip weapon: the shortest clip reload), and the loser's health in hits divided
	// by the number of shooters, one shot per gap each, is the shortest possible fight
	for (int k = 0; k < 2; ++k)
	{
		const UnitRow &enemy = *f.side[1 - k].row;
		if (dual[1 - k])
		{
			continue; // lane HERO-2: the close range weapon's pre-attack and delay are not the row's
		}
		CHECK(f.side[k].firstDamageFrame >= framesOf(enemy.preAttackMs));
		for (int gap : f.side[1 - k].minGap)
		{
			CHECK(gap >= framesOf(enemy.gapMs));
		}
	}
	{
		const UnitRow &win = *f.side[1 - f.loserSide].row;
		const UnitRow &lose = *f.side[f.loserSide].row;
		const float hit = perHit(win, lose);
		const int perMember = (int)std::ceil(lose.health / hit - 1e-3f);
		const int shots = perMember * lose.count; // every shot hits (the best case: no miss, no overkill)
		const int rounds = (shots + win.count - 1) / win.count;
		if (!dual[1 - f.loserSide]) // lane HERO-2: not for a winner that switches weapons
		{
			CHECK(f.endFrame >= f.side[f.loserSide].firstDamageFrame + (rounds - 1) * framesOf(win.gapMs));
		}
	}
	// (5) the loser's horde and members left the world and the pathfinder; (6) the bounty and the command points
	CHECK(f.loserGone);
	CHECK_FALSE(f.loserInPathfinder);
	for (int k = 0; k < 2; ++k)
	{
		const SideLog &sl = f.side[k];
		const SideLog &en = f.side[1 - k];
		size_t enemyDead = en.ids.size() - en.alive;
		CHECK(sl.cash == (std::uint32_t)(enemyDead * (size_t)en.row->bounty)); // percent 1.0: the BountyValue of each kill
		CHECK(sl.usageBefore == sl.row->commandPoints * sl.row->count);
		CHECK(sl.usageAfter == sl.row->commandPoints * ((int)sl.alive + sl.replenished) + sl.carrierPoints);
	}
}

struct Matchup
{
	const char *factionA, *hordeA, *factionB, *hordeB;
};

// every faction's basic melee horde and basic archer horde against another faction's basic melee horde
const Matchup kMatchups[] = {
	{ "FactionMen", "GondorFighterHorde", "FactionMordor", "MordorFighterHorde" },
	{ "FactionMen", "GondorArcherHorde", "FactionMordor", "MordorFighterHorde" },
	{ "FactionElves", "ElvenLorienWarriorHorde", "FactionIsengard", "IsengardFighterHorde" },
	{ "FactionElves", "ElvenLorienArcherHorde", "FactionIsengard", "IsengardFighterHorde" },
	{ "FactionDwarves", "DwarvenPhalanxHorde", "FactionAngmar", "AngmarDarkDunedainHorde" },
	{ "FactionDwarves", "DwarvenAxeThrowerHorde", "FactionAngmar", "AngmarDarkDunedainHorde" },
	{ "FactionIsengard", "IsengardFighterHorde", "FactionMen", "GondorFighterHorde" },
	{ "FactionIsengard", "IsengardUrukCrossbowHorde", "FactionMen", "GondorFighterHorde" },
	{ "FactionMordor", "MordorFighterHorde", "FactionElves", "ElvenLorienWarriorHorde" },
	{ "FactionMordor", "MordorArcherHorde", "FactionElves", "ElvenLorienWarriorHorde" },
	{ "FactionWild", "GoblinFighterHorde", "FactionDwarves", "DwarvenPhalanxHorde" },
	{ "FactionWild", "GoblinArcherHorde", "FactionDwarves", "DwarvenPhalanxHorde" },
	{ "FactionAngmar", "AngmarDarkDunedainHorde", "FactionWild", "GoblinFighterHorde" },
	{ "FactionAngmar", "AngmarDarkRangerHorde", "FactionWild", "GoblinFighterHorde" },
};
} // namespace

// Gondor Fighters (GondorFighterHorde, 15 men) ordered onto Mordor Orc warriors (MordorFighterHorde, 20 orcs) that are left alone, retail 2.01 data (INI.big + _patch201ini.big):
//   frame length: DelayBetweenShots = 1000 ms and PreAttackDelay = 500 ms are 5 and 3 frames (ceil(ms * 0.005), RW 0x73A429; RotWK's logic frame is 200 ms)
//   GondorFighter: ActiveBody MaxHealth GONDOR_SOLDIER_HEALTH = 200, SoldierArmor (patch 2.01): SLASH 85%; weapon GondorSword: DamageNugget 40 SLASH, DelayBetweenShots 5 frames
//   MordorFighter: MaxHealth 75, MordorOrcArmor SLASH 100%; weapon MordorWarriorAxe: DamageNugget 20 SLASH, DelayBetweenShots 5 frames
//   so a sword hit takes 40 of an orc (the flanking bonus is stop S-322: not applied), an axe hit takes 20 * 85% = 17 of a soldier
//   an orc (75) dies on its second sword hit (35 left, then clipped), so killing 20 orcs takes 40 applications of damage
TEST_CASE("combat retail: a Gondor fighter horde attacks a Mordor fighter horde: hits, deaths, the loser leaves the world, the bounty is paid")
{
	if (!haveWorld())
	{
		return;
	}
	const FightOutcome o = fightMelee("FactionMen", "GondorFighterHorde", "FactionMordor", "MordorFighterHorde", 1.0f);
	REQUIRE(o.winnerMembersStart == 15);
	REQUIRE(o.loserMembersStart == 20);
	CHECK(o.winnerHealthStart == 15 * 200.0f);
	CHECK(o.loserHealthStart == 20 * 75.0f);
	REQUIRE(o.endFrame > 0);
	// contact needs the horde to walk (they start 140 apart, melee reach 11.5) and the first swing waits for its 3 frame pre-attack
	CHECK(o.firstHitFrame >= 3);
	// every orc died, every one of them took exactly two sword hits (40 + the clipped 35)
	for (int d : o.deathFrames)
	{
		CHECK(d > 0);
	}
	// the orcs' health only ever dropped in 40s, except the clipped last hit of each orc (35): sum over all steps is the whole 20 * 75
	float sum = 0.0f;
	for (float h : o.loserHitSizes)
	{
		sum += h;
		const float units = h / 35.0f;
		const bool whole40s = std::fabs(h / 40.0f - std::round(h / 40.0f)) < 1e-4f;
		const bool hasClip = std::fabs(units - std::round(units)) < 1e-4f || std::fabs((h - 35.0f) / 40.0f - std::round((h - 35.0f) / 40.0f)) < 1e-4f;
		const bool made = whole40s || hasClip;
		CHECK_MESSAGE(made, "a step of " << h << " is not made of sword hits (40) and one clipped last hit (35)");
	}
	CHECK(sum == doctest::Approx(1500.0f));
	// the damage applications of the fight: 40 on the orcs (2 per orc), and on each soldier ceil(health lost / 17) (the killing hit is clipped to what was left)
	const float menLost = o.winnerHealthStart - o.winnerHealthEnd;
	unsigned long long menHits = 0;
	for (float lost : o.winnerLost)
	{
		menHits += (unsigned long long)std::ceil(lost / 17.0f - 1e-4f);
	}
	CHECK(o.applications == 40 + menHits);
	// nobody shoots faster than DelayBetweenShots (5 frames)
	for (int gap : o.minShotGap)
	{
		CHECK(gap >= 5);
	}
	// the winners are not all dead and their horde still stands; the losers' horde and members left the world and the pathfinder
	CHECK(o.winnerMembersEnd > 0);
	CHECK(o.winnerHordeAlive);
	CHECK(o.loserHordeGone);
	CHECK_FALSE(o.loserInPathfinder);
	// the bounty: percent 1.0 of the victim's BountyValue per kill (ECON-1 Economy::awardBounty; MORDOR_FIGHTER_BOUNTY_VALUE 1, GONDOR_SOLDIER_BOUNTY_VALUE 4), and the command
	// points of the dead are free again (the orcs' horde held 20 members' worth: usage > 0 before, 0 after)
	CHECK(o.kills == 20 + o.winnersKilled);
	CHECK(o.cashWinner == 20u * 1u);
	CHECK(o.cashLoser == 4u * (unsigned)o.winnersKilled);
	CHECK(o.usageLoserBefore > 0);
	CHECK(o.usageLoserAfter == 0);
	std::printf("  info: Gondor vs Mordor: %zu men left of %zu (%.0f health lost), the orcs died by frame %d, %llu applications, bounty cash %u, loser command points %d -> %d\n", o.winnerMembersEnd,
		o.winnerMembersStart, menLost, o.endFrame, o.applications, o.cashWinner, o.usageLoserBefore, o.usageLoserAfter);
}

TEST_CASE("combat retail: the same fight twice gives the same state hash in every frame")
{
	if (!haveWorld())
	{
		return;
	}
	const FightOutcome a = fightMelee("FactionMen", "GondorFighterHorde", "FactionMordor", "MordorFighterHorde", 1.0f);
	const FightOutcome none = fightMelee("FactionMen", "GondorFighterHorde", "FactionMordor", "MordorFighterHorde", 0.0f);
	// at the default bounty percent 0 a skirmish pays nothing (economy.md section 7), the fight itself is the same
	CHECK(none.cashWinner == 0u);
	CHECK(none.cashLoser == 0u);
	CHECK(none.endFrame == a.endFrame);
	const FightOutcome b = fightMelee("FactionMen", "GondorFighterHorde", "FactionMordor", "MordorFighterHorde", 1.0f);
	REQUIRE(a.hashes.size() == b.hashes.size());
	for (size_t i = 0; i < a.hashes.size(); ++i)
	{
		REQUIRE_MESSAGE(a.hashes[i] == b.hashes[i], "frame " << i);
	}
	CHECK(a.hashes.size() > 100);
	// the state does change while they fight
	CHECK(a.hashes[10] != a.hashes[a.hashes.size() - 1]);
}

TEST_CASE("combat retail: every faction's basic melee horde and basic archer horde fights an enemy horde until one side is dead")
{
	if (!haveWorld())
	{
		return;
	}
	for (const Matchup &m : kMatchups)
	{
		const FactionFight f = fightFactions(m.factionA, m.hordeA, m.factionB, m.hordeB, 1.0f, 6000);
		const std::string label = std::string(m.hordeA) + " attacks " + m.hordeB;
		checkFight(f, label.c_str());
		std::printf("  info: %s: %s died at frame %d, %llu applications, %zu / %zu members left\n", label.c_str(), f.loserSide == 0 ? m.hordeA : m.hordeB, f.endFrame, f.applications,
			f.side[0].alive, f.side[1].alive);
	}
}
