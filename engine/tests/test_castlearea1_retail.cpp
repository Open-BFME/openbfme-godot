// OpenBFME retail tests (lane CASTLEAREA-1) on a retail skirmish (Men against Mordor): a castle's area trigger (RW 0x79B881 / 0x79AB88) sets INSIDE_CASTLE on what
// enters it (RW 0x799157) and clears it on what leaves (RW 0x79935E); a save / load keeps both; the status clamps an inside structure's aim height for a shooter
// outside (RW 0x6CB85A); a critter afraid of castles paths 150 away from the keep. SKIP loudly when ROTWK_INSTALL / BFME2_INSTALL are unset.  GPL-3.0.

#include "doctest.h"

#include "BuildTestUtil.h"

#include "Common/Player.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/MapChunks.h"
#include "GameLogic/AI/AIMove.h"
#include "GameLogic/Combat/CombatLineOfSight.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Map/CastleTemplates.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/CastleModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"

#include <cmath>
#include <string>
#include <vector>

namespace
{
GameMessage selectMsg(int player, ObjectID id)
{
	GameMessage m(MSG_CREATE_SELECTED_GROUP, player);
	m.appendBooleanArgument(true);
	m.appendObjectIDArgument(id);
	return m;
}

Object *spawn(GameLogic &logic, const char *name, Player &owner, const Coord3D &at)
{
	const ThingTemplate *t = logic.things().findTemplate(name);
	REQUIRE_MESSAGE(t != nullptr, "retail template " << name);
	Object *o = logic.newObject(t, owner.getDefaultTeam(), ObjectStatusMaskType{});
	REQUIRE_MESSAGE(o != nullptr, name);
	Coord3D p = at;
	o->setPosition(&p);
	return o;
}

// the test's own even-odd test of a point against the area's world points (x0, y0, x1, y1, ..)
bool insidePolygon(const std::vector<float> &pts, float x, float y)
{
	bool in = false;
	const size_t n = pts.size() / 2;
	for (size_t i = 0, j = n - 1; i < n; j = i++)
	{
		const double xi = pts[2 * i], yi = pts[2 * i + 1], xj = pts[2 * j], yj = pts[2 * j + 1];
		if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi)
		{
			in = !in;
		}
	}
	return in;
}

unsigned insideCastle()
{
	return (unsigned)ObjectTemplateInfoBuilder::objectStatusIndex("INSIDE_CASTLE");
}

// a castle or camp flag captured by Men (a hero beside it: the capture scan RW 0x79B3C4), then its instant unpack: CastleFlagNE unpacks Castle_MenNE (one area of 23
// points in retail data, the keep GondorCastleBaseKeep), CampFlagNE Camp_MenNE (one area of 18 points; its GondorCampKeep is no CASTLE_KEEP: the camp has no keep)
struct CastleAreaGame
{
	buildtest::Game g;
	LiveGame *live = nullptr;
	GameLogic *logic = nullptr;
	Player *men = nullptr;
	Object *flag = nullptr;
	CastleBehavior *castle = nullptr;
	Coord3D inside{}, outside{};
	explicit CastleAreaGame(starttest::Shared &s, const char *flagName = "CastleFlagNE")
	{
		std::string error;
		REQUIRE_MESSAGE(buildtest::startGame(s, "FactionMen", "FactionMordor", 4242, g, &error), error);
		live = g.live.get();
		logic = &live->logic();
		men = live->players().findPlayerWithName(live->report().startSlotPlayers[0]);
		REQUIRE(men);
		const Coord3D at{ 1500.0f, 1500.0f, 0.0f };
		flag = spawn(*logic, flagName, *men, at);
		flag->friend_onBuildComplete();
		spawn(*logic, "GondorBoromir", *men, { at.x + 40.0f, at.y, at.z });
		for (int i = 0; i < 12; ++i)
		{
			logic->runLogicFrame();
		}
		REQUIRE(flag->getControllingPlayer() == men);
		live->dispatch().dispatch(selectMsg(men->getPlayerIndex(), flag->getID()));
		castle = dynamic_cast<CastleBehavior *>(flag->findModule("CastleBehavior"));
		REQUIRE(castle);
		REQUIRE(castle->areas().empty()); // a packed castle has none
		castle->initiateUnpack(true, nullptr);
		REQUIRE(castle->state() == CastleBehavior::STATE_UNPACKED);
		REQUIRE(castle->areas().size() == 1);
		// a point inside the area (the test's own polygon test) beside the centre, and one beyond its bounding box
		const std::vector<float> &pts = castle->areas()[0].points;
		float maxX = pts[0];
		for (size_t i = 0; i < pts.size(); i += 2)
		{
			maxX = std::max(maxX, pts[i]);
		}
		inside = { at.x + 12.5f, at.y + 7.5f, at.z };
		REQUIRE(insidePolygon(pts, inside.x, inside.y));
		outside = { maxX + 60.0f, at.y, at.z };
		REQUIRE_FALSE(insidePolygon(pts, outside.x, outside.y));
	}
	void place(Object &o, const Coord3D &p)
	{
		Coord3D q = p;
		o.setPosition(&q);
	}
	// out of the area: the exit is tested on the PREVIOUS integer cell (RW 0x69264D), so it is reported at the next cell move outside
	void leave(Object &o)
	{
		place(o, outside);
		place(o, { outside.x + 2.0f, outside.y, outside.z });
	}
};
} // namespace

TEST_CASE("castlearea1: Castle_MenNE's area is the base's line under the castle's transform, registered as a run-time trigger named as RW 0x7982A5 names it")
{
	OPENBFME_REQUIRE_START(s);
	CastleAreaGame c(*s);
	const CastleBehavior::Area &a = c.castle->areas()[0];
	std::string error;
	const CastleTemplate *layout = c.logic->castleTemplates().find("Castle_MenNE", &error);
	REQUIRE_MESSAGE(layout, error);
	REQUIRE(layout->lines.size() == 1);
	const std::vector<Point2F> &local = layout->lines[0].points;
	CHECK(local.size() == 23); // engine/tests/data/castle-survey.json: castle_menne.bse, 1 line, 23 points
	REQUIRE(a.points.size() == local.size() * 2);
	// the flag stands unrotated: the transform is the translation (x' = (0 y + 1 x) + 0 + tx)
	REQUIRE(c.flag->getOrientation() == 0.0f);
	for (size_t i = 0; i < local.size(); ++i)
	{
		CHECK(a.points[2 * i] == local[i].x + c.flag->getPosition()->x);
		CHECK(a.points[2 * i + 1] == local[i].y + c.flag->getPosition()->y);
	}
	CHECK(a.name == "Dynamic CastleFlagNE of id " + std::to_string(c.flag->getID()) + " at index 1");
	const ScriptEngine &engine = c.logic->scriptEngine();
	REQUIRE(a.trigger == (int)engine.triggerAreaCount() - 1); // the last trigger added: no frame ran since the unpack
	const TriggerArea &t = engine.triggerAreaAt((size_t)a.trigger);
	CHECK(t.name == a.name);
	REQUIRE(t.points.size() == local.size());
	CHECK(t.points[3].x == a.points[6]);
	CHECK(t.points[3].y == a.points[7]);
}

TEST_CASE("castlearea1: a unit moved into the castle's area gets INSIDE_CASTLE and joins the set (RW 0x799157); moved out it loses both (RW 0x79935E)")
{
	OPENBFME_REQUIRE_START(s);
	CastleAreaGame c(*s);
	Object *archer = spawn(*c.logic, "GondorArcher", *c.men, c.outside);
	c.logic->runLogicFrame();
	CHECK_FALSE(archer->testStatus(insideCastle()));
	c.place(*archer, c.inside);
	CHECK(archer->testStatus(insideCastle()));
	CHECK(c.castle->insideIds().count(archer->getID()) == 1);
	c.logic->runLogicFrame();
	c.place(*archer, c.outside); // the cell it left is still the one tested
	CHECK(archer->testStatus(insideCastle()));
	c.leave(*archer);
	CHECK_FALSE(archer->testStatus(insideCastle()));
	CHECK(c.castle->insideIds().count(archer->getID()) == 0);
}

TEST_CASE("castlearea1: a unit ordered to walk to an open camp's centre (Camp_MenNE: a castle's gate is shut) gets INSIDE_CASTLE on the way in, inside the area")
{
	OPENBFME_REQUIRE_START(s);
	CastleAreaGame c(*s, "CampFlagNE");
	Object *archer = spawn(*c.logic, "GondorArcher", *c.men, c.outside);
	c.logic->runLogicFrame();
	AIUpdateInterface *ai = archer->getAIUpdateInterface();
	REQUIRE(ai);
	ai->aiMoveToPosition(c.inside, CMD_FROM_PLAYER);
	int frames = 0;
	while (!archer->testStatus(insideCastle()) && frames < 900)
	{
		c.logic->runLogicFrame();
		++frames;
	}
	MESSAGE("frames to enter: " << frames);
	REQUIRE(archer->testStatus(insideCastle()));
	// the trigger tests the integer cell (RW 0x69264D): the cell the archer stands in is inside the area
	CHECK(insidePolygon(c.castle->areas()[0].points, (float)(int)archer->getPosition()->x, (float)(int)archer->getPosition()->y));
}

TEST_CASE("castlearea1: a save / load round trip keeps the area at its trigger index, the INSIDE_CASTLE status and the set; the loaded area still reports the exit")
{
	OPENBFME_REQUIRE_START(s);
	CastleAreaGame c(*s);
	Object *archer = spawn(*c.logic, "GondorArcher", *c.men, c.outside);
	c.logic->runLogicFrame();
	c.place(*archer, c.inside);
	c.logic->runLogicFrame();
	REQUIRE(archer->testStatus(insideCastle()));
	const ObjectID archerId = archer->getID(), flagId = c.flag->getID();
	const CastleBehavior::Area saved = c.castle->areas()[0];
	std::vector<std::uint8_t> bytes;
	std::string error;
	REQUIRE_MESSAGE(c.logic->saveSnapshot(bytes, &error), error);
	const std::uint32_t savedHash = c.logic->computeStateHash();
	// the game moves on: the archer leaves
	c.leave(*archer);
	REQUIRE_FALSE(archer->testStatus(insideCastle()));
	REQUIRE(c.logic->computeStateHash() != savedHash);
	REQUIRE_MESSAGE(c.live->loadSnapshot(bytes, &error), error);
	CHECK(c.logic->computeStateHash() == savedHash);
	Object *flag = c.logic->findObjectByID(flagId);
	archer = c.logic->findObjectByID(archerId);
	REQUIRE(flag);
	REQUIRE(archer);
	CastleBehavior *castle = dynamic_cast<CastleBehavior *>(flag->findModule("CastleBehavior"));
	REQUIRE(castle);
	CHECK(archer->testStatus(insideCastle()));
	CHECK(castle->insideIds().count(archerId) == 1);
	REQUIRE(castle->areas().size() == 1);
	CHECK(castle->areas()[0].trigger == saved.trigger);
	CHECK(castle->areas()[0].name == saved.name);
	CHECK(castle->areas()[0].points == saved.points);
	const TriggerArea &t = c.logic->scriptEngine().triggerAreaAt((size_t)saved.trigger);
	REQUIRE(t.points.size() * 2 == saved.points.size());
	CHECK(t.points[0].x == saved.points[0]);
	// the loaded trigger reaches the loaded castle
	c.leave(*archer);
	CHECK_FALSE(archer->testStatus(insideCastle()));
	CHECK(castle->insideIds().count(archerId) == 0);
}

TEST_CASE("castlearea1: an archer outside aims at a tall structure inside the castle at the clamped 40.0 (ClampedLOSHeightForCastleStructures, RW 0x6CB85A); "
		  "an archer inside aims at its full centre height")
{
	OPENBFME_REQUIRE_START(s);
	CastleAreaGame c(*s);
	CHECK(c.logic->settings().clampedLOSHeightForCastleStructures == 40.0f); // the constructor's default: retail gamedata.ini does not set it
	Object *tower = spawn(*c.logic, "GondorKeep", *c.men, c.inside); // made at its spot: its first position enters the area
	REQUIRE(tower->isKindOfName("STRUCTURE"));
	CHECK(tower->testStatus(insideCastle()));
	const float centreZ = c.logic->partition().geometryOf(*tower).centerZ;
	REQUIRE(centreZ > 40.0f); // tolerance: a precondition, the retail geometry is tall enough for the clamp to change the aim
	Object *archer = spawn(*c.logic, "GondorArcher", *c.men, c.outside);
	c.logic->runLogicFrame();
	REQUIRE_FALSE(archer->testStatus(insideCastle()));
	REQUIRE(archer->getWeapons());
	const Weapon *bow = archer->getWeapons()->weaponInSlot(0);
	REQUIRE(bow);
	const Coord3D out = CombatLineOfSight::victimLineOfSightPoint(*bow, *archer, *tower);
	CHECK(out.z == tower->getPosition()->z + 40.0f);
	c.place(*archer, c.inside);
	REQUIRE(archer->testStatus(insideCastle()));
	const Coord3D in = CombatLineOfSight::victimLineOfSightPoint(*bow, *archer, *tower);
	CHECK(in.z == tower->getPosition()->z + centreZ);
}

TEST_CASE("castlearea1: a sheep (AnimalAIUpdate, IGNORED_IN_GUI, AfraidOfCastles by default) entering the castle is not marked; it paths 150 away from the keep")
{
	OPENBFME_REQUIRE_START(s);
	CastleAreaGame c(*s);
	REQUIRE(c.castle->keepId() != INVALID_ID);
	const Object *keep = c.logic->findObjectByID(c.castle->keepId());
	REQUIRE(keep);
	Object *sheep = spawn(*c.logic, "Sheep", *c.men, c.outside);
	c.logic->runLogicFrame();
	AIUpdateInterface *ai = sheep->getAIUpdateInterface();
	REQUIRE(ai);
	CHECK(ai->hasAnimalInterface());
	CHECK(ai->animalAfraidOfCastles());
	c.place(*sheep, c.inside);
	CHECK_FALSE(sheep->testStatus(insideCastle()));
	CHECK(c.castle->insideIds().count(sheep->getID()) == 0);
	// the destination as RW 0x79923D computes it (float steps; the length's root is the double root of the float sum, stored as a float)
	const Coord3D &p = *sheep->getPosition(), &k = *keep->getPosition();
	const float dx = p.x - k.x, dy = p.y - k.y, dz = p.z - k.z;
	const float sum = (dz * dz + dy * dy) + dx * dx;
	const float len = (float)std::sqrt((double)sum);
	const float inv = 1.0f / len;
	const Coord3D want{ p.x + (dx * inv) * 150.0f, p.y + (inv * dy) * 150.0f, p.z + (inv * dz) * 150.0f };
	const Coord3D &got = ai->mover().requestedDestination();
	CHECK(got.x == want.x);
	CHECK(got.y == want.y);
	CHECK(got.z == want.z);
	// a unit with an AI but no animal interface is the castle's: an archer gets the status
	Object *archer = spawn(*c.logic, "GondorArcher", *c.men, c.outside);
	c.logic->runLogicFrame();
	REQUIRE_FALSE(archer->getAIUpdateInterface()->hasAnimalInterface());
	c.place(*archer, c.inside);
	CHECK(archer->testStatus(insideCastle()));
}

TEST_CASE("castlearea1: a pack frees the area (RW 0x79A37D): the trigger is emptied and no exit runs, so a structure inside keeps INSIDE_CASTLE as in retail")
{
	OPENBFME_REQUIRE_START(s);
	CastleAreaGame c(*s);
	// a structure the castle does not own (the pack destroys its members; the abandon sends the units out, RW 0x79C78C, through the still live area)
	Object *tower = spawn(*c.logic, "GondorKeep", *c.men, c.inside);
	REQUIRE(tower->testStatus(insideCastle()));
	const ObjectID towerId = tower->getID();
	const int trigger = c.castle->areas()[0].trigger;
	c.castle->abandonByPlayer();
	int frames = 0;
	while (c.castle->state() != CastleBehavior::STATE_IDLE && frames < 300)
	{
		c.logic->runLogicFrame();
		++frames;
	}
	REQUIRE(c.castle->state() == CastleBehavior::STATE_IDLE);
	CHECK(c.castle->areas().empty());
	CHECK(c.logic->scriptEngine().triggerAreaAt((size_t)trigger).points.empty());
	tower = c.logic->findObjectByID(towerId);
	REQUIRE(tower);
	CHECK(tower->testStatus(insideCastle()));
	CHECK(c.castle->insideIds().count(towerId) == 1);
}

TEST_CASE("castlearea1: a camp (Camp_MenNE) has an area but no keep (GondorCampKeep is a COMMANDCENTER, not a CASTLE_KEEP): a sheep is neither marked nor sent away")
{
	OPENBFME_REQUIRE_START(s);
	CastleAreaGame c(*s, "CampFlagNE");
	CHECK(c.castle->keepId() == INVALID_ID);
	Object *sheep = spawn(*c.logic, "Sheep", *c.men, c.outside);
	c.logic->runLogicFrame();
	AIUpdateInterface *ai = sheep->getAIUpdateInterface();
	REQUIRE(ai);
	const Coord3D before = ai->mover().requestedDestination();
	c.place(*sheep, c.inside);
	CHECK_FALSE(sheep->testStatus(insideCastle()));
	CHECK(c.castle->insideIds().count(sheep->getID()) == 0);
	CHECK(ai->mover().requestedDestination().x == before.x);
	CHECK(ai->mover().requestedDestination().y == before.y);
	// the camp's area still marks a soldier
	Object *archer = spawn(*c.logic, "GondorArcher", *c.men, c.outside);
	c.logic->runLogicFrame();
	c.place(*archer, c.inside);
	CHECK(archer->testStatus(insideCastle()));
}

TEST_CASE("castlearea1: a load keeps the run-time trigger list's extent: after a pack freed the last trigger, the next unpack takes the same index as in an unbroken game")
{
	OPENBFME_REQUIRE_START(s);
	CastleAreaGame c(*s);
	const int first = c.castle->areas()[0].trigger;
	REQUIRE(first == (int)c.logic->scriptEngine().triggerAreaCount() - 1); // the area is the list's last slot
	c.castle->abandonByPlayer();
	int frames = 0;
	while (c.castle->state() != CastleBehavior::STATE_IDLE && frames < 300)
	{
		c.logic->runLogicFrame();
		++frames;
	}
	REQUIRE(c.castle->state() == CastleBehavior::STATE_IDLE);
	const size_t extent = c.logic->scriptEngine().triggerAreaCount();
	CHECK(extent == (size_t)first + 1); // the freed slot stays
	// the packed castle went back to PlyrCivilian (RW 0x79CE0E): a Men hero takes it again (the capture scan, every 5 frames)
	spawn(*c.logic, "GondorBoromir", *c.men, { c.flag->getPosition()->x + 40.0f, c.flag->getPosition()->y, 0.0f });
	frames = 0;
	while (c.flag->getControllingPlayer() != c.men && frames < 60)
	{
		c.logic->runLogicFrame();
		++frames;
	}
	REQUIRE(c.flag->getControllingPlayer() == c.men);
	REQUIRE(c.logic->scriptEngine().triggerAreaCount() == extent);
	const ObjectID flagId = c.flag->getID(); // the load makes the objects again
	std::vector<std::uint8_t> bytes;
	std::string error;
	REQUIRE_MESSAGE(c.logic->saveSnapshot(bytes, &error), error);
	const std::uint32_t savedHash = c.logic->computeStateHash();
	REQUIRE_MESSAGE(c.live->loadSnapshot(bytes, &error), error);
	CHECK(c.logic->computeStateHash() == savedHash);
	CHECK(c.logic->scriptEngine().triggerAreaCount() == extent);
	CHECK(c.logic->scriptEngine().triggerAreaAt((size_t)first).points.empty());
	Object *flag = c.logic->findObjectByID(flagId);
	REQUIRE(flag);
	CastleBehavior *castle = dynamic_cast<CastleBehavior *>(flag->findModule("CastleBehavior"));
	REQUIRE(castle);
	castle->initiateUnpack(true, nullptr);
	REQUIRE(castle->areas().size() == 1);
	CHECK(castle->areas()[0].trigger == (int)extent); // an unbroken game's next index
}
