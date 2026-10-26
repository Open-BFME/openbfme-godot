// OpenBFME unit tests: the map script engine, lane SCRIPT-4: the guard / idle / stand-ground orders, the sequential timer, the team moves to the nearest
// object, recruiting, the timed model condition and the conditions ported with them. GPL-3.0.
//
// Expected values follow the RotWK functions cited at each check (ScriptActionsOrders.cpp, AIGuardCommands.cpp, ScriptConditions.cpp), never the
// engine's own output.

#include "doctest.h"
#include "CombatTestUtil.h"
#include "LogicTestUtil.h"
#include "ScriptTestUtil.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/RandomValue.h"
#include "Common/Team.h"
#include "GameClient/ControlBarCommands.h"
#include "GameClient/MapChunks.h"
#include "GameLogic/AI/AIGuard.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/ScriptEngine/ScriptActions.h"
#include "GameLogic/ScriptEngine/ScriptConditions.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"

#include <cmath>
#include <string>
#include <vector>

using namespace scripttest;

namespace
{

void startEngine(GameLogic &logic)
{
	logic.partition().setRegion(0.0f, 0.0f, 1000.0f, 1000.0f); // the map's extent (RW 0x62FCCD), as LiveGame sets it: the closest-object searches walk it
	ScriptEngine::SideScripts side;
	side.sideName = "";
	static ScriptList empty;
	side.lists.push_back(&empty);
	logic.scriptEngine().newGame({ side }, nullptr, nullptr, false);
}

void run(ScriptEngine &e, const ScriptActionRec &a)
{
	ScriptActions::execute(e, a);
}

bool eval(ScriptEngine &e, const ScriptCondition &c)
{
	return ScriptConditions::evaluate(e, c);
}

std::string qual(Team *t)
{
	return t->getControllingPlayer()->getPlayerName() + "/" + t->getName();
}

int statusBit(const char *name)
{
	return ObjectTemplateInfoBuilder::objectStatusIndex(name);
}

int modelBit(const char *name)
{
	for (int i = 0; TheModelConditionNames[i]; ++i)
	{
		if (std::string(TheModelConditionNames[i]) == name)
		{
			return i;
		}
	}
	return -1;
}

MapObject waypoint(int id, const char *name, float x, float y)
{
	MapObject a;
	a.m_objectName = "*Waypoints/Waypoint";
	a.m_location = { x, y, 0.0f };
	a.m_properties.setInt("waypointID", id);
	a.m_properties.setAsciiString("waypointName", name);
	a.m_isWaypoint = true;
	return a;
}

WorldHeightMap flatMap(int w, int h)
{
	WorldHeightMap m;
	m.m_width = w;
	m.m_height = h;
	m.m_borderSize = 0;
	m.m_dataSize = w * h;
	m.m_data.assign((size_t)w * h, 0);
	m.m_hasBlendTileData = true;
	m.m_tileNdxes.assign((size_t)w * h, 0);
	m.m_blendTileNdxes.assign((size_t)w * h, 0);
	m.m_extraBlendTileNdxes.assign((size_t)w * h, 0);
	m.m_cliffInfoNdxes.assign((size_t)w * h, 0);
	m.m_planeStride = (w + 7) / 8;
	m.m_blendedTiles.assign(1, TBlendTileInfo());
	m.m_cliffInfo.assign(1, TCliffInfo());
	m.m_boundaries = { { w, h } };
	return m;
}

// a combat world with one waypoint "Post" at (600, 300)
struct WaypointWorld
{
	combattest::CombatWorld w;
	WorldHeightMap map = flatMap(101, 101);
	MapChunks chunks;
	TerrainLogic terrain;
	WaypointWorld()
	{
		chunks.objects = { waypoint(1, "Post", 600.0f, 300.0f) };
		std::vector<std::string> problems;
		terrain.init(map, chunks, &problems);
		REQUIRE(problems.empty());
		w.logic->setTerrain(&terrain);
		startEngine(*w.logic);
	}
	~WaypointWorld() { w.logic->setTerrain(nullptr); }
};

// a sequential record of the team (or unit) on the engine's list, as UNIT_ / TEAM_EXECUTE_SEQUENTIAL_SCRIPT appends one (RW 0x606FDA)
void appendRecord(ScriptEngine &e, const Script &s, std::uint32_t team, ObjectID object)
{
	ScriptEngine::SequentialScript r;
	r.team = team;
	r.object = object;
	r.scriptName = s.name;
	r.script = &s;
	e.appendSequentialScript(r);
}

} // namespace

TEST_CASE("script4 TEAM_IDLE_FOR_SECONDS / TEAM_GUARD_FOR_SECONDS (jump table RW 0x7CF857: both -> RW 0x7C1704): the team's group idles, and the first "
		  "chain head of the team waits n * 5 frames (RW 0x60396C, imul by RW 0xD9F608); the GUARD form idles too")
{
	combattest::CombatWorld w;
	startEngine(*w.logic);
	ScriptEngine &e = w.logic->scriptEngine();
	Object *a = w.unit("Swordsman", 'A', 100.0f, 100.0f);
	Object *b = w.unit("Swordsman", 'A', 160.0f, 100.0f);
	Team *t = w.teamA();
	auto seq = script("Seq", { { cond("CONDITION_TRUE", {}) } }, { act("INCREMENT_COUNTER", { ip(0, 1), sp(4, "c") }) });
	appendRecord(e, *seq, t->getID(), INVALID_ID);
	appendRecord(e, *seq, t->getID(), INVALID_ID); // the same team: appended to the first chain (only its head is timed)
	REQUIRE(e.sequentialScripts().size() == 1);
	REQUIRE(e.sequentialScripts()[0].size() == 2);
	for (Object *o : { a, b })
	{
		o->getAIUpdateInterface()->aiMoveToPosition(Coord3D{ 800.0f, 800.0f, 0.0f }, CMD_FROM_SCRIPT);
	}
	run(e, act("TEAM_GUARD_FOR_SECONDS", { sp(3, qual(t)), ip(0, 3) }));
	CHECK(e.sequentialScripts()[0][0].framesToWait == 15); // 3 s * 5
	CHECK(e.sequentialScripts()[0][1].framesToWait == -1);
	for (Object *o : { a, b })
	{
		CHECK(o->getAIUpdateInterface()->currentStateId() == (unsigned)AI_IDLE); // groupIdle (RW 0x771F49), not the guard state
	}
	run(e, act("TEAM_IDLE_FOR_FRAMECOUNT", { sp(3, qual(t)), ip(0, 7) }));
	CHECK(e.sequentialScripts()[0][0].framesToWait == 7);
	CHECK(e.stats().unportedActions.empty());
}

TEST_CASE("script4 UNIT_GUARD_FOR_FRAMECOUNT / NAMED_GUARD / TEAM_GUARD (RW 0x7C9D7D / 0x7C9306 / 0x7C9367 -> aiGuardPosition RW 0x771790 -> RW 0x6649C6): "
		  "state 16 at the unit's own spot in mode 0; the unit's chain head waits the frames")
{
	combattest::CombatWorld w;
	startEngine(*w.logic);
	ScriptEngine &e = w.logic->scriptEngine();
	Object *g = w.unit("Swordsman", 'A', 200.0f, 220.0f);
	g->setName("Guard");
	auto seq = script("Seq", { { cond("CONDITION_TRUE", {}) } }, { act("INCREMENT_COUNTER", { ip(0, 1), sp(4, "c") }) });
	appendRecord(e, *seq, 0, g->getID());
	run(e, act("UNIT_GUARD_FOR_FRAMECOUNT", { sp(14, "Guard"), ip(0, 12) }));
	AIUpdateInterface *ai = g->getAIUpdateInterface();
	CHECK(ai->currentStateId() == (unsigned)AI_GUARD);
	CHECK(ai->guardLocation().x == 200.0f);
	CHECK(ai->guardLocation().y == 220.0f);
	CHECK(ai->guardMode() == 0);
	CHECK(ai->lastCommandSource() == CMD_FROM_SCRIPT);
	CHECK(e.sequentialScripts()[0][0].framesToWait == 12);

	Object *m1 = w.unit("Swordsman", 'A', 300.0f, 400.0f);
	Object *m2 = w.unit("Swordsman", 'A', 340.0f, 400.0f);
	m2->setStatus((unsigned)statusBit("UNDER_CONSTRUCTION"), true); // RW 0x7C9367 skips status 2
	m1->getAIUpdateInterface()->aiMoveToPosition(Coord3D{ 900.0f, 900.0f, 0.0f }, CMD_FROM_SCRIPT);
	m2->getAIUpdateInterface()->aiMoveToPosition(Coord3D{ 900.0f, 900.0f, 0.0f }, CMD_FROM_SCRIPT);
	run(e, act("TEAM_GUARD", { sp(3, qual(w.teamA())) }));
	CHECK(m1->getAIUpdateInterface()->currentStateId() == (unsigned)AI_GUARD);
	CHECK(m1->getAIUpdateInterface()->guardLocation().x == 300.0f);
	CHECK(m2->getAIUpdateInterface()->currentStateId() != (unsigned)AI_GUARD);
	CHECK(e.stats().unportedActions.empty());
}

TEST_CASE("script4 UNIT_GUARD_POSITION / TEAM_GUARD_POSITION (RW 0x7C9400 / 0x7C01B1 -> group RW 0x7726F5): the guard location is the waypoint; a horde "
		  "member is refused (RW 0x6649D7: status 0x26)")
{
	WaypointWorld p;
	ScriptEngine &e = p.w.logic->scriptEngine();
	Object *u = p.w.unit("Swordsman", 'A', 100.0f, 100.0f);
	u->setName("U");
	run(e, act("UNIT_GUARD_POSITION", { sp(14, "U"), sp(7, "Post") }));
	CHECK(u->getAIUpdateInterface()->currentStateId() == (unsigned)AI_GUARD);
	CHECK(u->getAIUpdateInterface()->guardLocation().x == 600.0f);
	CHECK(u->getAIUpdateInterface()->guardLocation().y == 300.0f);
	Object *h = p.w.unit("SwordHorde", 'B', 500.0f, 700.0f);
	p.w.frames(2);
	run(e, act("TEAM_GUARD_POSITION", { sp(3, qual(p.w.teamB())), sp(7, "Post") }));
	CHECK(h->getAIUpdateInterface()->currentStateId() == (unsigned)AI_GUARD); // the horde object itself (no HORDE_MEMBER status)
	CHECK(h->getAIUpdateInterface()->guardLocation().x == 600.0f);
	const ContainModuleInterface::ContainedItemsList *items = h->getContain()->getContainedItemsList();
	REQUIRE(items);
	REQUIRE(!items->empty());
	AIUpdateInterface *memberAi = items->front()->getAIUpdateInterface();
	REQUIRE(memberAi);
	CHECK(memberAi->aiGuardPosition(Coord3D{ 1.0f, 2.0f, 0.0f }, 0, CMD_FROM_SCRIPT) == false); // RW 0x6649D7: a HORDE_MEMBER is refused
	CHECK(e.stats().unportedActions.empty());
}

TEST_CASE("script4 TEAM_STAND_GROUND (RW 0x7C6C2B): STAND_GROUND (status 0x44) on every member and, for a horde, on each member of its list; false clears it")
{
	combattest::CombatWorld w;
	startEngine(*w.logic);
	ScriptEngine &e = w.logic->scriptEngine();
	Object *h = w.unit("SwordHorde", 'B', 500.0f, 500.0f);
	Object *s = w.unit("Swordsman", 'B', 300.0f, 300.0f);
	w.frames(2);
	const int sg = statusBit("STAND_GROUND");
	const ContainModuleInterface::ContainedItemsList *items = h->getContain()->getContainedItemsList();
	REQUIRE(items);
	REQUIRE(items->size() == 6u); // InitialPayload = Swordsman 6
	run(e, act("TEAM_STAND_GROUND", { sp(3, qual(w.teamB())), ip(8, 1) }));
	CHECK(h->testStatus((unsigned)sg));
	CHECK(s->testStatus((unsigned)sg));
	int members = 0;
	for (Object *m : *items)
	{
		members += m->testStatus((unsigned)sg) ? 1 : 0;
	}
	CHECK(members == 6);
	run(e, act("TEAM_STAND_GROUND", { sp(3, qual(w.teamB())), ip(8, 0) }));
	CHECK(!h->testStatus((unsigned)sg));
	CHECK(!items->front()->testStatus((unsigned)sg));
}

TEST_CASE("script4 TEAM_MOVE_TO_NEAREST_OBJECT_OF_TYPE (RW 0x7C30F7) has no owner filter; _OWNED_BY_PLAYER (RW 0x7C31FF) only the player's objects: the "
		  "team's group moves to the one nearest its centre")
{
	combattest::CombatWorld w;
	startEngine(*w.logic);
	ScriptEngine &e = w.logic->scriptEngine();
	Object *mover = w.unit("Swordsman", 'A', 500.0f, 500.0f);
	Object *own = w.unit("Dummy", 'A', 560.0f, 500.0f);   // Alice's, 60 away
	Object *enemy = w.unit("Dummy", 'B', 500.0f, 700.0f); // Bob's, 200 away
	(void)own;
	(void)enemy;
	TeamFactory &teams = w.logic->players().teams();
	Team *t = teams.createTeam(teams.initTeam("Movers", w.playerOf('A'), true), true);
	REQUIRE(t);
	mover->setTeam(t);
	w.frames(2); // the partition registers the new objects on a frame
	// the team's centre is the mover's spot (500, 500): Alice's own dummy (60 away) is the nearest Dummy of any owner
	run(e, act("TEAM_MOVE_TO_NEAREST_OBJECT_OF_TYPE", { sp(3, qual(t)), sp(61, "Dummy") }));
	w.frames(60);
	const float toOwn = std::fabs(mover->getPosition()->x - 560.0f) + std::fabs(mover->getPosition()->y - 500.0f);
	CHECK(toOwn < 30.0f); // tolerance: the move ends at the dummy's spot within the arrival distance and the units' radii
	run(e, act("TEAM_MOVE_TO_NEAREST_OBJECT_OF_TYPE_OWNED_BY_PLAYER", { sp(3, qual(t)), sp(61, "Dummy"), sp(11, "Bob") }));
	w.frames(150);
	const float toEnemy = std::fabs(mover->getPosition()->x - 500.0f) + std::fabs(mover->getPosition()->y - 700.0f);
	CHECK(toEnemy < 40.0f); // tolerance: as above
	CHECK(e.stats().unportedActions.empty());
}

TEST_CASE("script4 TEAM_RECRUIT_UNITS_FROM_TEAM (RW 0x7C2C7A -> Team RW 0x7A449F): up to the count, the source prototype's members nearest the "
		  "destination's live centre by the squared 2D distance join it; a tie goes to the later candidate; the destination is made when missing")
{
	combattest::CombatWorld w;
	startEngine(*w.logic);
	ScriptEngine &e = w.logic->scriptEngine();
	Player *alice = w.playerOf('A');
	TeamFactory &teams = w.logic->players().teams();
	TeamPrototype *srcProto = teams.initTeam("Src", alice, false);
	TeamPrototype *dstProto = teams.initTeam("Dst", alice, true);
	REQUIRE(srcProto);
	REQUIRE(dstProto);
	Team *src = teams.createTeam(srcProto, true);
	REQUIRE(src);
	Object *anchor = w.unit("Swordsman", 'A', 500.0f, 500.0f);
	Team *dst = teams.createTeam(dstProto, true);
	REQUIRE(dst);
	anchor->setTeam(dst); // the destination's live centre: (500, 500)
	Object *tieFirst = w.unit("Dummy", 'A', 600.0f, 500.0f);  // 100 away
	Object *tieSecond = w.unit("Dummy", 'A', 500.0f, 600.0f); // 100 away, after it in the list
	Object *far = w.unit("Dummy", 'A', 800.0f, 500.0f);
	Object *wrongType = w.unit("Swordsman", 'A', 510.0f, 500.0f);
	for (Object *o : { tieFirst, tieSecond, far, wrongType })
	{
		o->setTeam(src);
	}
	run(e, act("TEAM_RECRUIT_UNITS_FROM_TEAM", { sp(3, qual(dst)), ip(0, 1), sp(61, "Dummy"), sp(3, qual(src)) }));
	CHECK(tieSecond->getTeam() == dst); // RW 0x7A4561: d <= best keeps the later of two equal candidates
	CHECK(tieFirst->getTeam() == src);
	run(e, act("TEAM_RECRUIT_UNITS_FROM_TEAM", { sp(3, qual(dst)), ip(0, 5), sp(61, "Dummy"), sp(3, qual(src)) }));
	CHECK(tieFirst->getTeam() == dst);
	CHECK(far->getTeam() == dst);
	CHECK(wrongType->getTeam() == src); // not of the type
	CHECK(e.stats().unportedActions.empty());
}

TEST_CASE("script4 UNIT_SET_MODELCONDITION_FOR_DURATION (RW 0x7C6404 -> Object RW 0x695209): a lone unit's timed bit for _ftol2(5 * seconds) frames, a "
		  "zero duration for good; a horde's share: cvttss2si(n * fraction + 0.5) members chosen with one logic random draw per member")
{
	combattest::CombatWorld w;
	startEngine(*w.logic);
	ScriptEngine &e = w.logic->scriptEngine();
	Object *u = w.unit("Swordsman", 'A', 200.0f, 200.0f);
	u->setName("U");
	const int bit = modelBit("USER_1");
	REQUIRE(bit >= 0); // tolerance: a precondition (the name is one of the 591), its index is not what is tested
	run(e, act("UNIT_SET_MODELCONDITION_FOR_DURATION", { sp(14, "U"), sp(58, "USER_1"), rp(1.5f), rp(1.0f) }));
	CHECK(u->testModelCondition(bit));
	w.frames(7); // 1.5 s * 5 = 7.5 -> _ftol2 truncates to 7 frames
	CHECK(!u->testModelCondition(bit));
	run(e, act("UNIT_SET_MODELCONDITION_FOR_DURATION", { sp(14, "U"), sp(58, "user_1"), rp(1.5f), rp(1.0f) })); // the name's case is kept (RW 0x4065AA)
	CHECK(!u->testModelCondition(bit));
	run(e, act("UNIT_SET_MODELCONDITION_FOR_DURATION", { sp(14, "U"), sp(58, "USER_1"), rp(0.0f), rp(1.0f) }));
	w.frames(30);
	CHECK(u->testModelCondition(bit)); // frames 0: the bit for good

	Object *h = w.unit("SwordHorde", 'B', 600.0f, 600.0f);
	w.frames(2);
	h->setName("H");
	const std::vector<Object *> members(h->getContain()->getContainedItemsList()->begin(), h->getContain()->getContainedItemsList()->end());
	REQUIRE(members.size() == 6u);
	w.logic->random().seedRandom(77);
	GameLogicRandom expect(w.logic->random().algorithm());
	expect.seedRandom(77);
	// RW 0x695209: wanted = cvttss2si(6 * 0.4 + 0.5) = 2; member i draws random(1, 6 - i) and takes the bit while the draw <= wanted
	int wanted = 2;
	std::vector<bool> takes;
	for (int remaining = 6; remaining >= 1; --remaining)
	{
		const int r = expect.getValue(1, remaining, "Object.cpp", 0xC48);
		takes.push_back(r <= wanted);
		if (r <= wanted)
		{
			--wanted;
		}
	}
	run(e, act("UNIT_SET_MODELCONDITION_FOR_DURATION", { sp(14, "H"), sp(58, "USER_1"), rp(10.0f), rp(0.4f) }));
	int taken = 0;
	for (size_t i = 0; i < members.size(); ++i)
	{
		CHECK(members[i]->testModelCondition(bit) == takes[i]);
		taken += members[i]->testModelCondition(bit) ? 1 : 0;
	}
	CHECK(taken == 2);
	CHECK(!h->testModelCondition(bit)); // the horde object itself is not one of its members
	CHECK(w.logic->random().getValue(0, 1000000, "t", 1) == expect.getValue(0, 1000000, "t", 1)); // exactly six draws
	CHECK(e.stats().unportedActions.empty());
}

namespace
{
// SwordHorde with Slots = 8 (HordeContain's TransportContain field, the horde interface's slot 0x17C)
const char *const kSlotHorde = "Object SlotHorde\n"
							   "  KindOf = HORDE MELEE_HORDE LARGE_RECTANGLE_PATHFIND SELECTABLE CAN_ATTACK\n"
							   "  VisionRange = 200\n"
							   "  Geometry = BOX\n"
							   "  GeometryMajorRadius = 30\n"
							   "  GeometryMinorRadius = 45\n"
							   "  GeometryHeight = 20\n"
							   "  Body = ImmortalBody ModuleTag_Body\n"
							   "    MaxHealth = 1\n"
							   "  End\n"
							   "  Behavior = HordeAIUpdate ModuleTag_AI\n"
							   "  End\n"
							   "  Behavior = HordeContain ModuleTag_Contain\n"
							   "    Slots = 8\n"
							   "    RankInfo = RankNumber:1 UnitType:Swordsman Position:X:50 Y:0 Position:X:50 Y:20 Position:X:50 Y:-20\n"
							   "    RankInfo = RankNumber:2 UnitType:Swordsman Position:X:30 Y:0 Position:X:30 Y:20 Position:X:30 Y:-20\n"
							   "    InitialPayload = Swordsman 6\n"
							   "  End\n"
							   "  LocomotorSet\n"
							   "    Locomotor = HordeLoco\n"
							   "    Condition = SET_NORMAL\n"
							   "    Speed = 50\n"
							   "  End\n"
							   "End\n";
} // namespace

TEST_CASE("script4 conditions: EVAL_TEAM_HEALTH (RW 0x7E98D2) never reads hit points (a lone unit counts 1 of 1, a horde its count of its Slots); "
		  "NAMED_BUILDING_IS_EMPTY (RW 0x7E4C56); PLAYER_HAS_OBJECT_OF_VETERANCY (RW 0x7E99F4) only for a one-player mask (RW 0x6A85B6)")
{
	combattest::CombatWorld w(kSlotHorde);
	startEngine(*w.logic);
	ScriptEngine &e = w.logic->scriptEngine();
	Object *s = w.unit("Swordsman", 'A', 200.0f, 200.0f);
	ActiveBody *body = dynamic_cast<ActiveBody *>(s->getBodyModule());
	REQUIRE(body);
	body->internalChangeHealth(-60.0f);
	REQUIRE(w.health(s) == 40.0f);
	Team *t = w.teamA();
	// op 2 is "==" (RW 0x7E99BB)
	CHECK(eval(e, cond("EVAL_TEAM_HEALTH", { sp(3, qual(t)), ip(6, 2), ip(0, 100) })));
	Object *h = w.unit("SlotHorde", 'B', 600.0f, 600.0f);
	w.frames(2);
	h->setName("H");
	// the horde interface's slot 0x17C is the Slots (8, RW 0x86C8DE), slot 0x180(0) its count (6): 6 * 100 / 8 = 75 (integer)
	CHECK(eval(e, cond("EVAL_TEAM_HEALTH", { sp(3, qual(w.teamB())), ip(6, 2), ip(0, 75) })));
	CHECK(!eval(e, cond("NAMED_BUILDING_IS_EMPTY", { sp(14, "H") })));          // a contain holding 6
	s->setName("S");
	CHECK(!eval(e, cond("NAMED_BUILDING_IS_EMPTY", { sp(14, "S") }))); // no contain: false
	// veterancy: rank 0 >= 0 for Alice's swordsman; "<All Players>" is a mask of several players -> no player (RW 0x6A85B6), false
	REQUIRE(s->getExperienceTracker());
	CHECK(eval(e, cond("PLAYER_HAS_OBJECT_OF_VETERANCY", { sp(11, "Alice"), sp(61, "Swordsman"), ip(6, 3), ip(0, 0) })));
	CHECK(!eval(e, cond("PLAYER_HAS_OBJECT_OF_VETERANCY", { sp(11, "Alice"), sp(61, "Swordsman"), ip(6, 4), ip(0, 0) })));
	CHECK(!eval(e, cond("PLAYER_HAS_OBJECT_OF_VETERANCY", { sp(11, "<All Players>"), sp(61, "Swordsman"), ip(6, 3), ip(0, 0) })));
	CHECK(e.stats().unportedConditions.empty());
}

TEST_CASE("script4 conditions: BUILT_BY_PLAYER (RW 0x7EA59C) counts dead objects too and needs the raw name to be a template; COMPARE_NUM_PLAYERS_IN_GAME "
		  "(RW 0x6A8630(0): playable sides not defeated); PLAYER_HAS_SCIENCEPURCHASEPOINTS (RW 0x7E50AE); NAMED_ATTACKED_BY_OBJECTTYPE (RW 0x7EA374)")
{
	combattest::CombatWorld w;
	startEngine(*w.logic);
	ScriptEngine &e = w.logic->scriptEngine();
	Object *d = w.unit("Dummy", 'B', 300.0f, 300.0f);
	CHECK(eval(e, cond("BUILT_BY_PLAYER", { sp(61, "Dummy"), sp(11, "Bob") })));
	CHECK(!eval(e, cond("BUILT_BY_PLAYER", { sp(61, "Dummy"), sp(11, "Alice") })));
	CHECK(!eval(e, cond("BUILT_BY_PLAYER", { sp(61, "NoSuchThing"), sp(11, "Bob") })));
	// Alice and Bob have the playable FactionA / FactionB, the neutral player FactionNeutral is no playable side: 2 (op 2 "==")
	CHECK(eval(e, cond("COMPARE_NUM_PLAYERS_IN_GAME", { ip(6, 2), ip(0, 2) })));
	CHECK(!eval(e, cond("PLAYER_HAS_SCIENCEPURCHASEPOINTS", { sp(11, "Alice"), ip(0, 1) })));
	CHECK(eval(e, cond("PLAYER_HAS_SCIENCEPURCHASEPOINTS", { sp(11, "Alice"), ip(0, 0) })));
	d->setName("D");
	CHECK(!eval(e, cond("NAMED_ATTACKED_BY_OBJECTTYPE", { sp(14, "D"), sp(61, "Swordsman") }))); // no damage record yet
	Object *attacker = w.unit("Swordsman", 'A', 330.0f, 300.0f);
	attacker->getAIUpdateInterface()->aiAttackObject(d, CMD_FROM_SCRIPT);
	const int ran = w.runUntil([&] { return eval(e, cond("NAMED_ATTACKED_BY_OBJECTTYPE", { sp(14, "D"), sp(61, "Swordsman") })); }, 200);
	CHECK(ran < 200); // tolerance: the first hit lands within the swing time, any frame under the limit
	CHECK(!eval(e, cond("NAMED_ATTACKED_BY_OBJECTTYPE", { sp(14, "D"), sp(61, "Dummy") })));
	CHECK(e.stats().unportedConditions.empty());
}

TEST_CASE("script4 conditions: NAMED_SELECTED / TYPE_SELECTED (RW 0x7EA988 / 0x7E77A2) read the local player's selection outside a multiplayer game (a LAN / "
		  "Internet mode or a start with several human slots); "
		  "TYPE_SIGHTED (RW 0x7EB1C9) has no relationship filter")
{
	combattest::CombatWorld w;
	startEngine(*w.logic);
	ScriptEngine &e = w.logic->scriptEngine();
	Object *s = w.unit("Swordsman", 'A', 200.0f, 200.0f);
	s->setName("S");
	Player *local = w.logic->players().getLocalPlayer();
	REQUIRE(local);
	CHECK(!eval(e, cond("NAMED_SELECTED", { sp(14, "S") })));
	local->selection().push_back(s->getID());
	CHECK(eval(e, cond("NAMED_SELECTED", { sp(14, "S") })));
	CHECK(eval(e, cond("TYPE_SELECTED", { sp(61, "Swordsman") })));
	CHECK(!eval(e, cond("TYPE_SELECTED", { sp(61, "Dummy") })));
	// lane SCRIPT-4 r2 (Sol): a start with several human slots is a multiplayer session (EconomyContext::multiplayerSession, set by LiveGame from the
	// start message): false on every peer, whatever the local selection; the LAN / Internet modes too
	EconomyContext &context = w.logic->economy().context();
	context.multiplayerSession = true;
	CHECK(!eval(e, cond("NAMED_SELECTED", { sp(14, "S") })));
	CHECK(!eval(e, cond("TYPE_SELECTED", { sp(61, "Swordsman") })));
	context.multiplayerSession = false;
	context.gameMode = EconomyContext::MODE_LAN;
	CHECK(!eval(e, cond("NAMED_SELECTED", { sp(14, "S") })));
	context.gameMode = EconomyContext::MODE_SKIRMISH;
	CHECK(eval(e, cond("NAMED_SELECTED", { sp(14, "S") })));
	// an own unit within the vision range (150) is sighted; one far away is not
	Object *near = w.unit("Dummy", 'A', 260.0f, 200.0f);
	(void)near;
	w.frames(2); // the partition registers the new objects on a frame
	CHECK(eval(e, cond("TYPE_SIGHTED", { sp(14, "S"), sp(61, "Dummy"), sp(11, "Alice") })));
	CHECK(!eval(e, cond("TYPE_SIGHTED", { sp(14, "S"), sp(61, "Dummy"), sp(11, "Bob") })));
	CHECK(e.stats().unportedConditions.empty());
}

namespace
{

const char *const kCommando = "Object Commando\n"
							  "  KindOf = INFANTRY SELECTABLE CAN_ATTACK\n"
							  "  CommandSet = Set_Commando\n"
							  "  Geometry = CYLINDER\n"
							  "  GeometryMajorRadius = 8\n"
							  "  GeometryMinorRadius = 8\n"
							  "  GeometryHeight = 20\n"
							  "  Body = ActiveBody ModuleTag_Body\n"
							  "    MaxHealth = 100\n"
							  "  End\n"
							  "  Behavior = AIUpdateInterface ModuleTag_AI\n"
							  "  End\n"
							  "  Behavior = PhysicsBehavior ModuleTag_Physics\n"
							  "  End\n"
							  "  LocomotorSet\n"
							  "    Locomotor = WalkerLoco\n"
							  "    Condition = SET_NORMAL\n"
							  "    Speed = 55\n"
							  "  End\n"
							  "End\n";

struct ButtonWorld
{
	combattest::CombatWorld w{ kCommando };
	CommandStore store;
	CommandStore *saved = nullptr;
	ButtonWorld()
	{
		saved = TheCommandStore;
		TheCommandStore = &store;
		store.setThingFactory(&w.w.things);
		w.w.fx.env.blocks.registerBlock("CommandButton", [](INI *ini) { CommandStore::parseCommandButtonDefinitionGlobal(ini); });
		w.w.fx.env.blocks.registerBlock("CommandSet", [](INI *ini) { CommandStore::parseCommandSetDefinitionGlobal(ini); });
		const std::string err = w.w.load("CommandButton Command_Halt\n  Command = STOP\nEnd\n"
										 "CommandButton Command_Upg\n  Command = OBJECT_UPGRADE\nEnd\n"
										 "CommandSet Set_Commando\n  1 = Command_Upg\n  2 = Command_Upg\n  3 = Command_Halt\nEnd\n",
			INI_LOAD_OVERWRITE, "cmd.ini");
		REQUIRE_MESSAGE(err.empty(), err);
		startEngine(*w.logic);
	}
	~ButtonWorld() { TheCommandStore = saved; }
	unsigned long long noteCount(const std::string &prefix) const
	{
		unsigned long long n = 0;
		for (const auto &kv : w.logic->scriptEngine().stats().notes)
		{
			n += kv.first.rfind(prefix, 0) == 0 ? kv.second : 0;
		}
		return n;
	}
};

} // namespace

TEST_CASE("script4 NAMED_ / TEAM_USE_COMMANDBUTTON_ABILITY (RW 0x7C6243 / 0x7C11EE -> Object::doCommandButton RW 0x696FD2): the named unit runs every "
		  "slot of its command set holding the button (twice for two slots); a team runs the button by name on every group member, command set or not; "
		  "STOP idles (RW 0x5E821A); a type without a port is noted (S-1190); an unknown button does nothing")
{
	ButtonWorld b;
	ScriptEngine &e = b.w.logic->scriptEngine();
	Object *c = b.w.unit("Commando", 'A', 200.0f, 200.0f);
	c->setName("C");
	Object *s = b.w.unit("Swordsman", 'A', 260.0f, 200.0f); // no command set
	run(e, act("NAMED_USE_COMMANDBUTTON_ABILITY", { sp(14, "C"), sp(34, "Command_Upg") }));
	CHECK(b.noteCount("[S-1190] NAMED_USE_COMMANDBUTTON_ABILITY: command button Command_Upg (command 7)") == 2); // slots 1 and 2
	run(e, act("NAMED_USE_COMMANDBUTTON_ABILITY", { sp(14, "C"), sp(34, "Command_Nothing") }));
	CHECK(b.noteCount("[S-1190]") == 2);
	for (Object *o : { c, s })
	{
		o->getAIUpdateInterface()->aiMoveToPosition(Coord3D{ 800.0f, 800.0f, 0.0f }, CMD_FROM_SCRIPT);
		REQUIRE(o->getAIUpdateInterface()->currentStateId() != (unsigned)AI_IDLE);
	}
	run(e, act("NAMED_USE_COMMANDBUTTON_ABILITY", { sp(14, "C"), sp(34, "Command_Halt") }));
	CHECK(c->getAIUpdateInterface()->currentStateId() == (unsigned)AI_IDLE);
	CHECK(s->getAIUpdateInterface()->currentStateId() != (unsigned)AI_IDLE);
	run(e, act("TEAM_USE_COMMANDBUTTON_ABILITY", { sp(42, qual(b.w.teamA())), sp(34, "Command_Halt") }));
	CHECK(s->getAIUpdateInterface()->currentStateId() == (unsigned)AI_IDLE); // RW 0x7C11EE: no command set test
	CHECK(e.stats().unportedActions.empty());
}

TEST_CASE("script4 SET_PLAYER_MONEY_TO_COUNTER (RW 0x7C3655): the mask's players' money summed; SET_PLAYER_KILLS_OF_TYPE_TO_COUNTER (RW 0x7C38F6): only "
		  "for a one-player mask, the counter is left alone otherwise")
{
	combattest::CombatWorld w;
	startEngine(*w.logic);
	ScriptEngine &e = w.logic->scriptEngine();
	const std::int32_t alice = w.cash('A'), bob = w.cash('B');
	REQUIRE(alice > 0); // tolerance: a precondition (the skirmish start money), the sum is what is tested
	run(e, act("SET_PLAYER_MONEY_TO_COUNTER", { sp(11, "Alice"), sp(4, "m") }));
	CHECK(e.counter("m").value == alice);
	run(e, act("SET_PLAYER_MONEY_TO_COUNTER", { sp(11, "<All Players>"), sp(4, "m") }));
	CHECK(e.counter("m").value == alice + bob); // the neutral player has no money
	e.counter("k").value = 42;
	run(e, act("SET_PLAYER_KILLS_OF_TYPE_TO_COUNTER", { sp(11, "<All Players>"), sp(61, "Dummy"), sp(4, "k") }));
	CHECK(e.counter("k").value == 42); // RW 0x6A85B6: no player for a mask of several
	run(e, act("SET_PLAYER_KILLS_OF_TYPE_TO_COUNTER", { sp(11, "Alice"), sp(61, "Dummy"), sp(4, "k") }));
	CHECK(e.counter("k").value == 0);
	CHECK(e.stats().unportedActions.empty());
}

TEST_CASE("script4 r2 (Sol): in a multiplayer session (several human slots at the start) the <Local Player> parameter is the current script player on every "
		  "peer (S-1181), so SET_PLAYER_MONEY_TO_COUNTER writes the same value everywhere")
{
	for (int session = 0; session < 2; ++session)
	{
		combattest::CombatWorld w;
		w.logic->economy().context().multiplayerSession = session == 1;
		Player *alice = w.playerOf('A');
		Player *bob = w.playerOf('B');
		REQUIRE(w.logic->players().getLocalPlayer() == alice);
		bob->getMoney()->deposit(1000, false);
		const std::int32_t aliceCash = w.cash('A'), bobCash = w.cash('B');
		REQUIRE(aliceCash != bobCash);
		// a script of Bob's side (the current script player) asks for <Local Player>'s money
		ScriptList neutral, aliceList, bobList;
		bobList.items.push_back(item(script("money", { { cond("CONDITION_TRUE", {}) } }, { act("SET_PLAYER_MONEY_TO_COUNTER", { sp(11, "<Local Player>"), sp(4, "m") }) },
			{}, true)));
		std::vector<ScriptEngine::SideScripts> sides(3);
		const char *names[] = { "", "Alice", "Bob" };
		ScriptList *lists[] = { &neutral, &aliceList, &bobList };
		for (size_t i = 0; i < 3; ++i)
		{
			sides[i].sideName = names[i];
			sides[i].lists.push_back(lists[i]);
		}
		ScriptEngine &e = w.logic->scriptEngine();
		e.newGame(sides, nullptr, nullptr, false);
		w.frames(1);
		const ScriptEngine::Counter *c = e.findCounter("Bob/m");
		REQUIRE(c != nullptr);
		const std::int32_t expected = session == 1 ? bobCash : aliceCash; // one machine: the local player; a session: the current script player on every peer
		CHECK(c->value == expected);
	}
}
