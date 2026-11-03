// OpenBFME. Lane TARGET-1 tests: RotWK's target search (AI::findClosestEnemy RW 0x7006D7), the attack priority table (RW 0x605938 / 0x7597E9 / 0x758B68), the mood
// scan's cadence and ordered melee target (RW 0x66844A) and the AIData rows. Synthetic data (combattest::CombatWorld), no retail files. Every expectation is a
// consequence of the cited retail rule (the arithmetic is written out where a value is asserted).
#include "doctest.h"
#include "CombatTestUtil.h"

#include "GameLogic/AI/AIClosestEnemy.h"
#include "GameLogic/AI/AIAttackMelee.h"
#include "GameLogic/Combat/TargetFinder.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/ScriptEngine/AttackPriority.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"
#include "GameLogic/SkirmishAI/SkirmishAIData.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"

using namespace combattest;

namespace
{
const char kTargetObjects[] =
	"Object Pebble\n"
	"  KindOf = INFANTRY SELECTABLE NOT_AUTOACQUIRABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = PlainArmor\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"End\n"
	"Object Horseman\n"
	"  KindOf = CAVALRY SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  ThreatBreakdown\n"
	"    AIKindOf = CAVALRY\n"
	"  End\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = PlainArmor\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"End\n"
	"Object Bowman\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  ThreatBreakdown\n"
	"    AIKindOf = ARCHER\n"
	"  End\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = PlainArmor\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"End\n"
	"Object ChainSwordsman\n"
	"  KindOf = INFANTRY SELECTABLE CAN_ATTACK\n"
	"  VisionRange = 150\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  ThreatBreakdown\n"
	"    AIKindOf = INFANTRY\n"
	"  End\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = PlainArmor\n"
	"  End\n"
	"  WeaponSet\n"
	"    Conditions = None\n"
	"    Weapon = PRIMARY SwordWeapon\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"    AutoAcquireEnemiesWhenIdle = Yes\n"
	"    MoodAttackCheckRate = 400\n"
	"    AttackPriority = Picky\n"
	"  End\n"
	"  Behavior = PhysicsBehavior ModuleTag_Physics\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n";

const unsigned kIdleScan = AI::CAN_ATTACK | AI::ATTACK_BUILDINGS | AI::AUTO_ACQUIRE;

struct TargetWorld : CombatWorld
{
	TargetWorld()
		: CombatWorld(kTargetObjects)
	{
		combat().setAutoAcquireEnabled(false); // the tests drive the scans themselves
	}
	const AttackPriorityInfo *info(const char *name) { return logic->scriptEngine().attackPriorities().getAttackInfo(name); }
};
} // namespace

TEST_CASE("target1 aidata: the target search rows and the AttackPriority blocks (RW 0x70153B: a new block at the list head, a repeated one re-parsed)")
{
	const char *text = "AIData\n"
	                   "  EnableRepulsors = Yes\n"
	                   "  RepulsedDistance = 100\n"
	                   "  ForceIdleMSEC = 67\n"
	                   "  AttackPriorityDistanceModifier = 80.0\n"
	                   "  AttackUsesLineOfSight = No\n"
	                   "  AttackIgnoreInsignificantBuildings = Yes\n"
	                   "  ArtilleryMaxFriendlyFire = 50%\n"
	                   "  AttackPriority First\n"
	                   "    Default = 35\n"
	                   "  End\n"
	                   "  AttackPriority Second\n"
	                   "    Default = 10\n"
	                   "    Target = Dummy 50\n"
	                   "    Target = Dummy 70\n"
	                   "  End\n"
	                   "  AttackPriority First\n"
	                   "    Target = Swordsman 5\n"
	                   "  End\n"
	                   "End\n";
	GameLogicSettings s;
	std::string err;
	REQUIRE_MESSAGE(GameLogicSettingsLoader::scanAIData(text, s, &err), err);
	CHECK(s.forceIdleFrames == 1u);                      // parseDurationUnsignedInt: ceil(67 * 0.005) = ceil(0.335)
	CHECK(s.attackPriorityDistanceModifier == 80.0f);
	CHECK_FALSE(s.attackUsesLineOfSight);
	CHECK(s.attackIgnoreInsignificantBuildings);
	CHECK(s.artilleryMaxFriendlyFire == 0.5f);          // parsePercentToReal
	REQUIRE(s.attackPriorityDefs.size() == 2u);
	CHECK(s.attackPriorityDefs[0].name == "Second");     // pushed at the head after First
	CHECK(s.attackPriorityDefs[0].defaultPriority == 10);
	REQUIRE(s.attackPriorityDefs[0].targets.size() == 2u);
	CHECK(s.attackPriorityDefs[1].name == "First");
	CHECK(s.attackPriorityDefs[1].defaultPriority == 35); // the repeated block keeps Default and replaces the targets (RW 0x75E707)
	REQUIRE(s.attackPriorityDefs[1].targets.size() == 1u);
	CHECK(s.attackPriorityDefs[1].targets[0].first == "Swordsman");
	CHECK(s.attackPriorityDefs[1].targets[0].second == 5);
}

TEST_CASE("target1 priority table: getAttackInfo builds an info from its AIData block once, an unknown name answers the default info (RW 0x605938)")
{
	TargetWorld w;
	GameLogicSettings::AttackPriorityDef second;
	second.name = "Second";
	second.defaultPriority = 10;
	second.targets = { { "Dummy", 50 }, { "Dummy", 70 }, { "NoSuchThing", 9 } };
	w.logic->settings().attackPriorityDefs.insert(w.logic->settings().attackPriorityDefs.begin(), second);
	AttackPriorityStore &store = w.logic->scriptEngine().attackPriorities();
	const AttackPriorityInfo *a = store.getAttackInfo("Second");
	CHECK(store.getAttackInfo("Second") == a); // found by name the second time, not made again
	Object *hunter = w.unit("Swordsman", 'A', 300, 300);
	Object *dummy = w.unit("Dummy", 'B', 400, 300);
	Object *sword = w.unit("Swordsman", 'B', 500, 300);
	// RW keeps the Target lines front-inserted and sets them in list order: the FIRST line of a template wins (50, not 70)
	CHECK(a->getPriority(*hunter, *dummy, false) == 50.0f);
	CHECK(a->getPriority(*hunter, *sword, false) == 10.0f); // no entry: the default
	const AttackPriorityInfo *unknown = store.getAttackInfo("Nobody");
	CHECK(unknown == store.getDefaultAttackInfo());
	CHECK(unknown->getPriority(*hunter, *dummy, false) == 1.0f); // RW 0x758B1F: the default info's priority is 1
	CHECK(store.getAttackInfo("DefaultAttackPriority")->getPriority(*hunter, *dummy, false) == 35.0f); // retail aidata.ini's block
}

TEST_CASE("target1 priority table: the combat chain adjusts a hunter's priority by its ThreatBreakdown AIKindOf (RW 0x758B68)")
{
	TargetWorld w;
	SkirmishAIStore store{ w.w.keys };
	AICombatChain &chain = store.data().combatChains[0]; // INFANTRY (RW 0xDB55C8: INFANTRY, ARCHER, PIKEMAN, CAVALRY, ...)
	chain.targetTypes[0] = 3;                            // CAVALRY
	chain.targetPriorityModifiers[0] = 40.0f;
	chain.targetTypes[1] = 1;                            // ARCHER
	chain.targetPriorityModifiers[1] = -1.0f;            // RW 0xBD19DC: never
	w.logic->skirmishAI().setStore(&store);
	Object *hunter = w.unit("ChainSwordsman", 'A', 300, 300);
	Object *horse = w.unit("Horseman", 'B', 400, 300);
	Object *bow = w.unit("Bowman", 'B', 400, 340);
	Object *dummy = w.unit("Dummy", 'B', 400, 380);
	const AttackPriorityInfo *info = w.info("DefaultAttackPriority");
	// 35 + 40 / 80 (x87: fld m; fdiv modifier; fadd priority)
	CHECK(info->getPriority(*hunter, *horse, true) == 35.5f);
	CHECK(info->getPriority(*hunter, *bow, true) == 0.0f);
	// a target without a ThreatBreakdown (-1) matches no TargetTypes entry set here: the first unused entry (-1) gives its modifier 0
	CHECK(info->getPriority(*hunter, *dummy, true) == 35.0f);
	// a hunter without a ThreatBreakdown keeps the priority
	Object *plain = w.unit("Swordsman", 'A', 300, 400);
	CHECK(info->getPriority(*plain, *horse, true) == 35.0f);
	w.logic->skirmishAI().setStore(nullptr);
}

TEST_CASE("target1 search: the scored search keeps the first of equal float32 scores in partition order, so a later, minutely farther enemy wins (RW 0x7006D7)")
{
	TargetWorld w;
	Object *a = w.unit("Swordsman", 'A', 300, 300);
	// the older enemy's edge distance is 116 - 8 - 8 = 100; the newer one's 116.000488 - 16 = 100.000488 (416.0005f is 416.000488)
	Object *older = w.unit("Dummy", 'B', 416, 300);
	Object *newer = w.unit("Dummy", 'B', 300, 416.0005f);
	w.frames(2);
	const AttackPriorityInfo *info = w.info("DefaultAttackPriority");
	// score = (35 + 100) - d / 80 in float32: 133.75 for the older; 135 - 1.2500061 = 133.7499939, which rounds to 133.75 (half an ulp at 133.75 is 7.6e-6).
	// The partition lists the newer object first in their node (RW 0xA3B220 links at the head) and the tie (same priority) keeps the first
	CHECK(AI::findClosestEnemy(*a, 150.0f, kIdleScan, info, nullptr, 0) == newer);
	// a real distance difference decides: 20 farther costs 0.25
	const Coord3D moved{ 300.0f, 436.0f, 0.0f };
	newer->setPosition(&moved);
	w.frames(1);
	CHECK(AI::findClosestEnemy(*a, 150.0f, kIdleScan, info, nullptr, 0) == older);
}

TEST_CASE("target1 search: a higher attack priority outweighs distance at AttackPriorityDistanceModifier units per point")
{
	TargetWorld w;
	GameLogicSettings::AttackPriorityDef picky;
	picky.name = "Picky";
	picky.defaultPriority = 35;
	picky.targets = { { "Horseman", 37 } };
	w.logic->settings().attackPriorityDefs.insert(w.logic->settings().attackPriorityDefs.begin(), picky);
	Object *a = w.unit("ChainSwordsman", 'A', 300, 300); // AttackPriority = Picky, no combat chain store (no skirmish data: modifiers 0)
	Object *near = w.unit("Dummy", 'B', 366, 300);     // edge 50: 135 - 0.625 = 134.375
	Object *far = w.unit("Horseman", 'B', 300, 426);   // edge 110: 137 - 1.375 = 135.625
	w.frames(2);
	REQUIRE(a->getAIUpdateInterface()->attackInfo() == w.info("Picky"));
	CHECK(AI::findClosestEnemy(*a, 150.0f, kIdleScan, a->getAIUpdateInterface()->attackInfo(), nullptr, 0) == far);
	// 2 points are 160 units: a Horseman 170 farther than the Dummy loses (37 + 100 - 220 / 80 = 134.25 < 134.375); out of the 150 range it is not seen at all,
	// so the range is widened for the comparison
	const Coord3D moved{ 300.0f, 536.0f, 0.0f };
	far->setPosition(&moved);
	w.frames(1);
	CHECK(AI::findClosestEnemy(*a, 300.0f, kIdleScan | AI::NO_VISION_TEST, a->getAIUpdateInterface()->attackInfo(), nullptr, 0) == near);
}

TEST_CASE("target1 search: the filters - NOT_AUTOACQUIRABLE with AUTO_ACQUIRE, DO_NOT_PICK_ME, FLEE_OFF_MAP, allies; no info: the partition's closest object")
{
	TargetWorld w;
	Object *a = w.unit("Swordsman", 'A', 300, 300);
	Object *pebble = w.unit("Pebble", 'B', 340, 300); // nearest
	Object *dummy = w.unit("Dummy", 'B', 380, 300);
	w.unit("Dummy", 'A', 320, 300); // an ally, nearer still
	w.frames(2);
	const AttackPriorityInfo *info = w.info("DefaultAttackPriority");
	CHECK(AI::findClosestEnemy(*a, 150.0f, kIdleScan, info, nullptr, 0) == dummy);                    // RW 0xBD88B0: NOT_AUTOACQUIRABLE refused
	CHECK(AI::findClosestEnemy(*a, 150.0f, kIdleScan & ~AI::AUTO_ACQUIRE, info, nullptr, 0) == pebble); // not linked without AUTO_ACQUIRE
	dummy->setStatus((unsigned)CombatNames::status("DO_NOT_PICK_ME"), true);
	CHECK(AI::findClosestEnemy(*a, 150.0f, kIdleScan, info, nullptr, 0) == nullptr); // the scored loop skips status 0x4F
	dummy->setStatus((unsigned)CombatNames::status("DO_NOT_PICK_ME"), false);
	dummy->setStatus((unsigned)CombatNames::status("FLEE_OFF_MAP"), true);
	CHECK(AI::findClosestEnemy(*a, 150.0f, kIdleScan, info, nullptr, 0) == nullptr); // RW 0x6FF651: FLEE_OFF_MAP without DESTROYED
	dummy->setStatus((unsigned)CombatNames::status("FLEE_OFF_MAP"), false);
	// without an info: ThePartitionManager's closest object (RW 0xA39090, 3D centres) over a region (the game's map sets one)
	w.logic->partition().setRegion(0.0f, 0.0f, 1000.0f, 1000.0f);
	CHECK(AI::findClosestEnemy(*a, 150.0f, kIdleScan & ~AI::AUTO_ACQUIRE, nullptr, nullptr, 0) == pebble);
	CHECK(AI::findClosestEnemy(*a, 150.0f, kIdleScan, nullptr, nullptr, 0) == dummy);
}

TEST_CASE("target1 mood scan: the cadence - resetNextMoodCheckTime (RW 0x662DD9), setNextMoodCheckTime (RW 0x662E01) and the scan's MoodAttackCheckRate (RW 0x6686C6)")
{
	TargetWorld w;
	w.combat().setAutoAcquireEnabled(true);
	Object *a = w.unit("Swordsman", 'A', 300, 300);
	w.frames(2);
	AIUpdateInterface *ai = a->getAIUpdateInterface();
	const unsigned now = w.logic->getFrame();
	ai->setNextMoodCheckTime(now + 50);
	CHECK_FALSE(ai->randomlyOffsetMoodCheck());
	ai->resetNextMoodCheckTime(); // never earlier: the later check stays
	CHECK(ai->nextMoodCheckTime() == now + 50);
	CHECK(ai->randomlyOffsetMoodCheck());
	ai->setNextMoodCheckTime(now);
	CHECK_FALSE(ai->randomlyOffsetMoodCheck());
	ai->resetNextMoodCheckTime(); // ForceIdleMSEC (1 frame) from now
	CHECK(ai->nextMoodCheckTime() == now + 1);
	// a scan called by the AI before its time does nothing and keeps the time
	CHECK(ai->getNextMoodTarget(true, true) == nullptr);
	CHECK(ai->nextMoodCheckTime() == now + 1);
	// at its time without the offset flag: MoodAttackCheckRate 400 ms = 2 frames from now
	ai->setNextMoodCheckTime(now);
	CHECK(ai->getNextMoodTarget(true, true) == nullptr); // nothing in range
	CHECK(ai->nextMoodCheckTime() == now + 2);
	// with the flag the offset is drawn once and the flag clears
	ai->resetNextMoodCheckTime();
	ai->setNextMoodCheckTime(now); // clears it again: no draw
	CHECK_FALSE(ai->randomlyOffsetMoodCheck());
}

TEST_CASE("target1 mood scan: a player's attack order with a melee weapon is remembered (AI + 0x1A4) and answered by the mood scan while the target lives (RW 0x66854F)")
{
	TargetWorld w;
	w.combat().setAutoAcquireEnabled(true);
	Object *a = w.unit("Swordsman", 'A', 300, 300);
	Object *v = w.unit("Dummy", 'B', 800, 300); // far outside the vision range (150)
	w.frames(2);
	AIUpdateInterface *ai = a->getAIUpdateInterface();
	REQUIRE(ai->aiAttackObject(v, CMD_FROM_PLAYER));
	CHECK(ai->orderedMeleeTarget() == v->getID());
	ai->aiIdle(CMD_FROM_PLAYER);
	CHECK(ai->getNextMoodTarget(false, true) == v);
	v->kill(DAMAGE_UNRESISTABLE, DEATH_NORMAL);
	w.frames(1);
	CHECK(ai->getNextMoodTarget(false, true) == nullptr); // the dead target is dropped and nothing else is within the vision range
	CHECK(ai->orderedMeleeTarget() == INVALID_ID); // forgotten once dead (+ 0x458 bit 0)
	// an AI order does not set it
	Object *v2 = w.unit("Dummy", 'B', 340, 300);
	w.frames(1);
	REQUIRE(ai->aiAttackObject(v2, CMD_FROM_AI));
	CHECK(ai->orderedMeleeTarget() == INVALID_ID);
}

TEST_CASE("BFME2 building filters use scan flags and player type, not capturable flags")
{
	combattest::CombatWorld w(
		"ChildObject CaptureBuilding Dummy\n KindOf = STRUCTURE CAPTURABLE\nEnd\n"
		"ChildObject LinkedBuilding Dummy\n KindOf = STRUCTURE LINKED_TO_FLAG\nEnd\n"
		"ChildObject DefenseBuilding Dummy\n KindOf = STRUCTURE FS_BASE_DEFENSE\nEnd\n"
		"ChildObject ArmedContainer Slasher\n KindOf = STRUCTURE CAN_ATTACK\n"
		" Behavior = GarrisonContain ModuleTag_Contain\n ContainMax = 4\n End\nEnd\n"
		"ChildObject MineBuilding Dummy\n KindOf = STRUCTURE MINE\nEnd\n");
	w.combat().setAutoAcquireEnabled(false);
	auto *unit = w.unit("Slasher", 'A', 200, 200);
	auto *capture = w.unit("CaptureBuilding", 'B', 220, 200);
	auto *linked = w.unit("LinkedBuilding", 'B', 240, 200);
	auto *defense = w.unit("DefenseBuilding", 'B', 260, 200);
	auto &finder = w.combat().targets();
	w.playerOf('A')->setPlayerType(PLAYER_HUMAN, true);
	CHECK(finder.findClosestEnemy(*unit, 200, 0, CMD_FROM_AI) == defense);
	CHECK(finder.findClosestEnemy(*unit, 200, TargetFinder::ALLOW_STRUCTURES, CMD_FROM_AI) == capture);
	CHECK_FALSE(AI::allowsBuildingTarget(*unit, *linked));
	auto *armed = w.unit("ArmedContainer", 'B', 280, 200);
	auto *mine = w.unit("MineBuilding", 'B', 300, 200);
	CHECK(AI::allowsBuildingTarget(*unit, *armed));
	CHECK(AI::allowsBuildingTarget(*unit, *mine));
	w.playerOf('A')->setPlayerType(PLAYER_COMPUTER, true);
	CHECK(finder.findClosestEnemy(*unit, 200, 0, CMD_FROM_AI) == capture);
	CHECK(AI::allowsBuildingTarget(*unit, *linked));
	CHECK(unit->getWeapons()->canAttackObject(*capture, CMD_FROM_PLAYER, false));
}

TEST_CASE("melee reacquire refuses NOT_AUTOACQUIRABLE buildings and units for computer players")
{
	CombatWorld w(
		"ChildObject Wall Dummy\n KindOf = STRUCTURE NOT_AUTOACQUIRABLE\nEnd\n"
		"ChildObject HiddenUnit Dummy\n KindOf = INFANTRY NOT_AUTOACQUIRABLE\nEnd\n");
	w.combat().setAutoAcquireEnabled(false);
	w.playerOf('A')->setPlayerType(PLAYER_COMPUTER, true);
	auto *unit = w.unit("Slasher", 'A', 200, 200);
	auto *wall = w.unit("Wall", 'B', 210, 200);
	auto *hidden = w.unit("HiddenUnit", 'B', 220, 200);
	auto *target = w.unit("Dummy", 'B', 230, 200);
	w.frames(1);
	REQUIRE(AI::allowsBuildingTarget(*unit, *wall)); // computer exemption must not bypass the kind filter
	REQUIRE(unit->getWeapons()->canAttackObject(*hidden, CMD_FROM_PLAYER, false));
	auto *ai = unit->getAIUpdateInterface();
	REQUIRE(ai);
	AIStateMachine machine(*ai, "ReacquireTest");
	AIMeleeReAcquireState state(machine);
	CHECK(state.onEnter() == STATE_SUCCESS);
	CHECK(ai->currentVictim() == target);
	CHECK(machine.goalObjectID() == target->getID());
}

TEST_CASE("BFME2 insignificant building filter keeps occupied garrisons and faction structures")
{
	combattest::CombatWorld w(
		"ChildObject EmptyInn Dummy\n KindOf = STRUCTURE CAPTURABLE\n RemoveModule ModuleTag_AI\n"
		" Behavior = GarrisonContain ModuleTag_Contain\n ContainMax = 4\n End\nEnd\n"
		"ChildObject FactionInn EmptyInn\n KindOf = STRUCTURE CAPTURABLE FS_FACTORY\nEnd\n"
		"ChildObject ActiveInn EmptyInn\n Behavior = AIUpdateInterface ModuleTag_AI\n End\nEnd\n"
		"ChildObject BareBuilding Dummy\n KindOf = STRUCTURE LINKED_TO_FLAG\n RemoveModule ModuleTag_AI\nEnd\n");
	w.combat().setAutoAcquireEnabled(false);
	auto *unit = w.unit("Slasher", 'A', 200, 200);
	auto *inn = w.unit("EmptyInn", 'B', 220, 200);
	auto *faction = w.unit("FactionInn", 'B', 240, 200);
	auto *active = w.unit("ActiveInn", 'B', 260, 200);
	auto *bare = w.unit("BareBuilding", 'B', 280, 200);
	REQUIRE(inn->getContain());
	CHECK(inn->getAIUpdateInterface() == nullptr);
	CHECK_FALSE(AI::isSignificantBuildingTarget(*inn));
	CHECK(AI::isSignificantBuildingTarget(*faction));
	CHECK(AI::isSignificantBuildingTarget(*active));
	CHECK(AI::isSignificantBuildingTarget(*bare));
	auto &finder = w.combat().targets();
	CHECK(finder.findClosestEnemy(*unit, 200, TargetFinder::ALLOW_STRUCTURES, CMD_FROM_AI) == inn);
	const unsigned filtered = TargetFinder::ALLOW_STRUCTURES | TargetFinder::IGNORE_INSIGNIFICANT_BUILDINGS;
	for (auto owner : {PLAYER_HUMAN, PLAYER_COMPUTER})
	{
		w.playerOf('A')->setPlayerType(owner, true);
		CHECK(finder.findClosestEnemy(*unit, 200, filtered, CMD_FROM_AI) == faction);
	}
	auto *rider = w.unit("Slasher", 'B', 300, 200);
	REQUIRE(inn->getContain()->addToContain(rider));
	CHECK(AI::isSignificantBuildingTarget(*inn));
	CHECK(finder.findClosestEnemy(*unit, 200, filtered, CMD_FROM_AI) == inn);
}
