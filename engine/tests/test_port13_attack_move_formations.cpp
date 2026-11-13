// OpenBFME unit tests. GPL-3.0.
// Lane PORT-13 (issue #68, stop S-4463): AIData/AttackMoveUsesFormations (+ 0xBA, RW 0xC1CE50 / 0x6FFD9E), AIGroup::groupAttackMoveToPosition (RW 0x774E2F),
// AIGroup::moveFormationToPos mode 1 (RW 0x77348C, RW 0x773D4D .. 0x773DD1), AI command 0x25 (RW 0x77122F -> RW 0x66BC68), AIUpdateInterface::isAttacking
// (RW 0x662B3C -> State vslot 0x24 RW 0x7427EA), and state 61 (0x3D) on AIFollowPathAsTeamState (RW 0x74FC9B / 0x74FD39 / 0x74FF49 / 0x74B800 / 0x742721).

#include "doctest.h"

#include "CombatTestUtil.h"
#include "PeImage.h"
#include "RetailTestMount.h"

#include "Common/SnapshotProbe.h"
#include "Common/StateHash.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/AI/AIFollowPathAsTeam.h"
#include "GameLogic/AI/AIGroup.h"
#include "GameLogic/GameLogic.h"

#include <cmath>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace
{
const char *const kPort13ExtraUnits =
	"Locomotor AirLoco\n"
	"  Surfaces = AIR\n"
	"  TurnTime = 500\n"
	"  TurnTimeDamaged = 500\n"
	"  Acceleration = 510\n"
	"  Braking = 510\n"
	"  MinSpeed = 0\n"
	"  ZAxisBehavior = SURFACE_RELATIVE_HEIGHT\n"
	"  PreferredHeight = 60\n"
	"  Appearance = WINGS\n"
	"  StickToGround = No\n"
	"  CloseEnoughDist = 5\n"
	"  FormationPriority = RANGED1\n"
	"End\n"
	"Object AirFighter\n"
	"  KindOf = VEHICLE AIRCRAFT SELECTABLE CAN_ATTACK SCORE\n"
	"  VisionRange = 220\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = PlainArmor\n"
	"  End\n"
	"  WeaponSet\n"
	"    Conditions = None\n"
	"    Weapon = PRIMARY BowWeapon\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"    AutoAcquireEnemiesWhenIdle = Yes\n"
	"    MoodAttackCheckRate = 200\n"
	"  End\n"
	"  Behavior = DestroyDie ModuleTag_Destroy\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = AirLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 60\n"
	"  End\n"
	"End\n";

void applyEnnorathMarchSettings(GameLogic &logic)
{
	GameLogicSettings &gs = logic.settings();
	gs.formationColumnWidth = 60.0f;
	gs.formationRowDepth = 55.0f;
	gs.formationSquadSpacing = 30.0f;
	gs.formationColumns = 2;
	gs.minDistanceForGroup = 100.0f;
	gs.formationEnemyDistance = 200.0f;
	gs.narrowPassageScale = 0.4f;
	gs.waitForOthers = false;
	gs.useFormations = true;
	gs.attackMoveUsesFormations = true;
	gs.groupMoveClickToGatherAreaFactor = 0.5f;
}

void disablePlanningMode(combattest::CombatWorld &w)
{
	w.config.pathfind.planningModeEnabled = false;
	w.ai.reset();
	w.ai = std::make_unique<AIWorld>(*w.logic, w.config, w.w.fx.env.macros);
	w.ai->attach();
	w.ai->newMap(w.terrain);
}

bool hasStopPrefix(const GameLogic &logic, const char *prefix)
{
	for (const std::string &line : logic.report().stops)
	{
		if (line.rfind(prefix, 0) == 0)
		{
			return true;
		}
	}
	return false;
}

std::uint32_t callTarget(const retailtest::PeImage &pe, std::uint32_t va)
{
	std::vector<std::uint8_t> d;
	if (!pe.read(va, 5, &d) || d[0] != 0xE8)
	{
		return 0;
	}
	return va + 5u + pe.u32At(va + 1);
}
} // namespace

TEST_CASE("PORT-13-binary-facts: RotWK 2.01 game.dat pins for AttackMoveUsesFormations, RW 0x774E2F, RW 0x77348C mode 1, command 0x25 and state 61")
{
	const retailtest::PeImage *pe = retailtest::PeImage::fromEnvironment();
	if (!pe)
	{
		retailtest::printSkip("PORT-13-binary-facts (RW_GAME_DAT unset)");
		return;
	}
	// AIData::AttackMoveUsesFormations (+ 0xBA, row RW 0xC1CE50, parseBool RW 0x42E558, constructor RW 0x6FFD9E stores 1)
	CHECK(pe->cstring(pe->u32At(0xC1CE50)) == "AttackMoveUsesFormations");
	CHECK(pe->u32At(0xC1CE54) == 0x42E558u);
	CHECK(pe->u32At(0xC1CE5C) == 0xBAu);
	CHECK(pe->hex(0x6FFD9E, 7) == "c686ba00000001"); // mov byte [esi + 0xBA], 1

	// AIGroup::groupAttackMoveToPosition (RW 0x774E2F): + 0xBA, source == 0, + 0xBD, gatherAndMoveInFormation(pos, 0, mode = 1, ground = 1) RW 0x7747DE
	CHECK(pe->hex(0x774E6B, 7) == "80b8ba00000000");  // cmp byte [eax + 0xBA], 0
	CHECK(pe->hex(0x774E79, 7) == "80b8bd00000000");  // cmp byte [eax + 0xBD], 0
	CHECK(pe->hex(0x774E82, 9) == "6a01566a0156ff7508"); // push 1; push esi; push 1; push esi; push [ebp+8]
	CHECK(callTarget(*pe, 0x774E8D) == 0x7747DEu);

	// MSG_DO_ATTACKMOVETO (RW 0x77AA7A -> 0x77AAE6) calls RW 0x774E2F
	CHECK(callTarget(*pe, 0x77AAE6) == 0x774E2Fu);

	// moveFormationToPos (RW 0x77348C) mode 1: Object::isAbleToAttack (RW 0x691269) -> command 0x25 wrapper RW 0x77122F or RW 0x696266
	CHECK(pe->hex(0x773D4D, 4) == "837d1001");       // cmp dword [ebp + 0x10], 1
	CHECK(callTarget(*pe, 0x773D55) == 0x691269u);
	CHECK(callTarget(*pe, 0x773D7E) == 0x696266u);
	CHECK(callTarget(*pe, 0x773D94) == 0x77122Fu);

	// Command 0x25 (RW 0x77122F -> RW 0x66BC68) and state 61 (0x3D) registration in AIStateMachine (RW 0x753B4E)
	CHECK(pe->hex(0x771245, 2) == "6a25");           // push 0x25
	CHECK(pe->hex(0x66BD20, 2) == "6a3d");           // push 0x3D
	CHECK(pe->hex(0x753B51, 2) == "6a01");           // push 1 (isAttack = true)
	CHECK(callTarget(*pe, 0x753B56) == 0x74FC9Bu);
	CHECK(pe->hex(0x753B63, 2) == "6a3d");           // push 0x3D (state 61)
	CHECK(callTarget(*pe, 0x753B6B) == 0x8DBE1Du);
}

TEST_CASE("PORT-13-ini-and-crc: AIData AttackMoveUsesFormations defaults to true (RW 0x6FFD9E), parses Yes/No (RW 0xC1CE50), and keeps the hash of retail data")
{
	GameLogicSettings defaults;
	CHECK(defaults.attackMoveUsesFormations); // RW 0x6FFD9E: mov byte ptr [esi+0xBA], 1

	// both retail editions' default\aidata.ini set AttackMoveUsesFormations = Yes (line 83 of both mounts), so only a mod that turns it off
	// reaches the extra hashed byte; the retail value hashes as the pre-PORT-13 sequence did
	std::string error;
	GameLogicSettings modNo;
	REQUIRE_MESSAGE(GameLogicSettingsLoader::scanAIData("AIData\n  AttackMoveUsesFormations = No\nEnd\n", modNo, &error), error);
	CHECK_FALSE(modNo.attackMoveUsesFormations);

	GameLogicSettings retailYes;
	retailYes.attackMoveUsesFormations = false;
	REQUIRE_MESSAGE(GameLogicSettingsLoader::scanAIData("AIData\n  AttackMoveUsesFormations = Yes\nEnd\n", retailYes, &error), error);
	CHECK(retailYes.attackMoveUsesFormations);

	StateHasher hNo, hYes;
	modNo.crc(hNo);
	retailYes.crc(hYes);
	CHECK(hNo.value() != hYes.value());

	GameLogicSettings baselineCheck;
	baselineCheck.attackMoveUsesFormations = true;
	StateHasher hBaseline;
	baselineCheck.crc(hBaseline);
	CHECK(hBaseline.value() == hYes.value());
}

TEST_CASE("PORT-13-group-attack-move-gates: RW 0x774E2F formation march vs fallback across count, AIData flags, source, tighten cluster and nearby enemy")
{
	SUBCASE("tightly clustered player group marches in state 61 via MSG_DO_ATTACKMOVETO (planning off) even when groupMoveToPosition tightens out")
	{
		combattest::CombatWorld w(kPort13ExtraUnits);
		disablePlanningMode(w); // RW 0x77AA7A -> 0x77AAE6 -> 0x774E2F when PlanningModeEnabled (+ 0x11CA) is false
		applyEnnorathMarchSettings(*w.logic);
		w.logic->settings().groupMoveClickToGatherAreaFactor = 2.0f;

		std::vector<Object *> moveSquad;
		for (int i = 0; i < 4; ++i)
		{
			moveSquad.push_back(w.unit("Swordsman", 'A', 180.0f + 8.0f * (float)(i / 2), 480.0f + 8.0f * (float)(i % 2)));
		}
		std::vector<ObjectID> moveIds;
		for (Object *o : moveSquad)
		{
			moveIds.push_back(o->getID());
		}
		// Plain groupMoveToPosition (RW 0x774897) checks the tighten rectangle around a click near a wide/factor-scaled area; place destination inside tighten box:
		AIGroup moveGroup(*w.logic, moveIds);
		w.logic->settings().groupMoveClickToGatherAreaFactor = 80.0f;
		moveGroup.groupMoveToPosition(Coord3D{ 450.0f, 484.0f, 0.0f }, false, CMD_FROM_PLAYER);
		for (Object *o : moveSquad)
		{
			CHECK(o->getAIUpdateInterface()->currentStateId() != (unsigned)AI_FOLLOW_PATH_AS_TEAM);
		}

		// Now dispatch MSG_DO_ATTACKMOVETO on an identical cluster with the same huge GroupMoveClickToGatherAreaFactor: RW 0x774E2F has NO tighten check!
		combattest::CombatWorld wAtk(kPort13ExtraUnits);
		disablePlanningMode(wAtk);
		applyEnnorathMarchSettings(*wAtk.logic);
		wAtk.logic->settings().groupMoveClickToGatherAreaFactor = 80.0f;

		std::vector<Object *> atkSquad;
		for (int i = 0; i < 4; ++i)
		{
			atkSquad.push_back(wAtk.unit("Swordsman", 'A', 180.0f + 8.0f * (float)(i / 2), 480.0f + 8.0f * (float)(i % 2)));
		}
		wAtk.frames(1);
		const int alice = wAtk.playerIndex("Alice");
		wAtk.select(alice, atkSquad);
		wAtk.moveTo(alice, 450.0f, 484.0f, MSG_DO_ATTACKMOVETO);
		wAtk.frames(1);

		std::set<std::pair<float, float>> slots;
		for (Object *o : atkSquad)
		{
			AIUpdateInterface *ai = o->getAIUpdateInterface();
			REQUIRE(ai);
			CHECK(ai->currentStateId() == (unsigned)AI_FOLLOW_PATH_AS_TEAM_ATTACK);
			CHECK(ai->formationId() == 1);
			CHECK_FALSE(ai->attackMoveActive());
			slots.insert({ ai->formationOffset().x, ai->formationOffset().y });
		}
		CHECK(slots.size() == 4);
		CHECK(hasStopPrefix(*wAtk.logic, "[S-4462]"));
		CHECK(hasStopPrefix(*wAtk.logic, "[S-4463]"));
	}

	SUBCASE("AttackMoveUsesFormations = No falls back to individual attack-move")
	{
		combattest::CombatWorld w(kPort13ExtraUnits);
		applyEnnorathMarchSettings(*w.logic);
		w.logic->settings().attackMoveUsesFormations = false;
		std::vector<ObjectID> ids;
		for (int i = 0; i < 3; ++i)
		{
			ids.push_back(w.unit("Swordsman", 'A', 180.0f, 460.0f + 40.0f * (float)i)->getID());
		}
		AIGroup group(*w.logic, ids);
		group.groupAttackMoveToPosition(Coord3D{ 700.0f, 500.0f, 0.0f }, 0x7FFFFFFF, CMD_FROM_PLAYER);
		for (ObjectID id : ids)
		{
			AIUpdateInterface *ai = w.byId(id)->getAIUpdateInterface();
			CHECK(ai->currentStateId() != (unsigned)AI_FOLLOW_PATH_AS_TEAM_ATTACK);
			CHECK(ai->attackMoveActive());
		}
	}

	SUBCASE("UseFormations = No falls back to individual attack-move")
	{
		combattest::CombatWorld w(kPort13ExtraUnits);
		applyEnnorathMarchSettings(*w.logic);
		w.logic->settings().useFormations = false;
		std::vector<ObjectID> ids;
		for (int i = 0; i < 3; ++i)
		{
			ids.push_back(w.unit("Swordsman", 'A', 180.0f, 460.0f + 40.0f * (float)i)->getID());
		}
		AIGroup group(*w.logic, ids);
		group.groupAttackMoveToPosition(Coord3D{ 700.0f, 500.0f, 0.0f }, 0x7FFFFFFF, CMD_FROM_PLAYER);
		for (ObjectID id : ids)
		{
			AIUpdateInterface *ai = w.byId(id)->getAIUpdateInterface();
			CHECK(ai->currentStateId() != (unsigned)AI_FOLLOW_PATH_AS_TEAM_ATTACK);
			CHECK(ai->attackMoveActive());
		}
	}

	SUBCASE("non-player command sources (CMD_FROM_SCRIPT, CMD_FROM_AI) and single-unit groups fall back to individual attack-move")
	{
		for (CommandSourceType src : { CMD_FROM_SCRIPT, CMD_FROM_AI })
		{
			combattest::CombatWorld w(kPort13ExtraUnits);
			applyEnnorathMarchSettings(*w.logic);
			std::vector<ObjectID> ids = {
				w.unit("Swordsman", 'A', 180.0f, 480.0f)->getID(),
				w.unit("Swordsman", 'A', 180.0f, 520.0f)->getID(),
			};
			AIGroup group(*w.logic, ids);
			group.groupAttackMoveToPosition(Coord3D{ 700.0f, 500.0f, 0.0f }, 0x7FFFFFFF, src);
			for (ObjectID id : ids)
			{
				AIUpdateInterface *ai = w.byId(id)->getAIUpdateInterface();
				CHECK(ai->currentStateId() != (unsigned)AI_FOLLOW_PATH_AS_TEAM_ATTACK);
				CHECK(ai->attackMoveActive());
			}
		}

		combattest::CombatWorld wSingle(kPort13ExtraUnits);
		applyEnnorathMarchSettings(*wSingle.logic);
		Object *solo = wSingle.unit("Swordsman", 'A', 180.0f, 500.0f);
		AIGroup groupSingle(*wSingle.logic, { solo->getID() });
		groupSingle.groupAttackMoveToPosition(Coord3D{ 700.0f, 500.0f, 0.0f }, 0x7FFFFFFF, CMD_FROM_PLAYER);
		CHECK(solo->getAIUpdateInterface()->currentStateId() != (unsigned)AI_FOLLOW_PATH_AS_TEAM_ATTACK);
		CHECK(solo->getAIUpdateInterface()->attackMoveActive());
	}

	SUBCASE("nearby enemy within FormationEnemyDistance refuses formation ground path and falls back to individual attack-move")
	{
		combattest::CombatWorld w(kPort13ExtraUnits);
		applyEnnorathMarchSettings(*w.logic);
		w.logic->settings().formationEnemyDistance = 300.0f;
		Object *s1 = w.unit("Swordsman", 'A', 180.0f, 480.0f);
		Object *s2 = w.unit("Swordsman", 'A', 180.0f, 520.0f);
		w.unit("Dummy", 'B', 320.0f, 500.0f); // 140 units away < FormationEnemyDistance (300)
		AIGroup group(*w.logic, { s1->getID(), s2->getID() });
		group.groupAttackMoveToPosition(Coord3D{ 750.0f, 500.0f, 0.0f }, 0x7FFFFFFF, CMD_FROM_PLAYER);
		CHECK(s1->getAIUpdateInterface()->currentStateId() != (unsigned)AI_FOLLOW_PATH_AS_TEAM_ATTACK);
		CHECK(s2->getAIUpdateInterface()->currentStateId() != (unsigned)AI_FOLLOW_PATH_AS_TEAM_ATTACK);
		CHECK(s1->getAIUpdateInterface()->attackMoveActive());
		CHECK(s2->getAIUpdateInterface()->attackMoveActive());
	}
}

TEST_CASE("PORT-13-mixed-unit-dispatch: moveFormationToPos mode 1 (RW 0x773D4D) routes armed ground -> 61, unarmed ground -> 54, air attacker -> aiAttackMoveToPosition")
{
	combattest::CombatWorld w(kPort13ExtraUnits);
	applyEnnorathMarchSettings(*w.logic);

	Object *sword1 = w.unit("Swordsman", 'A', 180.0f, 460.0f);
	Object *sword2 = w.unit("Swordsman", 'A', 180.0f, 500.0f);
	Object *unarmed = w.unit("Dummy", 'A', 180.0f, 540.0f);
	Object *air = w.unit("AirFighter", 'A', 180.0f, 580.0f);

	REQUIRE(sword1->isAbleToAttack());
	REQUIRE(sword1->getAIUpdateInterface()->canFollowTeamPath());
	REQUIRE_FALSE(unarmed->isAbleToAttack());
	REQUIRE(unarmed->getAIUpdateInterface()->canFollowTeamPath());
	REQUIRE(air->isAbleToAttack());
	REQUIRE_FALSE(air->getAIUpdateInterface()->canFollowTeamPath());

	AIGroup group(*w.logic, { sword1->getID(), sword2->getID(), unarmed->getID(), air->getID() });
	group.groupAttackMoveToPosition(Coord3D{ 760.0f, 520.0f, 0.0f }, 0x7FFFFFFF, CMD_FROM_PLAYER);

	CHECK(sword1->getAIUpdateInterface()->currentStateId() == (unsigned)AI_FOLLOW_PATH_AS_TEAM_ATTACK);
	CHECK(sword2->getAIUpdateInterface()->currentStateId() == (unsigned)AI_FOLLOW_PATH_AS_TEAM_ATTACK);
	CHECK_FALSE(sword1->getAIUpdateInterface()->attackMoveActive());
	CHECK_FALSE(sword2->getAIUpdateInterface()->attackMoveActive());

	CHECK(unarmed->getAIUpdateInterface()->currentStateId() == (unsigned)AI_FOLLOW_PATH_AS_TEAM);
	CHECK_FALSE(unarmed->getAIUpdateInterface()->attackMoveActive());

	CHECK(air->getAIUpdateInterface()->currentStateId() != (unsigned)AI_FOLLOW_PATH_AS_TEAM);
	CHECK(air->getAIUpdateInterface()->currentStateId() != (unsigned)AI_FOLLOW_PATH_AS_TEAM_ATTACK);
	CHECK(air->getAIUpdateInterface()->attackMoveActive());
}

TEST_CASE("PORT-13-state61-combat-resume-and-arrival: state 61 engages mid-march enemy via sub-machine, reports isAttacking, repaths on kill and idles cleanly")
{
	combattest::CombatWorld w(kPort13ExtraUnits);
	applyEnnorathMarchSettings(*w.logic);
	w.logic->settings().formationEnemyDistance = 200.0f;

	std::vector<Object *> squad = {
		w.unit("Swordsman", 'A', 180.0f, 470.0f),
		w.unit("Swordsman", 'A', 180.0f, 530.0f),
		w.unit("Swordsman", 'A', 140.0f, 470.0f),
		w.unit("Swordsman", 'A', 140.0f, 530.0f),
	};
	// Pre-arm a stale individual attack-move far away to prove command 0x25 clears m_attackMoveActive so arrival at AI_IDLE stays idle.
	for (Object *o : squad)
	{
		REQUIRE(o->getAIUpdateInterface()->armAttackMove(Coord3D{ 950.0f, 950.0f, 0.0f }, CMD_FROM_PLAYER));
	}

	// Place enemy Dummy at (500, 500): 320 units east of front rank (> FormationEnemyDistance = 200), directly in the march corridor to (820, 500).
	Object *enemy = w.unit("Dummy", 'B', 500.0f, 500.0f);
	const ObjectID enemyId = enemy->getID();

	std::vector<ObjectID> ids;
	for (Object *o : squad)
	{
		ids.push_back(o->getID());
	}
	AIGroup group(*w.logic, ids);
	group.groupAttackMoveToPosition(Coord3D{ 820.0f, 500.0f, 0.0f }, 0x7FFFFFFF, CMD_FROM_PLAYER);

	std::vector<Coord3D> slotGoals;
	for (Object *o : squad)
	{
		AIUpdateInterface *ai = o->getAIUpdateInterface();
		REQUIRE(ai->currentStateId() == (unsigned)AI_FOLLOW_PATH_AS_TEAM_ATTACK);
		CHECK_FALSE(ai->attackMoveActive());
		CHECK_FALSE(ai->isAttacking());
		REQUIRE_FALSE(ai->stateMachine().goalPath().empty());
		slotGoals.push_back(ai->stateMachine().goalPath().back());
	}

	// Advance until at least one marcher engages the mid-route enemy in its internal attack sub-machine (RW 0x75003B .. 0x75006E).
	bool sawSubMachineAttack = false;
	const int framesToEngage = w.runUntil(
		[&] {
			for (Object *o : squad)
			{
				AIUpdateInterface *ai = o->getAIUpdateInterface();
				const auto *st = dynamic_cast<const AIFollowPathAsTeamState *>(ai->stateMachine().currentState());
				if (ai->currentStateId() == (unsigned)AI_FOLLOW_PATH_AS_TEAM_ATTACK && ai->isAttacking() && st && st->attackMachine() &&
					st->attackMachine()->currentState() && st->attackMachine()->currentState()->id() == (unsigned)AI_ATTACK_OBJECT)
				{
					CHECK(ai->lastCommandSource() == CMD_FROM_AI); // RW 0x750060
					sawSubMachineAttack = true;
					return true;
				}
			}
			return false;
		},
		250);
	CHECK(framesToEngage < 250); // tolerance: loop timeout guard until squad walks into mood-scan range of Dummy
	REQUIRE(sawSubMachineAttack);

	// Advance until the enemy is destroyed; immediately after combat finishes, RW 0x74FFCD restores m_savedCommandSource and RW 0x7502E9 repaths.
	const int framesToKill = w.runUntil([&] { return w.byId(enemyId) == nullptr; }, 300);
	CHECK(framesToKill < 300); // tolerance: loop timeout guard across melee swings until Dummy dies
	REQUIRE(w.byId(enemyId) == nullptr);

	w.frames(2);
	bool anyStillMarchingState61 = false;
	for (Object *o : squad)
	{
		AIUpdateInterface *ai = o->getAIUpdateInterface();
		CHECK_FALSE(ai->isAttacking());
		CHECK(ai->lastCommandSource() == CMD_FROM_PLAYER);
		if (ai->currentStateId() == (unsigned)AI_FOLLOW_PATH_AS_TEAM_ATTACK)
		{
			anyStillMarchingState61 = true;
		}
	}
	CHECK(anyStillMarchingState61);

	// Advance until every unit finishes its formation march, turns to the formation facing, and enters AI_IDLE.
	const int framesToArrive = w.runUntil(
		[&] {
			for (Object *o : squad)
			{
				if (o->getAIUpdateInterface()->currentStateId() != (unsigned)AI_IDLE)
				{
					return false;
				}
			}
			return true;
		},
		600);
	CHECK(framesToArrive < 600); // tolerance: loop timeout guard for post-combat repath and march to x = 800

	// Run 10 additional frames to confirm no unit resumes the cleared stale {950, 950} attack-move.
	w.frames(10);
	for (size_t i = 0; i < squad.size(); ++i)
	{
		AIUpdateInterface *ai = squad[i]->getAIUpdateInterface();
		CHECK(ai->currentStateId() == (unsigned)AI_IDLE);
		CHECK_FALSE(ai->attackMoveActive());
		const float dx = squad[i]->getPosition()->x - slotGoals[i].x;
		const float dy = squad[i]->getPosition()->y - slotGoals[i].y;
		CHECK(std::hypot((double)dx, (double)dy) < 25.0);                // tolerance: adjustDestination snaps each reserved slot goal to a passable 10-unit cell
		CHECK(std::fabs((double)squad[i]->getOrientation()) < 0.35);     // tolerance: RW 0xC28A34 final-facing arrival threshold 0.31415927 rad plus locomotor turn step
	}
}

TEST_CASE("PORT-13-xfer-and-crc: state 61 round-trips through Xfer while marching and mid-combat, matches crc, and SnapshotProbe rejects bad savedCommandSource")
{
	combattest::CombatWorld w(kPort13ExtraUnits);
	applyEnnorathMarchSettings(*w.logic);
	w.logic->settings().formationEnemyDistance = 200.0f;

	Object *s1 = w.unit("Swordsman", 'A', 180.0f, 480.0f);
	Object *s2 = w.unit("Swordsman", 'A', 180.0f, 520.0f);
	Object *enemy = w.unit("Dummy", 'B', 460.0f, 500.0f);
	(void)enemy;

	AIGroup group(*w.logic, { s1->getID(), s2->getID() });
	group.groupAttackMoveToPosition(Coord3D{ 800.0f, 500.0f, 0.0f }, 0x7FFFFFFF, CMD_FROM_PLAYER);
	w.frames(3);
	REQUIRE(s1->getAIUpdateInterface()->currentStateId() == (unsigned)AI_FOLLOW_PATH_AS_TEAM_ATTACK);
	REQUIRE_FALSE(s1->getAIUpdateInterface()->isAttacking());

	auto hashMachine = [](const AIStateMachine &m) {
		StateHasher h;
		m.crc(h);
		return h.value();
	};

	// 1. Round-trip Xfer while marching (sub-machine idle) + record closed-set checks for SnapshotProbe.
	{
		XferSave save;
		s1->getAIUpdateInterface()->stateMachine().xfer(save);

		Object *fresh = w.unit("Swordsman", 'A', 100.0f, 100.0f);
		AIStateMachine &dst = fresh->getAIUpdateInterface()->stateMachine();

		std::vector<std::string> checks;
		SnapshotProbe::state() = SnapshotProbe::State{};
		SnapshotProbe::state().log = &checks;
		{
			XferLoad load(save.data());
			dst.xfer(load);
		}
		SnapshotProbe::state() = SnapshotProbe::State{};

		CHECK(dst.currentState() != nullptr);
		CHECK(dst.currentState()->id() == (unsigned)AI_FOLLOW_PATH_AS_TEAM_ATTACK);
		CHECK_FALSE(fresh->getAIUpdateInterface()->isAttacking());
		CHECK(hashMachine(dst) == hashMachine(s1->getAIUpdateInterface()->stateMachine()));

		long long cmdSrcCheckIdx = -1;
		bool foundCmdSrcCheck = false;
		for (size_t i = 0; i < checks.size(); ++i)
		{
			if (checks[i] == "ai follow path as team: the command source")
			{
				cmdSrcCheckIdx = (long long)i;
				foundCmdSrcCheck = true;
				break;
			}
		}
		REQUIRE(foundCmdSrcCheck);

		Object *freshBad = w.unit("Swordsman", 'A', 100.0f, 140.0f);
		SnapshotProbe::state() = SnapshotProbe::State{};
		SnapshotProbe::state().armed = cmdSrcCheckIdx;
		XferLoad loadBad(save.data());
		CHECK_THROWS_AS(freshBad->getAIUpdateInterface()->stateMachine().xfer(loadBad), XferException);
		SnapshotProbe::state() = SnapshotProbe::State{};
	}

	// 2. Advance until mid-combat (sub-machine in AI_ATTACK_OBJECT) and verify Xfer + CRC parity there too.
	const int engaged = w.runUntil([&] { return s1->getAIUpdateInterface()->isAttacking() || s2->getAIUpdateInterface()->isAttacking(); }, 250);
	REQUIRE(engaged < 250); // tolerance: loop timeout guard until s1/s2 engages Dummy in state 61
	Object *fighter = s1->getAIUpdateInterface()->isAttacking() ? s1 : s2;
	REQUIRE(fighter->getAIUpdateInterface()->currentStateId() == (unsigned)AI_FOLLOW_PATH_AS_TEAM_ATTACK);
	REQUIRE(fighter->getAIUpdateInterface()->isAttacking());

	{
		XferSave saveCombat;
		fighter->getAIUpdateInterface()->stateMachine().xfer(saveCombat);

		Object *freshCombat = w.unit("Swordsman", 'A', 100.0f, 180.0f);
		AIStateMachine &dstCombat = freshCombat->getAIUpdateInterface()->stateMachine();
		XferLoad loadCombat(saveCombat.data());
		dstCombat.xfer(loadCombat);

		CHECK(dstCombat.currentState() != nullptr);
		CHECK(dstCombat.currentState()->id() == (unsigned)AI_FOLLOW_PATH_AS_TEAM_ATTACK);
		CHECK(freshCombat->getAIUpdateInterface()->isAttacking());
		CHECK(hashMachine(dstCombat) == hashMachine(fighter->getAIUpdateInterface()->stateMachine()));
	}
}
