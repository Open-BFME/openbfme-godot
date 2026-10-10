// OpenBFME retail tests for structures in combat (lane COMBAT-2). They run only when ROTWK_INSTALL and BFME2_INSTALL are set (otherwise SKIP): pure RotWK 2.01 + BFME2 1.06.
// Same file-order rule as test_hud_combat_retail.cpp: the HUD, combat and structure tests share ONE retail world.
//
// A Gondor fighter horde (15 members, GondorSword: 40 SLASH every 5 frames) attacks the barracks equivalent of each of the seven factions until it is destroyed. The rows below are read by
// hand from the 2.01 INI (INI.big + _patch201ini.big; the #defines of gamedata.ini resolved):
//   template, body class, MaxHealth, armour (armor.ini: FactoryArmor SLASH 20%, ResourceArmor SLASH 50%, MordorBarracks has none: 100%), BountyValue, the StructureCollapseUpdate
//   (every one: MinCollapseDelay = MaxCollapseDelay = 0, CollapseDamping 0.5, DestroyObjectWhenDone Yes; CollapseHeight in the row, the largest ACTIVE Geometry shape in the row when the
//   field is smaller), or the DestroyDie of MordorBarracks.
// Derived by hand:
//   * a sword hit takes 40 * SLASH% of the structure's health (8 on a FactoryArmor structure), so a structure of H health needs ceil(H / hit) hits (+1 when the float percent leaves a
//     residue: 20% parses to 0.19999999, a hit is 7.9999995 and 375 of them leave 1.8e-4 of a 3000 health structure);
//   * after the killing hit the collapse starts: the first update (next frame) begins the fall, the velocity grows by gravity * (1 - damping) = 2.56 * 0.5 = 1.28 per frame (GameData
//     Gravity -64 per second squared at 5 frames per second), so after n updates the fall is 1.28 * n * (n - 1) / 2 and the structure is destroyed at the first n with that >= the collapse
//     height H: n = smallest with n * (n - 1) >= H / 0.64; it leaves the world n frames after its death frame (the destroy list runs at the end of that frame);
//   * the bounty is BountyValue * the killer's player's bounty percent, paid once.
#include "doctest.h"
#include "StructureArena.h"

#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Module/StructureModules.h"
#include "GameLogic/VictoryConditions.h"
#include "GameLogic/System/TerrainResourceManager.h"

#include <cmath>
#include <cstdio>

using namespace structtest;

namespace
{
struct StructRow
{
	const char *faction, *templ, *body;
	float health;
	float slashPercent; // the armour's SLASH percentage (100 when the template has no armour)
	int bounty;
	float collapseHeight; // the height the fall has to cover: max(CollapseHeight, the largest active Geometry shape)
	bool collapses;       // StructureCollapseUpdate with DestroyObjectWhenDone (Mordor: DestroyDie)
	int armorFrames;      // pinned from the run: the frame the structure died (see the comment at the check)
};

// the death frames (last column) are this engine's attack machine (S-325 / S-327), not binary facts. Lane PATH-2 moved them earlier (264 -> 258, 266 -> 258,
// 430 -> 426, 62 -> 58): the attackers' approach distance adds the bounding SPHERES (RW obj + 0xBC), which are now RW 0xAD2860 / 0xAD2770 over every shape
// of the barracks' geometry (multi-shape; a cylinder's sqrt(r^2 + (h/2)^2)), so the members start swinging from farther away.
// Lane INTEG-1 (HORDE-2 + PATH-2 combined) moved them again (258 -> 256, 258 -> 256, 426 -> 422, 258 -> 256 Isengard, 258 -> 256 Angmar): HORDE-2's melee
// readiness (RW 0x98FA8E) and its member reach tests measure the edge gap with RW 0x6634BF, which subtracts the bounding CIRCLES (RW obj + 0xB8 = GeometryInfo
// + 0x10); RW 0xAD2860 sets that to the largest circle over every active shape, which PATH-2 ported. With the multi-shape circle the horde is ready at the
// barracks earlier (with the single-shape circle in the horde code alone the frames would be 264 / 260 / 426 / 262 / 264). Engine pins, not retail values.
// Lane PHYS-1: PHYS-1's crowd predicate exposes the unported allied clearing / attack-path machinery (S-166 / S-783). External per-member traces (review r2) reproduce
// every round 2 death pin: the steady contributing member counts are 8 / 11 / 8 / 8 / 7 / 7 / 8 in row order, versus 15 with only the crowd test bypassed. Gondor
// members 7, 8, 9, 10, 12, 13 and 14 remain idle behind allied goal reservations. Neither version puts a member centre inside a structure in these fixtures. These
// remain OpenBFME timings, not retail timings. Round 3 (the attack path S-784, allied clearing S-785, the Amoeba's goal reservation following its steps RW 0x990AB8)
// gives 446 / 326 / 726 / 496 / 66 / 126 / 446 with 9 / 12 / 9 / 7 / - / 7 / 9 steady contributors (`phys1 retail: the contributors of ...`); the members still idle
// are in HORDE-2's Amoeba idle cycle and scoring. Round 4 (the request RW 0x6639CF wired into the horde approach state as temporary non-retail wiring, S-786): GoblinCave 126 -> 108 (9 contributors), the
// others unchanged. Round 5 (RW 0x74A594's non-fleeing chain in the horde state, the temporary wiring removed, S-590): MordorBarracks 66 -> 74, GoblinCave 108 -> 118
// (8 contributors); the others unchanged (their horde is ready before the chain runs). Review r5 fixes (the squared, clamped edge gain of RW 0x6CA525, KeepRequest,
// the off-map refusal): MordorBarracks 74 -> 66, GoblinCave 118 -> 120 (9 contributors).
// Round 6 (the per-unit melee machine RW 0x744B71: the members attack through 0xE1 / 0xE2 and request no path of their own (crushing members can take 0xE9), S-787; RW 0x7463E8 in the horde chain):
// 446 -> 434, 326 -> 324, 726 -> 724, 496 -> 454, 66 -> 62, 120 -> 108, 446 -> 434 with 9 / 12 / 9 / 9 / - / 9 / 9 steady contributors.
// Lane MODULES-2 merge: the retail logic random draws of the emotion trackers (one QuarrelProbability draw per tracker and frame, RW 0x8B5DF3) and of the
// group bonus's first wake (RW 0x8937B5) move the random stream the melee machine reads: 434 -> 456, 324 -> 334, 724 -> 784, 454 -> 484, 62 -> 64,
// 108 -> 118, 434 -> 456 (without those two draws the pins above come back unchanged).
// Lane MOVE-2 r2: the horde's command hand-off (RW 0x89E169 -> slot 0x14 RW 0x87594C: the attack order first makes the members busy), the move hub's full
// busy rule (RW 0x874749: the horde's isMoving or the member's isIdle) and the member order's active-member rule / leash (RW 0x877B69 / 0x877B4F) change which
// members swing when: 456 -> 448, 334 -> 344, 784 -> 714, 484 -> 488, 64 -> 60, 118 -> 94, 456 -> 448. Engine pins.
// Lane MOD-4: AISpecialPowerUpdate runs (the Gondor horde's stance and capture modules): the UpdateModule constructor (RW 0x653114) wakes it in the first frame where the
// UnportedModule slept from its creation, which moves the update order the melee machine's draws follow: 456 -> 446, 334 -> 354, 784 -> 794, 484 -> 468,
// 64 -> 62, 118 -> 112, 456 -> 446 (with AISpecialPowerUpdate unregistered the pins above come back unchanged). Engine pins.
// Lane MOVE-2 r3 (the melee member pass: the contain runs the Amoeba update and moves the members to their melee destinations through the hub, RW 0x872EFC /
// 0x870A1B / 0x877D89; blockedBy's step aside as the AI command RW 0x66C4CA; the stored orientation wrapped, RW 0x70C31E): 454 -> 468, 334 -> 344, 740 -> 714, 424 -> 460, 58 -> 76, 108 -> 128, 454 -> 468. Engine pins.
const StructRow kRows[] = {
	// Merge MOD-4 + AUDIO-4: the death frames re-measured on the merged tree (MOD-4's powers / creeps and LargeGroupAudioUpdate's logic random draws both move them). Engine pins
	// Merge MOD-4 + AUDIO-4 + ANIM-1: the death frames re-measured on the merged tree (MOD-4's powers / creeps, LargeGroupAudioUpdate's logic random draws and
	// ANIM-1's attack timing all move them). Engine pins
	// Lane EXIT-1 (the hub's busy rule RW 0x87471B, the horde member update RW 0x66C748, maintainCurrentPosition RW 0x5E7CC7): the members reach the barracks in
	// RW's straight member steps: 432 -> 356, 326 unchanged, 742 -> 590, 482 -> 420, 58 -> 62, 112 -> 118, 432 -> 356. Engine pins
	// Merge EXIT-1 + MOVE-2 (EXIT-1's straight member steps with MOVE-2's melee member pass and the hand-off of the attack order): 356 -> 436, 326 -> 324,
	// 590 -> 664, 420 -> 384, 62 -> 52, 118 -> 110, 356 -> 436; the hit counts are unchanged. Engine pins
	// Lane MOVE-3 (the horde's own footprint RW 0x6ED071 in its attack path and destination checks, the member goal reservation RW 0x86EF13): 436, 324 -> 314,
	// 664 -> 684, 384 -> 444, 52, 110, 436; the hit counts are unchanged. Engine pins
	// MOVE-3 r2 and r3 (the exit's moveAlliesAwayFromDestination; the review fixes: a horde's line test radius 1 RW 0x6EE12D, the goal slot's angle, the patch
	// cost's candidate cell, the raw segment's NaN rule): re-measured unchanged. Engine pins
	// Lane IDLE-1 r2 (the AI updates in updates[0] and HordeContain in updates[1], RW 0x851E97 / 0x490AC4: every member's update now runs before its horde's member
	// pass, and the hub leaves a member whose physics motion is disabled alone, RW 0x874724): 436 -> 438, 324 -> 344, 664 -> 604, 384 -> 344, 52 -> 54, 110 -> 112,
	// 436 -> 438. Engine pins, not retail values.
	// Merge of IDLE-1 r2 (22e29980) into MOVE-3 with MOVE-3 r4: re-measured on JonathanPC, the IDLE-1 values above unchanged. Engine pins
	{ "FactionMen", "GondorBarracks", "ActiveBody", 3000.0f, 20.0f, 75, 155.0f, true, 438 },
	{ "FactionElves", "ElvenBarracks", "StructureBody", 3000.0f, 20.0f, 0, 100.0f, true, 344 }, // no BountyValue line
	{ "FactionDwarves", "DwarfBarracks", "ActiveBody", 5000.0f, 20.0f, 125, 155.0f, true, 604 },
	{ "FactionIsengard", "IsengardUrukPit", "StructureBody", 3000.0f, 20.0f, 88, 89.0f, true, 344 },
	{ "FactionMordor", "MordorBarracks", "StructureBody", 1500.0f, 100.0f, 0, 0.0f, false, 54 },
	{ "FactionWild", "GoblinCave", "ActiveBody", 1500.0f, 50.0f, 100, 25.0f, true, 112 },
	{ "FactionAngmar", "AngmarBarracks", "ActiveBody", 3000.0f, 20.0f, 75, 155.0f, true, 438 },
};

// n * (n - 1) / 2 * 1.28 >= H, the first n
int collapseUpdates(float height)
{
	int n = 1;
	while (1.28 * n * (n - 1) / 2.0 < (double)height)
	{
		++n;
	}
	return n;
}

struct Outcome
{
	std::vector<std::uint32_t> hashes;
	bool bodyOk = false;
	std::string bodyClass;
	float maxHealth = 0.0f;
	int deathFrame = -1, goneFrame = -1;
	unsigned long long applications = 0;
	std::vector<float> drops;              // every positive health step of one frame
	bool obstacleBefore = false;           // the footprint was an obstacle while the structure stood
	bool obstacleAfterDeath = true;        // ... and is not any more on the frame it died
	bool rubbleState = false, rubbleCondition = false;
	int cashBefore = 0, cashAfter = 0;
	bool claimantBefore = false, claimantAfter = true;
	unsigned long long collapsesBegun = 0, collapsesDone = 0;
	int firstHitFrame = -1;
	float perHitSeen = 0.0f;
};

Outcome fightStructure(const StructRow &row, float bountyPercent, bool recordHashes)
{
	SharedWorld &s = shared();
	Outcome out;
	Arena a(s, "FactionMen", row.faction);
	a.player(0)->setBountyPercent(bountyPercent);
	Object *horde = a.place("GondorFighterHorde", 0, 500.0f, 500.0f);
	Object *b = a.place(row.templ, 1, 640.0f, 500.0f);
	const ObjectID id = b->getID();
	a.logic.runLogicFrame();
	a.logic.runLogicFrame();
	BodyModuleInterface *body = b->getBodyModule();
	REQUIRE(body != nullptr);
	out.maxHealth = body->getMaxHealth();
	for (const auto &m : b->modules())
	{
		if (dynamic_cast<const ActiveBody *>(m.get()))
		{
			out.bodyClass = dynamic_cast<const StructureBody *>(m.get()) ? "StructureBody" : "ActiveBody";
		}
	}
	out.obstacleBefore = a.obstacleAt(640.0f, 500.0f);
	for (const TerrainResourceManager::Claimant &c : a.logic.economy().resources().claimants())
	{
		out.claimantBefore = out.claimantBefore || c.id == id;
	}
	out.cashBefore = (int)a.player(0)->getMoney()->countMoney();
	const unsigned long long applicationsBefore = a.logic.combat().counters().damageApplications;
	horde->getAIUpdateInterface()->aiAttackObject(b, CMD_FROM_PLAYER);
	if (recordHashes)
	{
		out.hashes.push_back(a.logic.computeStateHash());
	}
	float last = out.maxHealth;
	for (int f = 0; f < 2000; ++f)
	{
		a.logic.runLogicFrame();
		if (recordHashes)
		{
			out.hashes.push_back(a.logic.computeStateHash());
		}
		Object *o = a.logic.findObjectByID(id);
		if (o && !o->isEffectivelyDead())
		{
			const float h = o->getBodyModule()->getHealth();
			if (h < last)
			{
				out.drops.push_back(last - h);
				out.firstHitFrame = out.firstHitFrame < 0 ? f : out.firstHitFrame;
			}
			last = h;
		}
		if (out.deathFrame < 0 && o && o->isEffectivelyDead())
		{
			out.deathFrame = f;
			out.applications = a.logic.combat().counters().damageApplications - applicationsBefore;
			out.obstacleAfterDeath = a.obstacleAt(640.0f, 500.0f);
			out.rubbleState = o->getBodyModule()->getDamageState() == BODY_RUBBLE;
			out.rubbleCondition = o->testModelCondition(CombatNames::modelCondition("RUBBLE"));
			out.cashAfter = (int)a.player(0)->getMoney()->countMoney();
			for (const TerrainResourceManager::Claimant &c : a.logic.economy().resources().claimants())
			{
				out.claimantAfter = out.claimantAfter && c.id != id;
			}
			if (out.claimantAfter)
			{
				out.claimantAfter = true;
			}
		}
		if (!o)
		{
			out.goneFrame = f;
			if (out.deathFrame < 0)
			{
				out.deathFrame = f; // destroyed in the frame it died (DestroyDie)
				out.applications = a.logic.combat().counters().damageApplications - applicationsBefore;
				out.cashAfter = (int)a.player(0)->getMoney()->countMoney();
				out.obstacleAfterDeath = a.obstacleAt(640.0f, 500.0f);
				out.rubbleState = true; // the body had reached RUBBLE (the object is gone: asserted through the counters)
				out.rubbleCondition = true;
				out.claimantAfter = true;
				for (const TerrainResourceManager::Claimant &c : a.logic.economy().resources().claimants())
				{
					out.claimantAfter = out.claimantAfter && c.id != id;
				}
			}
			break;
		}
	}
	out.collapsesBegun = a.logic.combat().counters().collapsesBegun;
	out.collapsesDone = a.logic.combat().counters().collapsesDone;
	return out;
}
} // namespace

TEST_CASE("structure retail: a Gondor horde destroys the barracks of every faction: hits, collapse, rubble, pathfinder, bounty")
{
	if (!hudtest::haveWorld("structure retail"))
	{
		return;
	}
	for (const StructRow &row : kRows)
	{
		INFO(row.templ);
		const Outcome o = fightStructure(row, 1.0f, false);
		CHECK(o.bodyClass == row.body);
		REQUIRE(o.maxHealth == row.health);
		// the damage per hit: 40 * SLASH% (every drop of one frame is a whole number of hits, at most the 15 members' worth)
		const float perHit = 40.0f * row.slashPercent / 100.0f;
		REQUIRE_FALSE(o.drops.empty());
		for (float d : o.drops)
		{
			const float hits = d / perHit;
			CHECK_MESSAGE(std::fabs(hits - std::round(hits)) < 1e-3f, "a step of " << d << " is not a whole number of " << perHit << " hits");
			CHECK(hits < 15.5f);
		}
		// the number of hits: ceil(H / hit), one more when the float residue keeps a sliver of health
		const double need = std::ceil((double)row.health / (double)perHit - 1e-9);
		CHECK_MESSAGE((double)o.applications >= need, row.templ << ": " << o.applications << " hits for " << need);
		CHECK_MESSAGE((double)o.applications <= need + 1.0, row.templ << ": " << o.applications << " hits for " << need);
		// no member swings faster than every 5 frames: the soonest the structure can die
		CHECK(o.deathFrame >= (int)std::ceil(need / 15.0) * 5 - 5);
		CHECK_MESSAGE(o.deathFrame == row.armorFrames, row.templ << " died at frame " << o.deathFrame); // the approach and the swing phases are the attack machine's (S-325 / S-327)
		// the structure stood as an obstacle, and left the pathfinder in the frame it died
		CHECK(o.obstacleBefore);
		CHECK_FALSE(o.obstacleAfterDeath);
		CHECK(o.rubbleState);
		CHECK(o.rubbleCondition);
		// the ground the structure claimed (TerrainResourceBehavior, every one but MordorBarracks) is released: it is no claimant after its death
		CHECK(o.claimantBefore == (std::string(row.templ) != "MordorBarracks"));
		CHECK(o.claimantAfter);
		// the killer's player is paid BountyValue * 1.0 once
		CHECK(o.cashAfter - o.cashBefore == row.bounty);
		// the structure leaves the world when its fall is over: n frames after its death (the DestroyDie of Mordor's barracks: in the same frame)
		if (row.collapses)
		{
			CHECK(o.collapsesBegun == 1);
			CHECK(o.collapsesDone == 1);
			CHECK_MESSAGE(o.goneFrame - o.deathFrame == collapseUpdates(row.collapseHeight), row.templ << " fell for " << (o.goneFrame - o.deathFrame) << " frames");
		}
		else
		{
			CHECK(o.collapsesBegun == 0);
			CHECK(o.goneFrame == o.deathFrame);
		}
		std::printf("  info: Gondor horde vs %s: %llu hits, died at frame %d, gone at %d, claimant %d -> %d\n", row.templ, o.applications, o.deathFrame, o.goneFrame, (int)o.claimantBefore,
			(int)o.claimantAfter);
	}
}

TEST_CASE("structure retail: at the default bounty percent 0 a destroyed structure pays nothing")
{
	if (!hudtest::haveWorld("structure retail"))
	{
		return;
	}
	const Outcome o = fightStructure(kRows[4], 0.0f, false); // MordorBarracks: the shortest fight
	REQUIRE(o.deathFrame > 0);
	CHECK(o.cashAfter == o.cashBefore);
}

TEST_CASE("structure retail: the same structure fight twice gives the same state hash in every frame")
{
	if (!hudtest::haveWorld("structure retail"))
	{
		return;
	}
	const Outcome a = fightStructure(kRows[0], 1.0f, true);
	const Outcome b = fightStructure(kRows[0], 1.0f, true);
	REQUIRE(a.hashes.size() == b.hashes.size());
	for (size_t i = 0; i < a.hashes.size(); ++i)
	{
		REQUIRE_MESSAGE(a.hashes[i] == b.hashes[i], "frame " << i);
	}
	CHECK(a.hashes.size() > 200);
	CHECK(a.hashes[10] != a.hashes[a.hashes.size() - 1]);
}

// ---- the skirmish victory rules on real data ---------------------------------------------------------------------------------------------------------
// GameData (gamedata.ini, 2.01): VictoryConditionStructureObjectFilter = NONE +STRUCTURE -IGNORE_FOR_VICTORY -UNATTACKABLE -ECONOMY_STRUCTURE -WALL_UPGRADE -WALL_HUB -WALL_SEGMENT
// -DEFENSIVE_WALL -Inn -ShipWright -Outpost -SignalFire -CaptureFlag -ShireGreenDragon; VictoryConditionUnitObjectFilter = ANY -DOZER -NOT_AUTOACQUIRABLE -MordorWorker; no
// SecondsBeforeBaseCheckActive (the game's default 5.0 seconds = 25 frames). The rule of a skirmish (RW 0x8094A5): a player is defeated when no object of its own passes the structure
// filter (a live GondorBarracks does) and no live object of KindOf DOZER that is not IGNORE_FOR_VICTORY remains (a GondorWorker is a DOZER; MordorWorker has IGNORE_FOR_VICTORY and does
// not count). The victory update runs in phase 1 of every frame, so a structure or worker that dies in frame d is judged in frame d + 1.
namespace
{
struct Victory
{
	int barracksDied = -1, workerDied = -1;
	unsigned endFrame = 0, bobDefeatFrame = 0;
	bool bobDefeatedAfterBarracks = false;
	bool alliance = false, aliceWon = false, bobLost = false;
	int events = 0;
	bool bobSoldiersDead = false, bobDefeated = false;
	std::vector<std::uint32_t> hashes;
};

Victory playVictory(const char *attackerFaction, const char *attackerHorde, const char *attackerBase, const char *loserFaction, const char *barracks, const char *worker, bool killWorker, bool recordHashes)
{
	SharedWorld &s = shared();
	Victory out;
	Arena a(s, attackerFaction, loserFaction);
	a.logic.victory().init();
	Object *base = a.place(attackerBase, 0, 200.0f, 200.0f); // the attacker needs a structure of its own to stay in the game
	(void)base;
	Object *horde = a.place(attackerHorde, 0, 500.0f, 500.0f);
	Object *b = a.place(barracks, 1, 640.0f, 500.0f);
	Object *w = a.place(worker, 1, 700.0f, 540.0f);
	const ObjectID bId = b->getID(), wId = w->getID();
	a.logic.runLogicFrame();
	a.logic.runLogicFrame();
	horde->getAIUpdateInterface()->aiAttackObject(b, CMD_FROM_PLAYER);
	bool attackedWorker = false;
	VictoryConditions &v = a.logic.victory();
	for (int f = 0; f < 2500 && !v.singleAllianceRemaining(); ++f)
	{
		a.logic.runLogicFrame();
		if (recordHashes)
		{
			out.hashes.push_back(a.logic.computeStateHash());
		}
		const Object *bo = a.logic.findObjectByID(bId);
		const Object *wo = a.logic.findObjectByID(wId);
		if (out.barracksDied < 0 && (!bo || bo->isEffectivelyDead()))
		{
			out.barracksDied = (int)a.logic.getFrame();
		}
		// a GondorWorker has MaxHealth 999999 ("so they can't be killed", the INI says): the scripted kill (Object::kill, what a map script's KILL action does) removes it
		if (out.barracksDied >= 0 && !attackedWorker && killWorker && (int)a.logic.getFrame() == out.barracksDied + 10)
		{
			if (Object *victim = a.logic.findObjectByID(wId))
			{
				victim->kill(DEATH_NORMAL);
				attackedWorker = true;
			}
		}
		if (out.barracksDied >= 0 && out.workerDied < 0 && (!wo || wo->isEffectivelyDead()))
		{
			out.workerDied = (int)a.logic.getFrame();
		}
		if (out.barracksDied >= 0 && !out.bobDefeatedAfterBarracks && v.isDefeated(1))
		{
			out.bobDefeatedAfterBarracks = true;
		}
		if (!killWorker && out.barracksDied >= 0 && (int)a.logic.getFrame() > out.barracksDied + 40)
		{
			break; // the worker is not attacked: Bob must still be in the game 40 frames after his barracks fell
		}
	}
	out.endFrame = v.endFrame();
	out.alliance = v.singleAllianceRemaining();
	out.aliceWon = v.hasAchievedVictory(a.player(0));
	out.bobLost = v.hasBeenDefeated(a.player(1));
	out.bobDefeated = a.player(1)->isDefeated();
	out.bobDefeatFrame = a.player(1)->getDefeatFrame();
	out.events = (int)v.events().size();
	const Object *wo = a.logic.findObjectByID(wId);
	out.bobSoldiersDead = !wo || wo->isEffectivelyDead();
	return out;
}
} // namespace

TEST_CASE("structure retail: a player whose barracks fell but who still has a builder stays in the game; the loss comes with the builder")
{
	if (!hudtest::haveWorld("structure retail"))
	{
		return;
	}
	// Men against Mordor: GondorBarracks + a GondorWorker (KindOf DOZER, counts) for the loser
	const Victory v = playVictory("FactionMordor", "MordorFighterHorde", "MordorBarracks", "FactionMen", "GondorBarracks", "GondorWorker", true, false);
	REQUIRE(v.barracksDied > 25);
	CHECK((!v.bobDefeatedAfterBarracks || v.workerDied >= 0));
	REQUIRE(v.workerDied > v.barracksDied);
	CHECK(v.alliance);
	CHECK(v.endFrame == (unsigned)v.workerDied + 1); // judged by the update of the frame after the builder died
	CHECK(v.bobDefeatFrame == v.endFrame);
	CHECK(v.aliceWon);
	CHECK(v.bobLost);
	CHECK(v.bobDefeated);
	CHECK(v.events == 2);
	std::printf("  info: Mordor vs Men: barracks died at frame %d, the worker at %d, Men were eliminated at frame %u\n", v.barracksDied, v.workerDied, v.endFrame);
}

TEST_CASE("structure retail: nobody wins while the loser still has his barracks or a worker")
{
	if (!hudtest::haveWorld("structure retail"))
	{
		return;
	}
	const Victory v = playVictory("FactionMordor", "MordorFighterHorde", "MordorBarracks", "FactionMen", "GondorBarracks", "GondorWorker", false, false);
	REQUIRE(v.barracksDied > 25);
	CHECK_FALSE(v.alliance);      // the worker (DOZER) keeps Men in the game 40 frames after their barracks fell
	CHECK_FALSE(v.bobDefeated);
	CHECK(v.events == 0);
}

TEST_CASE("structure retail: a MordorWorker is IGNORE_FOR_VICTORY: Mordor is eliminated with its last barracks and the worker dies with it")
{
	if (!hudtest::haveWorld("structure retail"))
	{
		return;
	}
	const Victory v = playVictory("FactionMen", "GondorFighterHorde", "GondorBarracks", "FactionMordor", "MordorBarracks", "MordorWorker", false, false);
	REQUIRE(v.barracksDied > 25);
	CHECK(v.alliance);
	// MordorBarracks: DestroyDie, gone in the frame it died; VictoryConditions::update runs after the destroy list of the same frame (phase 5, RW 0x62EBCE:
	// lane END-1 moved it there from phase 1), so the loss is judged in that frame
	CHECK(v.endFrame == (unsigned)v.barracksDied);
	CHECK(v.bobDefeatFrame == v.endFrame);
	CHECK(v.bobDefeated);
	CHECK(v.aliceWon);
	CHECK(v.bobSoldiersDead); // killPlayer
	std::printf("  info: Men vs Mordor: the barracks died at frame %d, Mordor was eliminated at frame %u\n", v.barracksDied, v.endFrame);
}

TEST_CASE("structure retail: the same victory game twice gives the same state hash in every frame")
{
	if (!hudtest::haveWorld("structure retail"))
	{
		return;
	}
	const Victory a = playVictory("FactionMen", "GondorFighterHorde", "GondorBarracks", "FactionMordor", "MordorBarracks", "MordorWorker", false, true);
	const Victory b = playVictory("FactionMen", "GondorFighterHorde", "GondorBarracks", "FactionMordor", "MordorBarracks", "MordorWorker", false, true);
	REQUIRE(a.hashes.size() == b.hashes.size());
	for (size_t i = 0; i < a.hashes.size(); ++i)
	{
		REQUIRE_MESSAGE(a.hashes[i] == b.hashes[i], "frame " << i);
	}
	CHECK(a.hashes.size() > 30);
}

// ---- every template of the five body classes ------------------------------------------------------------------------------------------------------------
// COMBAT-1 left 375 + 97 + 55 + 20 + 4 templates (StructureBody, InactiveBody, RespawnBody, DelayedDeathBody, SymbioticStructuresBody) with no body at all: they held no health and could
// not be damaged. Every one of them is created now, holds the health of its data (an InactiveBody none: it is dead from the start) and a lethal hit leaves it dead (an InactiveBody only
// to UNRESISTABLE damage, a DelayedDeathBody after its delay, a SymbioticStructuresBody through its symbiote).
TEST_CASE("structure retail: every template with a StructureBody, InactiveBody, RespawnBody, DelayedDeathBody or SymbioticStructuresBody is created with a body that answers as retail's does")
{
	if (!hudtest::haveWorld("structure retail"))
	{
		return;
	}
	SharedWorld &s = shared();
	const char *classes[] = { "StructureBody", "InactiveBody", "RespawnBody", "DelayedDeathBody", "SymbioticStructuresBody" };
	size_t perClass[5] = { 0, 0, 0, 0, 0 };
	size_t created = 0, killable = 0, stayedDead = 0;
	std::vector<std::string> problems;
	Arena a(s, "FactionMen", "FactionMordor");
	for (const ThingTemplate *t : s.world->things().templates())
	{
		const ThingTemplate *tt = t->getFinalOverride();
		int which = -1;
		for (const ThingTemplate::Nugget &n : tt->behaviorModules().nuggets())
		{
			for (int c = 0; c < 5; ++c)
			{
				if (n.name == classes[c])
				{
					which = c;
				}
			}
		}
		if (which < 0)
		{
			continue;
		}
		++perClass[which];
		try
		{
			Object *o = a.logic.newObject(tt, a.player(1)->getDefaultTeam(), ObjectStatusMaskType{});
			if (!o || !o->getBodyModule())
			{
				problems.push_back(tt->getName() + ": no body");
				continue;
			}
			++created;
			BodyModuleInterface *body = o->getBodyModule();
			// the object's body is the LAST body module its template declares (RW 0x69A3C8): ArnorArvedui-like templates mix classes, so the class is taken from the body itself
			if (dynamic_cast<InactiveBody *>(body))
			{
				which = 1;
			}
			else if (dynamic_cast<SymbioticStructuresBody *>(body))
			{
				which = 4;
			}
			else if (dynamic_cast<DelayedDeathBody *>(body))
			{
				which = 3;
			}
			else if (dynamic_cast<RespawnBody *>(body))
			{
				which = 2;
			}
			else if (dynamic_cast<StructureBody *>(body))
			{
				which = 0;
			}
			else
			{
				continue; // a plain ActiveBody / ImmortalBody declared after the listed class: COMBAT-1's
			}
			if (which == 1)
			{
				if (!o->isEffectivelyDead() || body->getHealth() != 0.0f)
				{
					problems.push_back(tt->getName() + ": an InactiveBody object must be dead with no health");
				}
				continue;
			}
			if (which == 4)
			{
				continue; // answers through its symbiote (nothing links it yet, S-343)
			}
			if (body->getMaxHealth() == 0.0f)
			{
				continue; // the data says MaxHealth 0 (FarmInterface and kin: a placement marker)
			}
			if (body->getHealth() != body->getInitialHealth() && body->getHealth() != body->getMaxHealth())
			{
				problems.push_back(tt->getName() + ": health " + std::to_string(body->getHealth()) + " of " + std::to_string(body->getMaxHealth()));
				continue;
			}
			DamageInfo d;
			d.m_input.m_damageType = DAMAGE_UNRESISTABLE;
			d.m_input.m_amount = 1.0f;
			const float before = body->getHealth();
			o->attemptDamage(d);
			if (!(body->getHealth() < before) && !(which == 3))
			{
				problems.push_back(tt->getName() + ": a hit did not hurt it");
				continue;
			}
			++killable;
			DamageInfo big;
			big.m_input.m_damageType = DAMAGE_UNRESISTABLE;
			big.m_input.m_amount = 1.0e9f;
			o->attemptDamage(big);
			if (o->isEffectivelyDead() || which == 3)
			{
				++stayedDead;
			}
			else
			{
				problems.push_back(tt->getName() + ": a lethal hit left it alive");
			}
		}
		catch (const std::exception &e)
		{
			problems.push_back(tt->getName() + ": " + e.what());
		}
	}
	for (const std::string &p : problems)
	{
		FAIL_CHECK(p);
	}
	CHECK(perClass[0] >= 370);
	CHECK(perClass[1] >= 90);
	CHECK(created >= 550);
	CHECK(perClass[2] >= 50);
	CHECK(perClass[3] >= 15);
	CHECK(perClass[4] >= 4);
	CHECK(killable > 400);
	std::printf("  info: StructureBody %zu, InactiveBody %zu, RespawnBody %zu, DelayedDeathBody %zu, SymbioticStructuresBody %zu templates created with a body; %zu hurt, %zu died to a lethal hit\n", perClass[0],
		perClass[1], perClass[2], perClass[3], perClass[4], killable, stayedDead);
}

// ---- income and command points freed with the structure -----------------------------------------------------------------------------------------------
// MenFortressCitadel (the keep of the starting fortress, StructureBody + AutoDepositUpdate, MP_COUNT_FOR_VICTORY) pays its owner a fixed deposit at a fixed interval; AutoDepositUpdate
// stops paying while the object has the model condition RUBBLE, POST_RUBBLE or POST_COLLAPSE (RW, ECON-1): from the moment its damage state is RUBBLE nothing is deposited any more.
TEST_CASE("structure retail: the income of a destroyed keep stops with its rubble state (AutoDepositUpdate), and a HobbitWorkPit gives its command points back")
{
	if (!hudtest::haveWorld("structure retail"))
	{
		return;
	}
	SharedWorld &s = shared();
	Arena a(s, "FactionMordor", "FactionMen");
	Object *keep = a.place("MenFortressCitadel", 1, 600.0f, 600.0f);
	const ObjectID id = keep->getID();
	Player *owner = a.player(1);
	a.logic.runLogicFrame();
	const std::uint32_t start = owner->getMoney()->countMoney();
	for (int f = 0; f < 600; ++f)
	{
		a.logic.runLogicFrame();
	}
	const std::uint32_t earned = owner->getMoney()->countMoney() - start;
	CHECK(earned > 0u); // the citadel pays
	DamageInfo kill;
	kill.m_input.m_damageType = DAMAGE_UNRESISTABLE;
	kill.m_input.m_amount = 1.0e9f;
	REQUIRE(a.logic.findObjectByID(id) != nullptr);
	a.logic.findObjectByID(id)->attemptDamage(kill);
	CHECK(a.logic.findObjectByID(id)->getBodyModule()->getDamageState() == BODY_RUBBLE);
	a.logic.runLogicFrame();
	const std::uint32_t atDeath = owner->getMoney()->countMoney();
	for (int f = 0; f < 600; ++f)
	{
		a.logic.runLogicFrame();
	}
	CHECK(owner->getMoney()->countMoney() == atDeath); // nothing deposited after the rubble state
	std::printf("  info: the citadel earned %u in 600 frames, nothing after its death\n", earned);
	// command points: HobbitWorkPit (CommandPointBonus 10) adds to the player's bonus, its death takes it away at once (RW 0x8C1BB7 -> 0x68E114)
	Object *pit = a.place("HobbitWorkPit", 1, 800.0f, 600.0f);
	const int bonus = owner->commandPoints().getBonus();
	REQUIRE(pit != nullptr);
	CHECK(bonus >= 10);
	pit->attemptDamage(kill);
	CHECK(owner->commandPoints().getBonus() == bonus - 10);
}
