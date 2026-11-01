// OpenBFME. PLAY-4: the Discord tester's "stances don't work" (v0.3.0-preview.6), with the retail data. RotWK's stances act through the AI's guard machine
// (AIGuard.h): an idle unit in Battle guards its spot and chases what it fights out of it (the outer state, for GuardChaseUnitsDuration), in HoldGround it
// guards without pursuit (it walks back instead) and its scans reach a tenth of the vision (VISION -90%), in Aggressive it does not guard (the plain idle
// scan, no leash). A target a mood scan finds is a plain attack that replaces the guard and follows it, in HoldGround too: that is RotWK's (RW 0x66C536 ->
// 0x66BD33). The stance buttons send MSG_CHANGE_STANCE.
// Needs the retail install (SKIPs loudly without it).

#include "HudTestUtil.h"
#include "BuildTestUtil.h"
#include "StructureArena.h"

#include "GameClient/ControlBar.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/InGameHud.h"
#include "GameLogic/AI/AIEmotionStates.h"
#include "GameLogic/AI/AIGuard.h"
#include "GameLogic/AI/AIStateMachine.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/StancesBehavior.h"
#include "GameLogic/Module/ProductionUpdate.h"

#include "Common/GameCommon.h"

#include <algorithm>
#include <cmath>

using namespace hudtest;

TEST_CASE("stance1 retail: follow-path exit releases factory status on individuals but not hordes (RW 0x74B77D)")
{
	if (!haveWorld("stance1 factory exit"))
	{
		return;
	}
	structtest::Arena ar(shared(), "FactionMen", "FactionMordor");
	for (const char *name : { "GondorFighter", "GondorFighterHorde" })
	{
		Object *o = ar.place(name, 0, 600.0f, 600.0f);
		AIUpdateInterface *ai = o->getAIUpdateInterface();
		REQUIRE(ai);
		for (bool production : { false, true })
		{
			CAPTURE(name);
			CAPTURE(production);
			o->setStatus(0x5A, true);
			AIFollowPathState state(ai->stateMachine(), production);
			state.onExit(EXIT_NORMAL);
			CHECK(o->testStatus(0x5A) == o->isKindOfName("HORDE"));
		}
	}
}

TEST_CASE("stance1 retail: a factory horde waits for its members then initializes Battle without a player command (RW 0x89E2D0)")
{
	if (!haveWorld("stance1 factory horde"))
	{
		return;
	}
	structtest::Arena ar(shared(), "FactionMen", "FactionMordor");
	Object *horde = ar.place("GondorFighterHorde", 0, 600.0f, 600.0f);
	StancesBehavior *stance = StancesBehavior::of(*horde);
	REQUIRE(stance);
	REQUIRE(horde->getContain());
	const auto *members = horde->getContain()->getContainedItemsList();
	REQUIRE(members);
	REQUIRE_FALSE(members->empty());
	Object *member = members->front();
	REQUIRE(member);
	horde->setStatus(0x5A, true);
	member->setStatus(0x5A, true);
	for (int i = 0; i < 20; ++i)
	{
		ar.logic.runLogicFrame();
	}
	CHECK(horde->testStatus(0x5A));
	CHECK(stance->getStance() == STANCE_UNINITIALIZED);
	member->setStatus(0x5A, false);
	for (int i = 0; i < 20; ++i)
	{
		ar.logic.runLogicFrame();
	}
	CHECK_FALSE(horde->testStatus(0x5A));
	CHECK(stance->getStance() == STANCE_BATTLE);
}

TEST_CASE("stance1 retail: barracks production gives a fresh battalion Battle without stance input (RW 0x8622E9)")
{
	OPENBFME_REQUIRE_START(s);
	std::string error;
	buildtest::Game g;
	REQUIRE_MESSAGE(buildtest::startGame(*s, "FactionMen", "FactionMordor", 15, g, &error), error);
	GameLogic &logic = g.live->logic();
	Player *player = g.live->players().findPlayerWithName(g.live->report().startSlotPlayers[0]);
	REQUIRE(player);
	Object *fortress = nullptr;
	for (const auto &p : g.live->report().startingObjects)
	{
		Object *o = p.structure ? logic.findObjectByID(p.id) : nullptr;
		if (o && o->getControllingPlayer() == player)
		{
			fortress = o;
		}
	}
	REQUIRE(fortress);
	Coord3D pos = *fortress->getPosition();
	pos.x += 400.0f;
	Object *barracks = g.live->createObject("GondorBarracks", player->getPlayerIndex(), pos, 0.0f, &error);
	REQUIRE_MESSAGE(barracks, error);
	ProductionUpdateInterface *pu = barracks->getProductionUpdate();
	REQUIRE(pu);
	const ThingTemplate *type = logic.things().findTemplate("RohanSpearmenHorde");
	REQUIRE(type);
	REQUIRE(pu->queueCreateUnit(type, -1, pu->requestUniqueUnitID(), -1, false, "", false));
	for (int i = 0; i < 1000 && pu->getProductionCount(); ++i)
	{
		logic.runLogicFrame();
	}
	REQUIRE(pu->getProductionCount() == 0);
	Object *horde = nullptr;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getProducerID() == barracks->getID() && o->isKindOfName("HORDE"))
		{
			REQUIRE(horde == nullptr);
			horde = o;
		}
	}
	REQUIRE(horde);
	StancesBehavior *stance = StancesBehavior::of(*horde);
	REQUIRE(stance);
	for (int i = 0; i < 300; ++i)
	{
		logic.runLogicFrame();
	}
	CAPTURE(horde->getID());
	CAPTURE(horde->getAIUpdateInterface()->currentStateId());
	CHECK_FALSE(horde->testStatus(0x5A));
	CHECK(stance->getStance() == STANCE_BATTLE);
}

namespace
{
void changeStance(Rig &r, Object *unit, int stance)
{
	GameMessage sel(MSG_CREATE_SELECTED_GROUP, r.index());
	sel.appendBooleanArgument(true);
	sel.appendObjectIDArgument(unit->getID());
	r.game->commands().append(sel);
	GameMessage m(MSG_CHANGE_STANCE, r.index());
	m.appendIntegerArgument(stance);
	r.game->commands().append(m);
}

float dist2d(const Coord3D &a, const Coord3D &b)
{
	return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
}

struct Chase
{
	float maxFromSpot = 0.0f;
	float guardRange = 0.0f;
	float outerRange = 0.0f; // the outer state's exit radius (getAdjustedVisionRange, RW 0x6FF412)
	float speed = 0.0f;      // the horde's locomotor speed per frame (curLocomotorSpeed)
	float radius = 0.0f;     // the horde's bounding circle radius (Object + 0xB8, part of RW 0x6FF412's range)
	bool noted3802 = false;
	bool noted5148 = false;  // stop S-5148: the idle state's stance failure (RW 0x7553AB / 0x7537E0) is not ported
	unsigned lastState = 0;
	bool sawOuter = false, sawReturn = false;
	bool sawFaceObject = false; // the temporary FACE_OBJECT state (59, RW 0x75638B) of the TAUNT / POINT emotion
	bool attackedAfterFaceObject = false; // the parent machine left the guard for a plain attack (RW 0x66BD33) after it
	float foeDistAtAttack = -1.0f;  // the 3D centre distance to the nearest of the enemy horde and its members at that attack (RW 0x7006D7, distance type 1)
	float rangeAtAttack = -1.0f;    // the mood scan's range then (RW 0x6FF412(obj, 3))
};

// a Gondor soldier horde in `stance`, in a real skirmish start (the map revealed, UseShroud No), next to a Mordor horde that walks away from it: how far the
// soldiers follow it from the spot they guarded
Chase runChase(starttest::Shared *st, int stance)
{
	Chase out;
	std::string error;
	buildtest::Game g;
	REQUIRE_MESSAGE(buildtest::startGame(*st, "FactionMen", "FactionMordor", 4242, g, &error), error);
	LiveGame &live = *g.live;
	GameLogic &logic = live.logic();
	Player *player = live.players().findPlayerWithName(live.report().startSlotPlayers[0]);
	Player *enemy = live.players().findPlayerWithName(live.report().startSlotPlayers[1]);
	REQUIRE(player);
	REQUIRE(enemy);
	Object *fortress = nullptr;
	for (const StartingBase::Placed &p : live.report().startingObjects)
	{
		Object *o = p.structure ? logic.findObjectByID(p.id) : nullptr;
		fortress = (o && o->getControllingPlayer() == player) ? o : fortress;
	}
	REQUIRE(fortress);
	Coord3D c0 = *fortress->getPosition();
	c0.x += 900.0f;
	Object *unit = live.createObject("GondorFighterHorde", player->getPlayerIndex(), c0, 0.0f, &error);
	REQUIRE_MESSAGE(unit, error);
	for (int i = 0; i < 10; ++i)
	{
		logic.runLogicFrame();
	}
	StancesBehavior *sb = StancesBehavior::of(*unit);
	REQUIRE(sb);
	if (stance != STANCE_BATTLE)
	{
		sb->setStance((StanceType)stance); // the logic of MSG_CHANGE_STANCE (AIGroup::setStance RW 0x76FF27 -> RW 0x8620DD)
		logic.runLogicFrame();
	}
	AIUpdateInterface *ai = unit->getAIUpdateInterface();
	REQUIRE(ai);
	const Coord3D spot = *unit->getPosition();
	out.guardRange = AIGuardMachine::getStdGuardRange(*unit);
	out.outerRange = AIGuardMachine::getAdjustedVisionRange(*unit);
	out.speed = ai->curLocomotorSpeed();
	// 150 ahead (centre to centre): inside Battle's guard range, the two formations (centred on their hordes, RW 0x86C4EA) a step apart. In Battle the guard's
	// inner state takes the enemy before it strikes. HoldGround's attribute modifier (FighterHordeStanceHoldGround: VISION -90%) leaves the horde a tenth of
	// the vision range (RW 0x6FF412 -> 0x68E43B) plus its bounding circle: its guard waits for the enemy's centre to come that near (RW 0x70146A ->
	// 0x7006D7, the 3D centre distance); the enemy closes in and strikes, and the struck horde's TAUNT / POINT emotion's FACE_OBJECT state (RW 0x75638B ->
	// 0x7553AB -> 0x66844A) runs the mood scan with the same reach
	Object *foe = live.createObject("MordorFighterHorde", enemy->getPlayerIndex(), Coord3D{ c0.x + 150.0f, c0.y, 0.0f }, 0.0f, &error);
	REQUIRE_MESSAGE(foe, error);
	out.radius = CombatQueries::boundingCircleRadius(*unit);
	auto watch = [&]() {
		const AIStateMachine *m = ai->stateMachineOrNull();
		out.sawFaceObject = out.sawFaceObject || m->temporaryStateId() == (unsigned)AI_FACE_OBJECT_IDLE;
		if (!out.attackedAfterFaceObject && out.sawFaceObject && m->currentStateId() == (unsigned)AI_ATTACK_OBJECT)
		{
			out.attackedAfterFaceObject = true;
			// lane TARGET-1's search (AI::findClosestEnemy, RW 0x7006D7) takes the partition's objects, the enemy horde's members among them, and a HORDE
			// seeker's find held by a HORDE container becomes that container (RW 0x668ABC): the find is the nearest of the enemy horde and its members
			const Coord3D &a = *unit->getPosition();
			auto dist3d = [&a](const Coord3D &b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z)); };
			out.foeDistAtAttack = dist3d(*foe->getPosition());
			if (const ContainModuleInterface *c = foe->getContain())
			{
				if (const auto *members = c->getContainedItemsList())
				{
					for (const Object *m : *members)
					{
						out.foeDistAtAttack = std::min(out.foeDistAtAttack, dist3d(*m->getPosition()));
					}
				}
			}
			out.rangeAtAttack = AIGuardMachine::getAdjustedVisionRange(*unit);
		}
	};
	for (int i = 0; i < 15; ++i)
	{
		logic.runLogicFrame(); // the soldiers find it and close in
		watch();
	}
	AIUpdateInterface *foeAi = foe->getAIUpdateInterface();
	REQUIRE(foeAi);
	foeAi->aiMoveToPosition(Coord3D{ c0.x + 900.0f, c0.y, 0.0f }, CMD_FROM_PLAYER);
	for (int i = 0; i < 150; ++i)
	{
		logic.runLogicFrame();
		out.maxFromSpot = std::max(out.maxFromSpot, dist2d(*unit->getPosition(), spot));
		watch();
		if (const AIGuardState *gs = dynamic_cast<const AIGuardState *>(ai->stateMachineOrNull()->currentState()))
		{
			if (gs->guardMachine())
			{
				const unsigned id = gs->guardMachine()->currentStateId();
				out.sawOuter = out.sawOuter || id == AI_GUARD_OUTER;
				out.sawReturn = out.sawReturn || id == AI_GUARD_RETURN;
			}
		}
	}
	out.lastState = ai->currentStateId();
	for (const auto &kv : logic.notedStopHits())
	{
		out.noted3802 = out.noted3802 || kv.first == guardStopLine();
		out.noted5148 = out.noted5148 || kv.first == idleStanceStopLine();
	}
	return out;
}
} // namespace

TEST_CASE("play4 stances retail: an idle unit guards its spot in Battle and HoldGround (mode 0 / 1) and not in Aggressive; the stance button sends the stance")
{
	if (!haveWorld("play4 stances retail"))
	{
		return;
	}
	SharedWorld &s = shared();
	Rig r(s);
	float mx = 0, my = 0;
	REQUIRE(r.logic().terrain()->getExtent(0, mx, my));
	const Coord3D c0 = r.freeSpot(mx * 0.5f, my * 0.5f, 300.0f);
	Object *unit = r.make("GondorFighterHorde", c0.x, c0.y);
	r.frame(10);
	StancesBehavior *sb = StancesBehavior::of(*unit);
	REQUIRE(sb);
	AIUpdateInterface *ai = unit->getAIUpdateInterface();
	REQUIRE(ai);
	// StancesBehavior's first update (RW 0x8622E9): Battle, and the idle unit guards its spot in mode 0 (RW 0x662A21(0))
	CHECK(sb->getStance() == STANCE_BATTLE);
	CHECK(ai->currentStateId() == (unsigned)AI_GUARD);
	CHECK(ai->guardMode() == GUARDMODE_NORMAL);
	CHECK(ai->isIdle()); // the guard machine's idle state answers idle (RW 0x74379D -> RW 0x8BD372)
	changeStance(r, unit, STANCE_HOLD_GROUND);
	r.frame(2);
	CHECK(sb->getStance() == STANCE_HOLD_GROUND);
	CHECK(ai->currentStateId() == (unsigned)AI_GUARD);
	CHECK(ai->guardMode() == GUARDMODE_GUARD_WITHOUT_PURSUIT);
	changeStance(r, unit, STANCE_AGGRESSIVE);
	r.frame(2);
	CHECK(sb->getStance() == STANCE_AGGRESSIVE);
	CHECK(ai->currentStateId() == (unsigned)AI_IDLE); // RW 0x862257: leaving Battle / HoldGround for Aggressive idles the AI
	// a move order in HoldGround: HoldGroundMoving while it walks (RW 0x862275 from aiDoCommand), HoldGround and a guard again at the end (RW 0x86228E)
	changeStance(r, unit, STANCE_HOLD_GROUND);
	r.frame(2);
	ai->aiMoveToPosition(Coord3D{ c0.x + 150.0f, c0.y, 0.0f }, CMD_FROM_PLAYER);
	r.frame(1);
	CHECK(sb->getStance() == STANCE_HOLD_GROUND_MOVING);
	for (int i = 0; i < 100 && sb->getStance() != STANCE_HOLD_GROUND; ++i)
	{
		r.frame(1);
	}
	CHECK(sb->getStance() == STANCE_HOLD_GROUND);
	CHECK(ai->currentStateId() == (unsigned)AI_GUARD);
	CHECK(ai->guardMode() == GUARDMODE_GUARD_WITHOUT_PURSUIT);
	// the control bar: the toggle shows the current stance's image, follows the stance sent (HoldGround -> Battle) and its own press sends nothing (RW 0x96FAC6)
	RecordingShellServices services;
	InGameHud::Config cfg{ *r.game, *s.world, *s.mount->fs, services, r.view, s.mouse, s.meta, nullptr };
	InGameHud hud(cfg);
	std::string error;
	REQUIRE_MESSAGE(hud.boot(&error), error);
	const auto worldContext = hud.enterContext();
	hud.input().ui().selectObject(unit->getID());
	ControlBar &bar = hud.controlBar();
	bar.update();
	auto findToggle = [&]() -> const ControlBarButton * {
		for (const ControlBarButton &b : bar.palantirButtons())
		{
			if (b.button && b.button->m_command == GUI_COMMAND_TOGGLE_STANCE)
			{
				return &b;
			}
		}
		return nullptr;
	};
	const ControlBarButton *toggle = findToggle();
	REQUIRE(toggle);
	CHECK(toggle->state == ButtonState::Enabled);
	CHECK(toggle->image == "UCCommon_HoldGroundStance");
	// lane CAMPUI-1: the toggle's press reaching the command processor sends nothing (retail's press opens the Palantir's radial, RW 0x96FAC6; only an
	// item sends, RW 0x96F53C: test_campui1_stance.cpp); it is counted. The stance is sent as the radial's Battle item sends it
	CHECK_FALSE(bar.pressButton(toggle->slot, true));
	CHECK(bar.unportedPresses().at("TOGGLE_STANCE (no sub-menu)") == 1u);
	REQUIRE(bar.sendChangeStance(STANCE_BATTLE));
	hud.update(0.2);
	r.game->advance(0.2);
	r.frame(1);
	CHECK(sb->getStance() == STANCE_BATTLE);
	bar.update();
	toggle = findToggle();
	REQUIRE(toggle);
	CHECK(toggle->image == "UCCommon_BattleStance");
}

TEST_CASE("play4 stances retail: a fleeing enemy is chased out of the guarded spot in Battle; in HoldGround the struck horde's FACE_OBJECT mood scan "
		  "finds its attacker within a tenth of the vision and follows it; Aggressive follows it without a leash")
{
	OPENBFME_REQUIRE_START(st);
	const Chase battle = runChase(st, STANCE_BATTLE);
	const Chase hold = runChase(st, STANCE_HOLD_GROUND);
	const Chase aggressive = runChase(st, STANCE_AGGRESSIVE);
	MESSAGE("guard range " << battle.guardRange << ", outer range " << battle.outerRange << ", speed " << battle.speed << " per frame; farthest from the spot: battle "
						  << battle.maxFromSpot << " (outer " << battle.sawOuter << "), hold " << hold.maxFromSpot << " (outer " << hold.sawOuter << ", return " << hold.sawReturn
						  << ", face object " << hold.sawFaceObject << ", attack after it " << hold.attackedAfterFaceObject << ", last state " << hold.lastState << ", enemy at " << hold.foeDistAtAttack << " of range " << hold.rangeAtAttack << "), aggressive " << aggressive.maxFromSpot);
	// the bounds from the retail rules (AIGuard.h) and data: HoldGround (GUARDMODE_GUARD_WITHOUT_PURSUIT) skips the outer state; the locomotor brakes from full
	// speed in 1 s (NormalMeleeHordeLocomotor Braking 500); Battle's outer state chases (GuardChaseUnitsDuration 15000 ms) while the enemy stays inside the
	// vision range around the spot (RW 0x6FF412)
	const float brakingFrames = 1.0f * (float)LOGICFRAMES_PER_SECOND;
	REQUIRE(hold.speed > 0.0f);
	CHECK(battle.sawOuter);
	CHECK_FALSE(hold.sawOuter);
	// HoldGround (S-5149, closed by the review of #73): RotWK's scans reach RW 0x6FF412's range, the vision range with the stance's VISION -90% plus the
	// bounding circle, measured centre to centre (RW 0x7006D7, distance type 1). At contact the struck horde's FACE_OBJECT state (59) runs the mood scan; the
	// attacker's centre is within that range, and the target it finds is a plain attack that replaces the guard and follows it (RW 0x66C536 -> 0x66BD33):
	// RotWK's chase when struck (neither the idle update nor the scan reads the guard mode), not a port bug. The port's chase has no StopChaseDistance
	// (S-325) and is not ended by the idle state's re-guard (S-5148), so how far it goes is the port's
	// tolerance: the float rounding of 1 + (-0.9) and of the product (FighterHordeStanceHoldGround: VISION -90%, a tenth of the vision range)
	CHECK(hold.guardRange - hold.radius == doctest::Approx((battle.guardRange - battle.radius) * 0.1f).epsilon(0.001));
	CHECK(hold.sawFaceObject);          // the FACE_OBJECT emotion at contact
	CHECK(hold.attackedAfterFaceObject); // its mood scan found the attacker
	CHECK(hold.foeDistAtAttack <= hold.rangeAtAttack); // tolerance: the property, the find is within RotWK's reach centre to centre (where in it is the simulation's)
	CHECK(hold.rangeAtAttack == hold.guardRange);
	CHECK_FALSE(hold.sawReturn);
	CHECK(hold.lastState == (unsigned)AI_ATTACK_OBJECT);
	// stop S-5148: an idle stanced unit is not guarded again by the idle state's failure (Aggressive's horde idles and scans)
	CHECK(aggressive.noted5148);
	CHECK(battle.maxFromSpot <= battle.outerRange + battle.speed * brakingFrames);  // the outer state's leash: the vision range around the spot
	CHECK(aggressive.maxFromSpot > battle.outerRange);                             // Aggressive: no guard, the plain attack follows the enemy without a leash
	// stop S-3802 (only the LOCATION guard of the stances is ported) is reported by every game whose units guard; Aggressive does not guard
	CHECK(battle.noted3802);
	CHECK(hold.noted3802);
}
