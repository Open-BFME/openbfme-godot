// OpenBFME unit tests: the player commands of movement through the lockstep command path (lane MOVE-1): GameMessage -> command list -> dispatcher -> AI.

#include "doctest.h"
#include "MoveTestUtil.h"
#include "GameClient/FormationAssistant.h"

#include <algorithm>
#include <cmath>
#include <limits>

using namespace movetest;

TEST_CASE("commands: a selection and a move message move the selected units to the one point, each adjusted beside the others (RW 0x75748C, MoveToGroupOrder)")
{
	MoveWorld mw;
	mw.buildMap();
	const int alice = mw.playerIndex("Alice");
	Object *a = mw.spawn("Walker", 305.0f, 305.0f);
	Object *b = mw.spawn("Walker", 305.0f, 345.0f);
	Object *c = mw.spawn("Walker", 305.0f, 385.0f);
	Object *other = mw.spawn("Walker", 505.0f, 305.0f); // not selected
	mw.frames(2);
	const float otherX = other->getPosition()->x;
	mw.select(alice, { a, b, c });
	mw.moveTo(alice, 705.0f, 705.0f);
	CHECK(mw.commands.messages().size() == 2);
	CHECK(a->getAIUpdateInterface()->isIdle());       // nothing happens until the logic frame runs the command list
	mw.frames(1);
	CHECK(mw.commands.messages().size() == 0);                   // the list was processed and reset
	CHECK(mw.aiCommands.stats().moves == 1);
	CHECK(AICommands::selection(*mw.logic, alice).size() == 3);
	CHECK_FALSE(a->getAIUpdateInterface()->isIdle());
	CHECK_FALSE(b->getAIUpdateInterface()->isIdle());
	CHECK_FALSE(c->getAIUpdateInterface()->isIdle());
	CHECK(other->getAIUpdateInterface()->isIdle());
	mw.frames(150);
	CHECK(a->getAIUpdateInterface()->isIdle());
	CHECK(b->getAIUpdateInterface()->isIdle());
	CHECK(c->getAIUpdateInterface()->isIdle());
	// lane MOVE-3: RotWK's MSG_DO_MOVETO hands the group to the group manager's MoveToGroupOrder (RW 0x77BAAA -> 0x75748C): every unit, in group order, gets
	// the point adjusted by adjustDestination against the goals reserved before it (RW 0x94DF97): a takes the point, b and c the nearest free cells (the ZH
	// group move kept each unit's offset from the nearest one; that is now MSG_DO_FORCEMOVETO's)
	const Coord3D goal{ 705.0f, 705.0f, 0.0f };
	CHECK(dist2d(*a->getPosition(), goal) < 25.0f);
	CHECK(dist2d(*b->getPosition(), goal) < 40.0f);
	CHECK(dist2d(*c->getPosition(), goal) < 40.0f);
	CHECK(dist2d(*a->getPosition(), *b->getPosition()) > 5.0f); // not stacked on one cell
	CHECK(dist2d(*a->getPosition(), *c->getPosition()) > 5.0f);
	CHECK(dist2d(*b->getPosition(), *c->getPosition()) > 5.0f);
	CHECK(other->getPosition()->x == otherX);
}

TEST_CASE("commands: a stop message idles the group; a new selection without createNew adds; remove takes units out")
{
	MoveWorld mw;
	mw.buildMap();
	const int alice = mw.playerIndex("Alice");
	Object *a = mw.spawn("Walker", 105.0f, 105.0f);
	Object *b = mw.spawn("Walker", 105.0f, 205.0f);
	mw.frames(2);
	mw.select(alice, { a });
	mw.select(alice, { b }, false);
	mw.frames(1);
	CHECK(AICommands::selection(*mw.logic, alice).size() == 2);
	mw.moveTo(alice, 905.0f, 105.0f);
	mw.frames(8);
	CHECK(a->getPosition()->x > 150.0f);
	CHECK(b->getPosition()->x > 150.0f);
	GameMessage stop(MSG_DO_STOP, alice);
	mw.send(stop);
	mw.frames(1);
	CHECK(a->getAIUpdateInterface()->isIdle());
	CHECK(b->getAIUpdateInterface()->isIdle());
	const float ax = a->getPosition()->x;
	mw.frames(5);
	CHECK(a->getPosition()->x == ax);
	GameMessage remove(MSG_REMOVE_FROM_SELECTED_GROUP, alice);
	remove.appendObjectIDArgument(a->getID());
	mw.send(remove);
	mw.frames(1);
	REQUIRE(AICommands::selection(*mw.logic, alice).size() == 1);
	CHECK(AICommands::selection(*mw.logic, alice)[0] == b->getID());
	GameMessage destroy(MSG_DESTROY_SELECTED_GROUP, alice);
	mw.send(destroy);
	mw.frames(1);
	CHECK(AICommands::selection(*mw.logic, alice).empty());
}

TEST_CASE("commands: MSG_ADD_WAYPOINT appends to the path the units follow")
{
	MoveWorld mw;
	mw.buildMap();
	const int alice = mw.playerIndex("Alice");
	Object *a = mw.spawn("Walker", 105.0f, 105.0f);
	mw.frames(2);
	mw.select(alice, { a });
	mw.moveTo(alice, 505.0f, 105.0f, MSG_ADD_WAYPOINT);
	mw.moveTo(alice, 505.0f, 505.0f, MSG_ADD_WAYPOINT);
	mw.frames(1);
	CHECK(a->getAIUpdateInterface()->currentStateId() == (unsigned)AI_FOLLOW_PATH);
	CHECK(mw.aiCommands.stats().waypoints == 2);
	bool passedFirst = false;
	for (int f = 0; f < 200 && !a->getAIUpdateInterface()->isIdle(); ++f)
	{
		mw.frames(1);
		if (dist2d(*a->getPosition(), Coord3D{ 505.0f, 105.0f, 0.0f }) < 30.0f)
		{
			passedFirst = true;
		}
	}
	CHECK(passedFirst);
	CHECK(dist2d(*a->getPosition(), Coord3D{ 505.0f, 505.0f, 0.0f }) < 15.0f);
}

TEST_CASE("commands: a formation move message ends with the units facing the angle; a move-and-orientate message orders one object")
{
	MoveWorld mw;
	mw.buildMap();
	const int alice = mw.playerIndex("Alice");
	Object *a = mw.spawn("Walker", 105.0f, 105.0f);
	Object *b = mw.spawn("Walker", 305.0f, 105.0f);
	mw.frames(2);
	mw.select(alice, { a });
	FormationAssistant formation;
	std::string formationError;
	REQUIRE(formation.scan("FormationAssistant\n UnitDefinition Unit\n ObjectFilter = ALL\n End\n FormationTemplate Line\n Rows\n Row = Unit\n End\n End\nEnd\n", &formationError));
	mw.aiCommands.setFormationAssistant(&formation);
	GameMessage fm(MSG_DO_MOVETO_FORMATION, alice);
	fm.appendLocationArgument(Coord3D{ 105.0f, 405.0f, 0.0f });
	fm.appendRealArgument(3.14159265f);
	fm.appendIntegerArgument(0);
	fm.appendBooleanArgument(false);
	mw.send(fm);
	GameMessage mo(MSG_DO_MOVE_AND_ORIENTATE_OBJECTTO, alice);
	mo.appendObjectIDArgument(b->getID());
	mo.appendLocationArgument(Coord3D{ 305.0f, 405.0f, 0.0f });
	mo.appendRealArgument(1.5707963f);
	mw.send(mo);
	mw.frames(80);
	CHECK(a->getAIUpdateInterface()->isIdle());
	CHECK(b->getAIUpdateInterface()->isIdle());
	CHECK(dist2d(*a->getPosition(), Coord3D{ 105.0f, 405.0f, 0.0f }) < 15.0f);
	CHECK(dist2d(*b->getPosition(), Coord3D{ 305.0f, 405.0f, 0.0f }) < 15.0f);
	CHECK(std::fabs(a->getOrientation()) > 3.0f);                        // facing pi (either sign of the wrap)
	CHECK(b->getOrientation() == doctest::Approx(1.5707963f).epsilon(0.02));
	CHECK(mw.aiCommands.stats().formationMoves == 1);
	CHECK(mw.aiCommands.stats().orientMoves == 1);
}

TEST_CASE("commands: malformed messages are rejected and counted, an unknown type is reported unhandled, a second handler for a type is an error")
{
	MoveWorld mw;
	mw.buildMap();
	const int alice = mw.playerIndex("Alice");
	GameMessage noArg(MSG_DO_MOVETO, alice);
	mw.send(noArg);
	GameMessage badPlayer(MSG_DO_MOVETO, 999);
	badPlayer.appendLocationArgument(Coord3D{ 1, 1, 0 });
	mw.send(badPlayer);
	GameMessage unknown(MSG_DO_SPECIAL_POWER, alice);
	mw.send(unknown);
	mw.frames(1);
	CHECK(mw.aiCommands.stats().rejected == 2);
	CHECK(mw.dispatcher.unhandled().at(MSG_DO_MOVETO) == 2);
	CHECK(mw.dispatcher.unhandled().at(MSG_DO_SPECIAL_POWER) == 1);
	CHECK(mw.dispatcher.handlerLane(MSG_DO_MOVETO) == "MOVE-1");
	AICommands again;
	CHECK_THROWS_AS(again.registerHandlers(mw.dispatcher), std::logic_error);
}

TEST_CASE("commands: the same messages in two worlds give the same state hash in every frame")
{
	auto run = [](std::vector<std::uint32_t> &hashes) {
		MoveWorld mw;
		mw.buildMap();
		const int alice = mw.playerIndex("Alice");
		std::vector<Object *> us;
		for (int i = 0; i < 6; ++i)
		{
			us.push_back(mw.spawn("Walker", 105.0f + 40.0f * (float)i, 105.0f + 20.0f * (float)(i % 3)));
		}
		mw.frames(2);
		mw.select(alice, us);
		mw.moveTo(alice, 605.0f, 605.0f);
		for (int f = 0; f < 80; ++f)
		{
			mw.frames(1);
			hashes.push_back(mw.logic->computeStateHash());
		}
	};
	std::vector<std::uint32_t> h1, h2;
	run(h1);
	// a different heap layout: allocate and keep a few blocks so the second world's objects are not at the addresses of the first's
	std::vector<std::unique_ptr<char[]>> pad;
	for (int i = 1; i < 50; ++i)
	{
		pad.emplace_back(new char[(size_t)i * 37]);
	}
	run(h2);
	REQUIRE(h1.size() == h2.size());
	for (size_t i = 0; i < h1.size(); ++i)
	{
		INFO("frame " << i);
		CHECK(h1[i] == h2[i]);
	}
	CHECK(h1.front() != h1.back()); // the state really changes
}

TEST_CASE("commands: rotate firing arc turns selected units toward the target without moving")
{
 MoveWorld mw;
 mw.buildMap();
 const int alice=mw.playerIndex("Alice");
 Object *unit=mw.spawn("Walker",110.5f,110.5f);
 mw.frames(2);
 mw.select(alice,{unit});
 GameMessage order(MSG_DO_ROTATE_FIRINGARC,alice);
 order.appendLocationArgument({110.5f,410.5f,0});
 mw.send(order);
 mw.frames(30);
 CHECK(unit->getPosition()->x==110.5f);
 CHECK(unit->getPosition()->y==110.5f);
 // tolerance: recovered AIRotateFiringarc::update (BFME2 RVA 0x34274A) completes below 0.01 radians.
 CHECK(std::fabs(unit->getOrientation()-1.57079632679f)<0.01f);
}

namespace
{
const char *const kOneRowFormation = "FormationAssistant\n UnitDefinition Unit\n ObjectFilter = ALL\n End\n FormationTemplate Line\n Rows\n Row = Unit Unit\n End\n End\nEnd\n";

GameMessage formationOrder(int player, const Coord3D &where, float angle, bool attack)
{
	GameMessage m(MSG_DO_MOVETO_FORMATION, player);
	m.appendLocationArgument(where);
	m.appendRealArgument(angle);
	m.appendIntegerArgument(0);
	m.appendBooleanArgument(attack);
	return m;
}
}

// PR #88 review fix 4: RW 0x94E72B sends each slot's object an attack-move (RW 0x770CA5) when the order's attack flag is set
TEST_CASE("commands: a formation move with the attack flag arms an attack-move for every slot's unit; without it none")
{
	for (bool attack : { false, true })
	{
		MoveWorld mw;
		mw.buildMap();
		const int alice = mw.playerIndex("Alice");
		Object *a = mw.spawn("Walker", 105.0f, 105.0f);
		Object *b = mw.spawn("Walker", 145.0f, 105.0f);
		mw.frames(2);
		mw.select(alice, { a, b });
		FormationAssistant formation;
		std::string formationError;
		REQUIRE(formation.scan(kOneRowFormation, &formationError));
		mw.aiCommands.setFormationAssistant(&formation);
		mw.send(formationOrder(alice, Coord3D{ 125.0f, 405.0f, 0.0f }, 0.0f, attack));
		mw.frames(1);
		CHECK(mw.aiCommands.stats().formationMoves == 1);
		CHECK(mw.aiCommands.stats().rejected == 0);
		CHECK(a->getAIUpdateInterface()->attackMoveActive() == attack);
		CHECK(b->getAIUpdateInterface()->attackMoveActive() == attack);
		CHECK_FALSE(a->getAIUpdateInterface()->isIdle());
		CHECK_FALSE(b->getAIUpdateInterface()->isIdle());
	}
}

// PR #88 review fix 5: a peer's malformed formation / firing-arc order (a NaN or infinite angle or location, an angle outside [-2 pi, 2 pi]) is
// rejected, it never reaches the moves
TEST_CASE("commands: malformed formation and firing-arc orders with non-finite values are rejected and the simulation keeps running")
{
	MoveWorld mw;
	mw.buildMap();
	const int alice = mw.playerIndex("Alice");
	Object *a = mw.spawn("Walker", 105.0f, 105.0f);
	Object *b = mw.spawn("Walker", 145.0f, 105.0f);
	mw.frames(2);
	const Coord3D a0 = *a->getPosition(), b0 = *b->getPosition(); // after the spawn's snap to its cell
	mw.select(alice, { a, b });
	FormationAssistant formation;
	std::string formationError;
	REQUIRE(formation.scan(kOneRowFormation, &formationError));
	mw.aiCommands.setFormationAssistant(&formation);
	const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
	mw.send(formationOrder(alice, Coord3D{ 125.0f, 405.0f, 0.0f }, nan, false));
	mw.send(formationOrder(alice, Coord3D{ 125.0f, 405.0f, 0.0f }, inf, true));
	mw.send(formationOrder(alice, Coord3D{ 125.0f, 405.0f, 0.0f }, 1.0e20f, false)); // finite, but the facing wrap loop would never end
	mw.send(formationOrder(alice, Coord3D{ 125.0f, 405.0f, 0.0f }, -6.3f, false));
	mw.send(formationOrder(alice, Coord3D{ nan, 405.0f, 0.0f }, 0.0f, false));
	mw.send(formationOrder(alice, Coord3D{ 125.0f, -inf, 0.0f }, 0.0f, false));
	mw.send(formationOrder(alice, Coord3D{ 125.0f, 405.0f, nan }, 0.0f, false));
	for (const Coord3D &target : { Coord3D{ nan, 405.0f, 0.0f }, Coord3D{ 125.0f, inf, 0.0f }, Coord3D{ 125.0f, 405.0f, -inf } })
	{
		GameMessage arc(MSG_DO_ROTATE_FIRINGARC, alice);
		arc.appendLocationArgument(target);
		mw.send(arc);
	}
	mw.frames(30);
	CHECK(mw.aiCommands.stats().rejected == 10);
	CHECK(mw.aiCommands.stats().formationMoves == 0);
	for (Object *o : { a, b })
	{
		CHECK(o->getAIUpdateInterface()->isIdle());
		CHECK_FALSE(o->getAIUpdateInterface()->attackMoveActive());
		CHECK(std::isfinite(o->getOrientation()));
	}
	CHECK(a->getPosition()->x == a0.x);
	CHECK(a->getPosition()->y == a0.y);
	CHECK(b->getPosition()->x == b0.x);
	CHECK(b->getPosition()->y == b0.y);
	// the simulation survives: a well-formed order afterwards still moves the units
	mw.send(formationOrder(alice, Coord3D{ 125.0f, 405.0f, 0.0f }, 0.0f, false));
	mw.frames(1);
	CHECK(mw.aiCommands.stats().formationMoves == 1);
	CHECK_FALSE(a->getAIUpdateInterface()->isIdle());
	CHECK_FALSE(b->getAIUpdateInterface()->isIdle());
}
