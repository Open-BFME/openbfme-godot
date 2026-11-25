// OpenBFME. GPL-3.0.
// PORT-217 (fork issue 507, P-123): a builder turns to face the structure it builds or repairs.
//
// The expectation is RotWK 2.01 `game.dat` (md5 `daed3668006cd90f01c34e5a7da1901f`), read with `tools/mod/rw.py dis`
// (caveat S-001). DozerAIUpdate's action machine has four dock sub tasks (0 select the dock, 1 move to it, 2 face the goal,
// 3 work) and the third one is the AI's face command 0x26:
//
//   RW 0x88DD1C .. 0x88DD1F   the dock sub task is 1 (moving)?
//   RW 0x88DD25               the dozer's AI vslot 0x1B8 (isIdle, RW 0x6643FC) answers true
//   RW 0x88DD2F / 0x88DD35    the dock interface's vslot 0x48(2): sub task := 2 (facing)
//   RW 0x88DD38 / 0x88DD3A    the command's arguments: CMD_FROM_AI (2), the goal object
//   RW 0x88DD3E               `call 0x7C81B9` — AICommandInterface::aiFaceObject (AI command 0x26 -> AI state 36)
//   RW 0x88DD43 / 0x88DD4A    the dock sub task is 2?
//   RW 0x88DD57 / 0x88DD5F    the AI is idle again (the turn ended)
//   RW 0x88DD65 / 0x88DD6B    the dock sub task := 3 (work), then the building sound and the porter's fade
//   RW 0x88DA58 .. 0x88DA7A   the same three steps in the repair branch (0x1B8, vslot 0x48(2), 0x7C81B9)
//   RW 0x88DA86 .. 0x88DAA1   the repair branch's sub task 2 -> 3 gate: the work waits for the turn
//
// The turn itself is the ported state 36 (AIFaceState): its update ends when the angle to the goal is below 0.035 rad
// (RW 0xC2A1BC) for a dozer whose locomotor's MinSpeed is 0 (RW 0x5E3796). So after the work starts the builder's facing
// points at the structure within that threshold, and the work does not start before.
//
// Synthetic INI only: no retail data, no ROTWK_INSTALL / OPENBFME_ENNORATATH_INSTALL.
#include "doctest.h"

#include "CombatTestUtil.h"
#include "ProdTestUtil.h"

#include "GameLogic/AI/AIEmotionStates.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/DozerAIUpdate.h"
#include "GameLogic/Module/WorkerAIUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"

#include <cmath>
#include <string>

namespace
{
// A Porter of the port (KindOf DOZER: the dispatcher's gate RW 0x77B067 bit 0x4000, and the build site legality's clear-path
// test RW 0x796A46) with the dozer module, a slow walker and the retail facing rate; the structures come from the world
// (no Guard / body of their own: they only need a geometry and a body for the repair rate).
const char kPort217Objects[] =
	"Object P217Porter\n"
	"  KindOf = INFANTRY SELECTABLE DOZER\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = NoArmor\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 200\n"
	"  End\n"
	"  Behavior = DozerAIUpdate ModuleTag_AI\n"
	"    RepairHealthPercentPerSecond = 10%\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = SlowTurnLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 40\n"
	"  End\n"
	"End\n"
	"Object P217Site\n"
	"  KindOf = STRUCTURE SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 20\n"
	"  GeometryMinorRadius = 20\n"
	"  GeometryHeight = 20\n"
	"  BuildCost = 100\n"
	"  BuildTime = 2.0\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = NoArmor\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 400\n"
	"  End\n"
	"  Behavior = GettingBuiltBehavior ModuleTag_GB\n"
	"    SpawnTimer = 1\n"
	"  End\n"
	"End\n"
	"Object P217Building\n"
	"  KindOf = STRUCTURE SELECTABLE IMMOBILE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 20\n"
	"  GeometryMinorRadius = 20\n"
	"  GeometryHeight = 20\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = NoArmor\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 400\n"
	"  End\n"
	"End\n";

// the port's own walker shape (MoveTestUtil's WalkerLoco) with the retail TurnTime of a non-horde unit
const char kPort217Locomotors[] =
	"Locomotor SlowTurnLoco\n"
	"  Surfaces = GROUND\n"
	"  TurnTime = 2000\n"
	"  TurnTimeDamaged = 2000\n"
	"  Acceleration = 200\n"
	"  Braking = 200\n"
	"  MinSpeed = 0\n"
	"  ZAxisBehavior = NO_Z_MOTIVE_FORCE\n"
	"  Appearance = TWO_LEGS\n"
	"  StickToGround = Yes\n"
	"  CloseEnoughDist = 1\n"
	"End\n";

struct Port217World : combattest::CombatWorld
{
	Port217World()
	{
		std::string err = w.load(kPort217Locomotors, INI_LOAD_OVERWRITE, "port217loco.ini");
		REQUIRE_MESSAGE(err.empty(), err);
		err = w.load(kPort217Objects, INI_LOAD_OVERWRITE, "port217.ini");
		REQUIRE_MESSAGE(err.empty(), err);
		// GameData's build time settings (prodtest::defaultSettings: loaded, no low-energy or multiple-factory penalty) and the
		// build rules the site legality's terrain pass needs; the port's own fixtures do the same
		logic->productionSettings() = prodtest::defaultSettings();
		logic->settings().buildRulesLoaded = true;
		// a task ordered on frame 0 has no "most recent" order frame yet (RW 0x88BE47 compares the order frame strictly), so the
		// world runs three frames first, as the combat fixture's `unit` does for every object
		frames(3);
	}

	Object *porter(float x, float y, float facing)
	{
		Object *o = spawn("P217Porter", x, y, facing, teamA());
		REQUIRE(o != nullptr);
		return o;
	}
	Object *site(const char *name, float x, float y)
	{
		Object *o = spawn(name, x, y, 0.0f, teamA());
		REQUIRE(o != nullptr);
		return o;
	}
	DozerAIUpdate *dozer(Object *o)
	{
		DozerAIUpdate *d = dynamic_cast<DozerAIUpdate *>(o->getAIUpdateInterface());
		REQUIRE(d != nullptr);
		return d;
	}
	// runs logic frames until `ok` or `limit`
	template <typename Fn> bool until(int limit, Fn ok)
	{
		for (int i = 0; i < limit; ++i)
		{
			if (ok())
			{
				return true;
			}
			frames(1);
		}
		return ok();
	}
	// the dozer's AI is in the face state 36 (RW 0x88DD3E -> command 0x26)
	bool facing(Object *o) { return o->getAIUpdateInterface()->currentStateId() == (unsigned)AI_FACE_OBJECT; }
	// the angle between the builder's facing and the direction to `target`, the retail completion test (RW 0xC2A1BC)
	float angleTo(Object &builder, const Object &target)
	{
		return SimMath::absD((double)emotionRelAngle(builder, *target.getPosition()));
	}
};

const float kFacedEnough = 0.035f; // RW 0xC2A1BC

// The building sound and the porter's fade are client work; the logic only posts them.
} // namespace

TEST_CASE("PORT-217 build: the builder turns to the structure before the work starts (RW 0x88DD3E, AI command 0x26) and works facing it")
{
	Port217World w;
	// far to the east, the builder looking the other way (west): the walk to the dock is a straight line, so only the face
	// step can turn him — RW 0x88DD3E
	Object *porter = w.porter(600.0f, 600.0f, 3.14159265f);
	const ThingTemplate *what = w.w.things.findTemplate("P217Site");
	REQUIRE(what != nullptr);
	DozerAIUpdate *d = w.dozer(porter);
	Player *alice = w.players.findPlayerWithName("Alice");
	REQUIRE(alice != nullptr);
	alice->depositMoney(5000);
	Object *site = d->construct(*what->getFinalOverride(), Coord3D{ 900.0f, 560.0f, 0.0f }, 0.0f, *alice);
	REQUIRE(site != nullptr);
	const ObjectID siteId = site->getID();

	// the face command is issued at the dock (sub task 1 -> 2) and the work waits for it (2 -> 3)
	bool sawFace = false;
	int faceFrame = -1, workFrame = -1;
	for (int f = 0; f < 600 && workFrame < 0; ++f)
	{
		if (w.facing(porter))
		{
			if (!sawFace)
			{
				faceFrame = f;
			}
			sawFace = true;
			CHECK(d->getBuildSubTask() == DozerAIUpdate::DOZER_FACING_BUILD);
		}
		if (d->getBuildSubTask() == DozerAIUpdate::DOZER_DO_BUILD_AT_DOCK && d->working())
		{
			workFrame = f;
		}
		w.frames(1);
	}
	CHECK_MESSAGE(sawFace, "the builder never entered the face state 36 before the work");
	REQUIRE_MESSAGE(faceFrame >= 0, "no face frame"); // tolerance: a precondition (the frame the loop saw state 36); the order is asserted below
	REQUIRE_MESSAGE(workFrame >= 0, "the builder never started to work"); // tolerance: a precondition (the frame the loop saw the work); the order is asserted next
	CHECK(faceFrame < workFrame); // tolerance: the same gate, RW 0x88DD5F: the work sub task starts after the turn's frames // tolerance: the frame numbers are the loop's count from the sim's start; RW 0x88DD5F gates the work on the turn, so the order is what the address says, not a measured distance

	// once working, the builder faces the structure within the retail threshold (RW 0xC2A1BC), and it still does after it
	// is done (nothing turns it back): the add-on case of the issue
	Object *built = w.logic->findObjectByID(siteId);
	REQUIRE(built != nullptr);
	CHECK(w.angleTo(*porter, *built) < kFacedEnough); // tolerance: the retail turn threshold, RW 0xC2A1BC (the state 36 body ends the turn below 0.035 rad), not a measured value
	CHECK(w.until(400, [&] { return built->getConstructionPercent() == -1.0f; }));
	CHECK(w.angleTo(*porter, *built) < kFacedEnough); // tolerance: the retail turn threshold, RW 0xC2A1BC (the state 36 body ends the turn below 0.035 rad), not a measured value
}

TEST_CASE("PORT-217 repair: a builder that arrives looking away turns to the damaged structure before it heals it (RW 0x88DA7A)")
{
	Port217World w;
	Object *porter = w.porter(600.0f, 600.0f, 3.14159265f);
	Object *building = w.site("P217Building", 900.0f, 600.0f);
	ActiveBody *body = dynamic_cast<ActiveBody *>(building->getBodyModule());
	REQUIRE(body != nullptr);
	body->internalChangeHealth(-0.5f * body->getMaxHealth());
	REQUIRE(body->getHealth() < body->getMaxHealth()); // tolerance: a precondition (DamagePercent leaves the structure damaged); the repair itself is asserted exact below
	DozerAIUpdate *d = w.dozer(porter);
	REQUIRE(d->repair(*building));

	bool sawFace = false;
	int faceFrame = -1, workFrame = -1;
	for (int f = 0; f < 600 && workFrame < 0; ++f)
	{
		if (w.facing(porter))
		{
			if (!sawFace)
			{
				faceFrame = f;
			}
			sawFace = true;
			CHECK(d->getBuildSubTask() == DozerAIUpdate::DOZER_FACING_BUILD);
		}
		if (d->getBuildSubTask() == DozerAIUpdate::DOZER_DO_BUILD_AT_DOCK && d->working() && d->repairing())
		{
			workFrame = f;
		}
		w.frames(1);
	}
	CHECK_MESSAGE(sawFace, "the repairing builder never entered the face state 36");
	REQUIRE_MESSAGE(workFrame >= 0, "the repairing builder never started to work"); // tolerance: a precondition (the frame the loop saw the work); the order is asserted next
	CHECK(faceFrame < workFrame); // tolerance: the same gate, RW 0x88DD5F: the work sub task starts after the turn's frames

	// the repair's first frame of work happens with the builder already facing the structure
	CHECK(w.angleTo(*porter, *building) < kFacedEnough); // tolerance: the retail turn threshold, RW 0xC2A1BC (the state 36 body ends the turn below 0.035 rad), not a measured value
}

TEST_CASE("PORT-217 an immobile structure as the target is faced too: the turn does not need a move (RW 0x690E97 keeps the builder mobile)")
{
	Port217World w;
	Object *porter = w.porter(600.0f, 600.0f, 0.0f);
	Object *building = w.site("P217Building", 600.0f, 900.0f); // due north: a quarter turn off the builder's facing
	DozerAIUpdate *d = w.dozer(porter);
	REQUIRE(d->repair(*building));
	REQUIRE(w.until(200, [&] { return d->getBuildSubTask() == DozerAIUpdate::DOZER_FACING_BUILD; }));
	w.frames(1);
	// state 36 turns the builder in place: the locomotor goal angle, not a move (RW 0x75638B)
	CHECK(w.until(200, [&] { return w.angleTo(*porter, *building) < kFacedEnough; })); // tolerance: the retail turn threshold, RW 0xC2A1BC (the state 36 body ends the turn below 0.035 rad), not a measured value
	// and the turn is what the work waits for: the structure was never damaged, so the repair task ends as soon as it reaches
	// sub task 3 (RW 0x88DB8F: the health is full), which means the sub task 3 gate itself was reached
	CHECK(w.until(60, [&] { return !d->repairing(); }));
	CHECK(w.angleTo(*porter, *building) < kFacedEnough); // tolerance: the retail turn threshold, RW 0xC2A1BC (the state 36 body ends the turn below 0.035 rad), not a measured value
}

TEST_CASE("PORT-217 the work does not start before the turn ends: a builder at the dock still turning keeps the work waiting (RW 0x88DD5F)")
{
	Port217World w;
	// the builder starts already at the dock but looking away: the move finishes on the first frame, so the only thing
	// between the order and the work is the turn
	Object *porter = w.porter(900.0f, 620.0f, 3.14159265f);
	// a structure the builder has to place (a build task) right where it stands: the walk is over at once, so the only thing
	// between the order and the work is the turn
	DozerAIUpdate *d = w.dozer(porter);
	const ThingTemplate *what = w.w.things.findTemplate("P217Site");
	REQUIRE(what != nullptr);
	Player *alice = w.players.findPlayerWithName("Alice");
	alice->depositMoney(5000);
	Object *placed = d->construct(*what->getFinalOverride(), Coord3D{ 900.0f, 600.0f, 0.0f }, 0.0f, *alice);
	REQUIRE(placed != nullptr);
	int turnFrames = 0;
	int workFrame = -1;
	for (int f = 0; f < 600 && workFrame < 0; ++f)
	{
		const bool turning = w.facing(porter) && !porter->getAIUpdateInterface()->isIdle();
		if (turning)
		{
			++turnFrames;
			// while the turn runs the work has not started
			CHECK(d->getBuildSubTask() != DozerAIUpdate::DOZER_DO_BUILD_AT_DOCK);
		}
		if (d->getBuildSubTask() == DozerAIUpdate::DOZER_DO_BUILD_AT_DOCK && d->working())
		{
			workFrame = f;
		}
		w.frames(1);
	}
	CHECK(turnFrames > 0); // tolerance: a count of frames, never a content value; the fix makes it positive (0 on the old tree)
	REQUIRE_MESSAGE(workFrame >= 0, "the builder never started to work"); // tolerance: a precondition (the frame the loop saw the work); the order is asserted next
	CHECK(w.angleTo(*porter, *placed) < kFacedEnough); // tolerance: the retail turn threshold, RW 0xC2A1BC (the state 36 body ends the turn below 0.035 rad), not a measured value
}
