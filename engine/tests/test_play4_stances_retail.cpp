// OpenBFME. PLAY-4: the Discord tester's "stances don't work" (v0.3.0-preview.6), with the retail data. RotWK's stances act through the AI's guard machine
// (AIGuard.h): an idle unit in Battle guards its spot and chases what it fights out of it (the outer state, for GuardChaseUnitsDuration), in HoldGround it
// guards without pursuit (it walks back instead), in Aggressive it does not guard (the plain idle scan, no leash). The stance buttons send MSG_CHANGE_STANCE.
// Needs the retail install (SKIPs loudly without it).

#include "HudTestUtil.h"
#include "BuildTestUtil.h"

#include "GameClient/ControlBar.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/InGameHud.h"
#include "GameLogic/AI/AIGuard.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/StancesBehavior.h"

#include "Common/GameCommon.h"

#include <algorithm>
#include <cmath>

using namespace hudtest;

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
	bool noted3802 = false;
	unsigned lastState = 0;
	bool sawOuter = false, sawReturn = false;
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
	Object *foe = live.createObject("MordorFighterHorde", enemy->getPlayerIndex(), Coord3D{ c0.x + 120.0f, c0.y, 0.0f }, 0.0f, &error);
	REQUIRE_MESSAGE(foe, error);
	for (int i = 0; i < 15; ++i)
	{
		logic.runLogicFrame(); // the soldiers find it and close in
	}
	AIUpdateInterface *foeAi = foe->getAIUpdateInterface();
	REQUIRE(foeAi);
	foeAi->aiMoveToPosition(Coord3D{ c0.x + 900.0f, c0.y, 0.0f }, CMD_FROM_PLAYER);
	for (int i = 0; i < 150; ++i)
	{
		logic.runLogicFrame();
		out.maxFromSpot = std::max(out.maxFromSpot, dist2d(*unit->getPosition(), spot));
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
	// the control bar: the toggle shows the current stance's image and a press sends the next of its Stances (HoldGround -> Battle)
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
	REQUIRE(bar.pressButton(toggle->slot, true));
	hud.update(0.2);
	r.game->advance(0.2);
	r.frame(1);
	CHECK(sb->getStance() == STANCE_BATTLE);
	bar.update();
	toggle = findToggle();
	REQUIRE(toggle);
	CHECK(toggle->image == "UCCommon_BattleStance");
}

TEST_CASE("play4 stances retail: a fleeing enemy is chased out of the guarded spot in Battle, not in HoldGround; Aggressive follows it without a leash")
{
	OPENBFME_REQUIRE_START(st);
	const Chase battle = runChase(st, STANCE_BATTLE);
	const Chase hold = runChase(st, STANCE_HOLD_GROUND);
	const Chase aggressive = runChase(st, STANCE_AGGRESSIVE);
	MESSAGE("guard range " << battle.guardRange << ", outer range " << battle.outerRange << ", speed " << battle.speed << " per frame; farthest from the spot: battle "
						  << battle.maxFromSpot << " (outer " << battle.sawOuter << "), hold " << hold.maxFromSpot << " (outer " << hold.sawOuter << ", return " << hold.sawReturn
						  << "), aggressive " << aggressive.maxFromSpot);
	// the bounds from the retail rules (AIGuard.h) and data: the inner state's attack lasts at most 3 s (RW 0x94A048: frame + 3 * LOGICFRAMES_PER_SECOND), HoldGround
	// (GUARDMODE_GUARD_WITHOUT_PURSUIT) skips the outer state and walks back; the locomotor brakes from full speed in 1 s (NormalMeleeHordeLocomotor Braking 500);
	// Battle's outer state chases (GuardChaseUnitsDuration 15000 ms) while the enemy stays inside the vision range around the spot (RW 0x6FF412)
	const float innerFrames = 3.0f * (float)LOGICFRAMES_PER_SECOND, brakingFrames = 1.0f * (float)LOGICFRAMES_PER_SECOND;
	REQUIRE(hold.speed > 0.0f);
	CHECK(battle.sawOuter);
	CHECK_FALSE(hold.sawOuter);
	CHECK(hold.sawReturn);
	CHECK(hold.maxFromSpot <= hold.speed * (innerFrames + brakingFrames));           // HoldGround: no further than the inner attack and the braking take it
	CHECK(battle.maxFromSpot > hold.maxFromSpot);                                   // Battle: the outer state pursues beyond that
	CHECK(battle.maxFromSpot <= battle.outerRange + battle.speed * brakingFrames);  // the outer state's leash: the vision range around the spot
	CHECK(aggressive.maxFromSpot > battle.outerRange);                             // Aggressive: no guard, the plain attack follows the enemy without a leash
	// stop S-3802 (only the LOCATION guard of the stances is ported) is reported by every game whose units guard; Aggressive does not guard
	CHECK(battle.noted3802);
	CHECK(hold.noted3802);
}
