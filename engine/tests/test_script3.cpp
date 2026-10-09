// OpenBFME unit tests: the map script engine, lane SCRIPT-3: HAS_FINISHED_AUDIO's logic-side timer, ALLOW_DISALLOW_ONE_BUILDING, the science availability,
// the attitude and its mood matrix, the hunt state, the group attack and the waypoint path states with their link choice. GPL-3.0.
//
// Expected values follow the RotWK functions cited at each check (ScriptEngine.cpp, ScriptActionsUnits.cpp, AIHunt.cpp, AIWaypointPath.cpp,
// AIUpdateCombat.cpp), never the engine's own output.

#include "doctest.h"
#include "CombatTestUtil.h"
#include "LogicTestUtil.h"
#include "ScriptTestUtil.h"

#include "Common/BuildAssistant.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "GameClient/MapChunks.h"
#include "GameLogic/AI/AIHunt.h"
#include "GameLogic/AI/AIWaypointPath.h"
#include "GameLogic/Damage.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/CastleModules.h"
#include "GameLogic/Module/LogicModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ScriptEngine/ScriptActions.h"
#include "GameLogic/ScriptEngine/ScriptConditions.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"

#include <map>
#include <set>

using namespace scripttest;

namespace
{

const char *const kUnitIni = "Object Unit\n"
							 "  KindOf = SELECTABLE INFANTRY\n"
							 "  Draw = W3DDefaultDraw ModuleTag_Draw\n  End\n"
							 "  Body = ActiveBody ModuleTag_Body\n    MaxHealth = 100\n  End\n"
							 "End\n"
							 "Object Tower\n"
							 "  KindOf = SELECTABLE STRUCTURE\n"
							 "  Draw = W3DDefaultDraw ModuleTag_Draw\n  End\n"
							 "  Body = ActiveBody ModuleTag_Body\n    MaxHealth = 500\n  End\n"
							 "End\n";

// the script host of the audio tests: "Long" lasts 1999 ms, "Short" 0 ms, "Multi" 1000 ms of its first file, anything else has no event info
struct AudioHost : ScriptEngineHost
{
	int queries = 0;
	Object *createObject(const ThingTemplate &, Team &, const Coord3D &, float) override { return nullptr; }
	bool audioLengthMs(const std::string &name, std::int32_t &ms, bool &picked) override
	{
		++queries;
		picked = name == "Multi";
		static const std::map<std::string, std::int32_t> lengths = { { "Long", 1999 }, { "Short", 0 }, { "Multi", 1000 } };
		auto it = lengths.find(name);
		if (it == lengths.end())
		{
			return false;
		}
		ms = it->second;
		return true;
	}
};

void run(ScriptEngine &e, const ScriptActionRec &a)
{
	ScriptActions::execute(e, a);
}

// a team's script name, qualified with its owner (RW 0x604043: a plain name would be the current side's)
std::string qual(Team *t)
{
	return t->getControllingPlayer()->getPlayerName() + "/" + t->getName();
}

void startEngine(GameLogic &logic)
{
	ScriptEngine::SideScripts side;
	side.sideName = "";
	static ScriptList empty;
	side.lists.push_back(&empty);
	logic.scriptEngine().newGame({ side }, nullptr, nullptr, false);
}

} // namespace

TEST_CASE("script3 HAS_FINISHED_AUDIO (RW 0x759B84): the first query starts the timer (length / 200 ms truncated), true at its frame and consumed; no info: "
		  "finished at once without an entry; the list is hashed")
{
	ScriptGame g;
	g.start();
	AudioHost host;
	g.engine().setHost(&host);
	const ScriptCondition longer = cond("HAS_FINISHED_AUDIO", { sp(12, "Long") });
	const unsigned start = g.lw.logic->getFrame();
	const std::uint32_t before = g.lw.logic->computeStateHash();
	// 1999 / 200 = 9.995 -> 9 frames
	CHECK_FALSE(ScriptConditions::evaluate(g.engine(), longer));
	REQUIRE(g.engine().audioTimers().size() == 1);
	CHECK(g.engine().audioTimers()[0].name == "Long");
	CHECK(g.engine().audioTimers()[0].endFrame == start + 9);
	CHECK(g.lw.logic->computeStateHash() != before);
	for (int i = 0; i < 8; ++i)
	{
		g.frame();
		CHECK_FALSE(ScriptConditions::evaluate(g.engine(), longer));
	}
	g.frame();
	CHECK(g.lw.logic->getFrame() == start + 9);
	CHECK(ScriptConditions::evaluate(g.engine(), longer)); // frame >= end: true, the entry removed (RW 0x702055)
	CHECK(g.engine().audioTimers().empty());
	// the next query starts a new timer
	CHECK_FALSE(ScriptConditions::evaluate(g.engine(), longer));
	CHECK(g.engine().audioTimers().size() == 1);
	// a zero-length event is finished at its first query (end = now); an unknown event has no info: true, nothing kept
	CHECK(ScriptConditions::evaluate(g.engine(), cond("HAS_FINISHED_AUDIO", { sp(12, "Short") })));
	CHECK(ScriptConditions::evaluate(g.engine(), cond("HAS_FINISHED_AUDIO", { sp(12, "Nothing") })));
	CHECK(g.engine().audioTimers().size() == 1);
	// a multi-file sound reports the S-1187 choice
	CHECK_FALSE(ScriptConditions::evaluate(g.engine(), cond("HAS_FINISHED_AUDIO", { sp(12, "Multi") })));
	bool noted = false;
	for (const auto &kv : g.engine().stats().notes)
	{
		noted = noted || kv.first.find("[S-1187]") != std::string::npos;
	}
	CHECK(noted);
	CHECK(g.engine().stats().unportedConditions.empty());
}

TEST_CASE("script3 ALLOW_DISALLOW_ONE_BUILDING (RW 0x7BDB45): the players of the parameter forbid / allow the template id (Player + 0x720) that "
		  "allowedToBuild (RW 0x6AC856) reads")
{
	ScriptGame g;
	LogicModules::registerAll(g.lw.w.modules);
	const std::string err = g.lw.w.load(kUnitIni, INI_LOAD_OVERWRITE, "units.ini");
	REQUIRE_MESSAGE(err.empty(), err);
	g.start();
	Player *alice = g.lw.players.findPlayerWithName("Alice");
	Player *bob = g.lw.players.findPlayerWithName("Bob");
	const ThingTemplate *tower = g.lw.logic->things().findTemplate("Tower");
	REQUIRE(tower);
	CHECK(BuildAssistant::playerAllowedToBuild(*alice, tower, *g.lw.logic));
	run(g.engine(), act("ALLOW_DISALLOW_ONE_BUILDING", { sp(11, "Alice"), sp(15, "Tower"), ip(8, 0) }));
	CHECK(alice->isTemplateDisabled(tower->getTemplateID()));
	CHECK_FALSE(bob->isTemplateDisabled(tower->getTemplateID()));
	CHECK_FALSE(BuildAssistant::playerAllowedToBuild(*alice, tower, *g.lw.logic));
	run(g.engine(), act("ALLOW_DISALLOW_ONE_BUILDING", { sp(11, "Alice"), sp(15, "Tower"), ip(8, 1) }));
	CHECK(BuildAssistant::playerAllowedToBuild(*alice, tower, *g.lw.logic));
	// an unknown template: nothing
	run(g.engine(), act("ALLOW_DISALLOW_ONE_BUILDING", { sp(11, "Alice"), sp(15, "NoSuchThing"), ip(8, 0) }));
	CHECK(BuildAssistant::playerAllowedToBuild(*alice, tower, *g.lw.logic));
	CHECK(g.engine().stats().unportedActions.empty());
}

TEST_CASE("script3 Player::setScienceAvailability (RW 0x6AE412): the first disabled entry is erased, else the first hidden one; Disabled / Hidden "
		  "append; Available only erases")
{
	logictest::LogicWorld lw;
	Player &p = *lw.players.findPlayerWithName("Alice");
	const ScienceType a = (ScienceType)11, b = (ScienceType)12;
	p.setScienceAvailability(a, 1);
	CHECK(p.isScienceDisabled(a));
	CHECK_FALSE(p.isScienceHidden(a));
	p.setScienceAvailability(a, 2); // out of the disabled list, into the hidden list
	CHECK_FALSE(p.isScienceDisabled(a));
	CHECK(p.isScienceHidden(a));
	p.setScienceAvailability(a, 0);
	CHECK_FALSE(p.isScienceHidden(a));
	// a science in both lists (two scripts): one call erases only the disabled entry (RW 0x6AE448 skips the hidden search)
	p.setScienceAvailability(b, 2);
	p.setScienceAvailability(b, 1); // the hidden entry is erased, a disabled one appended
	CHECK(p.isScienceDisabled(b));
	CHECK_FALSE(p.isScienceHidden(b));
	const std::uint32_t h0 = lw.logic->computeStateHash();
	p.setScienceAvailability(b, 0);
	CHECK(lw.logic->computeStateHash() != h0); // the lists are hashed
}

TEST_CASE("script3 attitude (RW 0x66E12A, 0x664CD6, 0x664D7E) and the idle scan (RW 0x668748): a computer player's PASSIVE unit answers only its last "
		  "attacker, -3 looks for nothing; a human player's units ignore the attitude")
{
	combattest::CombatWorld w;
	startEngine(*w.logic);
	Object *a = w.unit("Swordsman", 'A', 300.0f, 300.0f);
	Object *dummy = w.unit("Dummy", 'B', 360.0f, 300.0f); // within the vision range (150)
	a->setName("A1");
	AIUpdateInterface *ai = a->getAIUpdateInterface();
	REQUIRE(ai);
	w.playerOf('A')->setPlayerType(PLAYER_COMPUTER, true);
	CHECK(ai->attitude() == 0);
	CHECK(ai->moodMatrixValue() == 0x402u); // a computer player, NORMAL
	run(w.logic->scriptEngine(), act("TEAM_SET_ATTITUDE", { sp(3, qual(w.teamA())), ip(20, -1) }));
	CHECK(ai->attitude() == -1);
	CHECK(ai->moodMatrixValue() == 0x202u);
	CHECK(ai->nextMoodTarget() == nullptr); // passive, never hit
	// the dummy hits it: the last damage source is the answer
	DamageInfo info;
	info.m_input.m_damageType = DAMAGE_UNRESISTABLE;
	info.m_input.m_deathType = DEATH_NORMAL;
	info.m_input.m_sourceID = dummy->getID();
	info.m_input.m_amount = 1.0f;
	a->getBodyModule()->attemptDamage(info);
	CHECK(ai->nextMoodTarget() == dummy);
	// -3: nothing, and a current victim is dropped (the unit idles)
	REQUIRE(ai->aiAttackObject(dummy, CMD_FROM_PLAYER));
	CHECK(ai->currentVictim() == dummy);
	run(w.logic->scriptEngine(), act("NAMED_SET_ATTITUDE", { sp(14, "A1"), ip(20, -3) }));
	CHECK(ai->attitude() == -3);
	CHECK(ai->currentVictim() == nullptr);
	CHECK(ai->currentStateId() == (unsigned)AI_IDLE);
	CHECK(ai->moodMatrixValue() == 0x2002u);
	CHECK(ai->nextMoodTarget() == nullptr);
	// the other moods: their bits
	const std::pair<int, unsigned> moods[] = { { -2, 0x102u }, { 0, 0x402u }, { 1, 0x802u }, { 2, 0x1002u }, { 7, 0x402u } };
	for (const auto &m : moods)
	{
		ai->setAttitude(m.first);
		CHECK(ai->moodMatrixValue() == m.second);
	}
	// a human player's unit: MM_Controller_Player whatever the attitude
	w.playerOf('A')->setPlayerType(PLAYER_HUMAN, true);
	ai->setAttitude(-3);
	CHECK(ai->moodMatrixValue() == 1u);
	CHECK(ai->nextMoodTarget() == dummy);
	CHECK(w.logic->scriptEngine().stats().unportedActions.empty());
}

TEST_CASE("script3 TEAM_HUNT / NAMED_HUNT (RW 0x7C0347 / 0x7C9531 -> RW 0x6644E6, state 0x11 RW 0x741133): the hunter finds the closest enemy across the map, "
		  "kills it, and with no enemy left the hunt succeeds into idle")
{
	combattest::CombatWorld w;
	startEngine(*w.logic);
	Object *h = w.unit("Swordsman", 'A', 150.0f, 150.0f);
	Object *far1 = w.unit("Dummy", 'B', 700.0f, 700.0f); // far beyond the vision range: an idle swordsman would never see it
	Object *far2 = w.unit("Dummy", 'B', 850.0f, 850.0f);
	h->setName("Hunter");
	AIUpdateInterface *ai = h->getAIUpdateInterface();
	w.frames(3);
	CHECK(ai->currentStateId() == (unsigned)AI_IDLE);
	run(w.logic->scriptEngine(), act("NAMED_HUNT", { sp(14, "Hunter") }));
	REQUIRE(ai->currentStateId() == (unsigned)AI_HUNT);
	const AIHuntState *hunt = dynamic_cast<const AIHuntState *>(ai->stateMachine().currentState());
	REQUIRE(hunt);
	// onEnter: the first scan within GameLogicRandomValue(0, 15) frames (RW 0x743BF5)
	CHECK(hunt->nextEnemyScanFrame() >= w.logic->getFrame());
	CHECK(hunt->nextEnemyScanFrame() <= w.logic->getFrame() + 15u);
	const ObjectID id1 = far1->getID(), id2 = far2->getID();
	const int ran = w.runUntil([&] { return !w.byId(id1) && !w.byId(id2); }, 3000);
	CHECK(ran < 3000);
	// nothing left: the hunt machine idles without a victim and the state succeeds (RW 0x74820A) into idle
	w.frames(40);
	CHECK(ai->currentStateId() == (unsigned)AI_IDLE);
	// a refused hunt: a projectile / horde member / immobile object stays as it is (RW 0x66450F); a non-AI script target is ignored
	run(w.logic->scriptEngine(), act("TEAM_HUNT", { sp(3, "NoSuchTeam") }));
	CHECK(w.logic->scriptEngine().stats().unportedActions.empty());
}

TEST_CASE("script3 TEAM_ATTACK_NAMED (RW 0x7C4613 -> AIGroup RW 0x77208A): every member of the team's AI group attacks the unit, closest first")
{
	combattest::CombatWorld w;
	startEngine(*w.logic);
	Object *victim = w.unit("Dummy", 'B', 600.0f, 600.0f);
	victim->setName("Victim");
	std::vector<Object *> team;
	for (int i = 0; i < 3; ++i)
	{
		team.push_back(w.unit("Swordsman", 'A', 100.0f + 60.0f * (float)i, 100.0f));
	}
	Team *t = w.teamA();
	REQUIRE(t);
	run(w.logic->scriptEngine(), act("TEAM_ATTACK_NAMED", { sp(3, qual(t)), sp(14, "Victim") }));
	for (Object *o : team)
	{
		CHECK(o->getAIUpdateInterface()->currentVictim() == victim);
		CHECK(o->getAIUpdateInterface()->isAttacking());
	}
}

namespace
{

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

MapObject waypoint(int id, const char *name, float x, float y)
{
	MapObject a;
	a.m_objectName = "*Waypoints/Waypoint";
	a.m_location = { x, y, 0.0f };
	a.m_properties.setInt("waypointID", id);
	a.m_properties.setAsciiString("waypointName", name);
	a.m_properties.setAsciiString("waypointPathLabel1", "Path");
	a.m_isWaypoint = true;
	return a;
}

// a fork: 1 -> { 2, 3 } -> 4
struct PathWorld
{
	combattest::CombatWorld w;
	WorldHeightMap map = flatMap(101, 101);
	MapChunks chunks;
	TerrainLogic terrain;
	explicit PathWorld(unsigned seed)
	{
		chunks.objects = { waypoint(1, "W1", 300.0f, 300.0f), waypoint(2, "W2", 500.0f, 200.0f), waypoint(3, "W3", 200.0f, 500.0f),
			waypoint(4, "W4", 600.0f, 600.0f) };
		chunks.waypointLinks = { { 1, 2 }, { 1, 3 }, { 2, 4 }, { 3, 4 } };
		std::vector<std::string> problems;
		terrain.init(map, chunks, &problems);
		REQUIRE(problems.empty());
		w.logic->setTerrain(&terrain);
		w.logic->random().seedRandom(seed);
		startEngine(*w.logic);
	}
	~PathWorld() { w.logic->setTerrain(nullptr); }
};

} // namespace

TEST_CASE("script3 NAMED_FOLLOW_WAYPOINTS (RW 0x7C9196, state 3 RW 0x755A20): the closest waypoint first, then at each waypoint a link drawn with the "
		  "logic generator (RW 0x74292D); both branches of a fork occur over seeds; the end of the path idles")
{
	std::set<int> branches;
	for (unsigned seed : { 1u, 7u, 1234u, 0x5EEDu, 99991u, 0x9E3779B9u, 31337u, 0xC0FFEEu })
	{
		PathWorld p(seed);
		Object *u = p.w.unit("Dummy", 'A', 250.0f, 250.0f);
		u->setName("Walker");
		AIUpdateInterface *ai = u->getAIUpdateInterface();
		run(p.w.logic->scriptEngine(), act("NAMED_FOLLOW_WAYPOINTS", { sp(14, "Walker"), sp(24, "Path") }));
		REQUIRE(ai->currentStateId() == (unsigned)AI_FOLLOW_WAYPOINT_PATH_AS_INDIVIDUALS);
		const auto *s = dynamic_cast<const AIFollowWaypointPathState *>(ai->stateMachine().currentState());
		REQUIRE(s);
		CHECK(s->currentWaypoint() == 1);
		int via = 0;
		const int ran = p.w.runUntil(
			[&] {
				if (const auto *cur = dynamic_cast<const AIFollowWaypointPathState *>(ai->stateMachine().currentState()))
				{
					if (cur->currentWaypoint() == 2 || cur->currentWaypoint() == 3)
					{
						via = cur->currentWaypoint();
					}
				}
				return ai->currentStateId() == (unsigned)AI_IDLE;
			},
			2000);
		CHECK(ran < 2000);
		CHECK((via == 2 || via == 3));
		MESSAGE("seed " << seed << ": via waypoint " << via << " after " << ran << " frames");
		branches.insert(via);
		const Coord3D &pos = *u->getPosition();
		CHECK((pos.x - 600.0f) * (pos.x - 600.0f) + (pos.y - 600.0f) * (pos.y - 600.0f) < 40.0f * 40.0f);
		CHECK(p.w.logic->scriptEngine().stats().unportedActions.empty());
	}
	CHECK(branches.size() == 2);
}

TEST_CASE("script3 TEAM_FOLLOW_WAYPOINTS as a team (RW 0x7BFE32 -> command 7, state 2): the members share the team's current waypoint (team + 0x6C) "
		  "and all take the same branch")
{
	for (unsigned seed = 1; seed <= 4; ++seed)
	{
		PathWorld p(seed);
		std::vector<Object *> members;
		for (int i = 0; i < 3; ++i)
		{
			members.push_back(p.w.unit("Dummy", 'A', 220.0f + 25.0f * (float)i, 240.0f));
		}
		Team *t = p.w.teamA();
		run(p.w.logic->scriptEngine(), act("TEAM_FOLLOW_WAYPOINTS", { sp(3, qual(t)), sp(24, "Path"), ip(8, 1), ip(8, 0) }));
		CHECK(t->currentWaypointId() == 1);
		std::set<int> seen;
		const int ran = p.w.runUntil(
			[&] {
				bool allIdle = true;
				for (Object *o : members)
				{
					AIUpdateInterface *ai = o->getAIUpdateInterface();
					if (const auto *cur = dynamic_cast<const AIFollowWaypointPathState *>(ai->stateMachine().currentState()))
					{
						if (cur->currentWaypoint() == 2 || cur->currentWaypoint() == 3)
						{
							seen.insert(cur->currentWaypoint());
						}
					}
					allIdle = allIdle && ai->currentStateId() == (unsigned)AI_IDLE;
				}
				return allIdle;
			},
			3000);
		CHECK(ran < 3000);
		CHECK(seen.size() == 1); // one draw for the team: every member walks the same branch
	}
}

TEST_CASE("script3 a one-shot script ends after its false actions too (RW 0x6099DC: 0x609C0F -> 0x609C16); a repeating script runs them every frame")
{
	ScriptGame g;
	g.lists[1].items.push_back(item(script("once", { { cond("CONDITION_FALSE", {}) } }, { act("INCREMENT_COUNTER", { ip(0, 1), sp(4, "yes") }) },
		{ act("INCREMENT_COUNTER", { ip(0, 1), sp(4, "no") }) }, true)));
	g.lists[1].items.push_back(item(script("again", { { cond("CONDITION_FALSE", {}) } }, {}, { act("INCREMENT_COUNTER", { ip(0, 1), sp(4, "again") }) })));
	// a one-shot without false actions stays active while its conditions are false
	g.lists[1].items.push_back(item(script("waits", { { cond("CONDITION_FALSE", {}) } }, { act("INCREMENT_COUNTER", { ip(0, 1), sp(4, "w") }) }, {}, true)));
	g.start();
	for (int i = 0; i < 4; ++i)
	{
		g.frame();
	}
	CHECK(g.counter("Alice/no") == 1);
	CHECK(g.counter("Alice/yes") == -999);
	CHECK(g.counter("Alice/again") == 4);
	const auto &root = g.engine().sides()[1].root;
	REQUIRE(root.scripts.size() == 3);
	CHECK_FALSE(root.scripts[0]->active);
	CHECK(root.scripts[1]->active);
	CHECK(root.scripts[2]->active);
}

TEST_CASE("script3 CAN_BUILD_AT_BASE (RW 0x7E66EE): a BASE_FOUNDATION unit of the player whose FoundationAIUpdate holds no occupant; another player, an "
		  "occupied plot or a unit that is no foundation: false")
{
	ScriptGame g;
	LogicModules::registerAll(g.lw.w.modules);
	const std::string err = g.lw.w.load(std::string(kUnitIni) +
											"Object Plot\n"
											"  KindOf = BASE_FOUNDATION IMMOBILE\n"
											"  Body = ActiveBody ModuleTag_Body\n    MaxHealth = 100\n  End\n"
											"  Behavior = FoundationAIUpdate ModuleTag_Foundation\n  End\n"
											"  Behavior = CastleMemberBehavior ModuleTag_Member\n  End\n"
											"End\n",
		INI_LOAD_OVERWRITE, "plots.ini");
	REQUIRE_MESSAGE(err.empty(), err);
	g.lw.logic->settings().bodyThresholdsLoaded = true; // GameData's UnitDamagedThreshold / UnitReallyDamagedThreshold (the bodies use the defaults)
	g.lw.logic->settings().unitDamagedThreshold = 0.65f;
	g.lw.logic->settings().unitReallyDamagedThreshold = 0.4f;
	g.start();
	Object *plot = g.lw.make("Plot", g.lw.teamOf("Alice"));
	Object *unit = g.lw.make("Unit", g.lw.teamOf("Alice"));
	REQUIRE(plot);
	REQUIRE(unit);
	plot->setName("Plot");
	unit->setName("Unit");
	auto can = [&](const char *player, const char *name) { return ScriptConditions::evaluate(g.engine(), cond("CAN_BUILD_AT_BASE", { sp(11, player), sp(14, name) })); };
	CHECK(can("Alice", "Plot"));
	CHECK_FALSE(can("Bob", "Plot")); // the unit's controlling player must be the parameter's first player
	CHECK_FALSE(can("Alice", "Unit")); // neither a castle nor a BASE_FOUNDATION
	CHECK_FALSE(can("Alice", "Nobody"));
	// a structure on the plot (FoundationAIUpdate slot 0xC: the occupant): no room
	CastleMemberBehavior *member = dynamic_cast<CastleMemberBehavior *>(plot->findModule("CastleMemberBehavior"));
	REQUIRE(member);
	member->setOccupantId(unit->getID());
	CHECK_FALSE(can("Alice", "Plot"));
	CHECK(g.engine().stats().unportedConditions.empty());
}

// ---- lane SCRIPT-3 round 2 (review r1): the four retail corrections, each pinned -------------------------------------------------------------------

TEST_CASE("script3 r2 hunt: the hunt machine's idle state draws the logic generator on entry (RW 0x748A4F, AIStates.cpp line 0x94A) after the scan "
		  "offset (RW 0x743BF5, line 0x3C92)")
{
	combattest::CombatWorld w;
	startEngine(*w.logic);
	Object *h = w.unit("Swordsman", 'A', 150.0f, 150.0f); // no enemy anywhere: the attack state (the default) fails at once into idle
	h->setName("Hunter");
	w.frames(2);
	w.logic->random().enableCallLog(true);
	w.logic->random().clearCallLog();
	run(w.logic->scriptEngine(), act("NAMED_HUNT", { sp(14, "Hunter") }));
	REQUIRE(h->getAIUpdateInterface()->currentStateId() == (unsigned)AI_HUNT);
	const auto &log = w.logic->random().callLog();
	int scan = -1, idle = -1;
	for (size_t i = 0; i < log.size(); ++i)
	{
		if (log[i].line == 0x3C92 && scan < 0)
		{
			scan = (int)i;
			CHECK(log[i].lo == 0);
			CHECK(log[i].hi == 3 * LOGICFRAMES_PER_SECOND);
		}
		if (log[i].line == 0x94A && idle < 0)
		{
			idle = (int)i;
			CHECK(log[i].lo == 0);
			CHECK(log[i].hi == 2 * LOGICFRAMES_PER_SECOND);
			CHECK(log[i].drewNumber);
		}
	}
	CHECK(scan >= 0);
	CHECK(idle > scan);
	w.logic->random().enableCallLog(false);
}

TEST_CASE("script3 r2 attitude -3 (RW 0x66E145): the machine's goal object is cleared before the idle command's gate, so a locked temporary state "
		  "(the command refused) keeps no goal")
{
	combattest::CombatWorld w;
	startEngine(*w.logic);
	w.playerOf('A')->setPlayerType(PLAYER_COMPUTER, true);
	Object *a = w.unit("Swordsman", 'A', 300.0f, 300.0f);
	Object *dummy = w.unit("Dummy", 'B', 360.0f, 300.0f);
	AIUpdateInterface *ai = a->getAIUpdateInterface();
	REQUIRE(ai->aiAttackObject(dummy, CMD_FROM_PLAYER));
	REQUIRE(ai->stateMachine().goalObjectID() == dummy->getID());
	ai->stateMachine().setTemporaryStateRW(AI_WAIT, -1); // locked: every command is refused (RW 0x667174)
	REQUIRE(ai->stateMachine().temporaryStateLocked());
	ai->setAttitude(-3);
	CHECK(ai->stateMachine().goalObjectID() == INVALID_ID);
	CHECK(ai->currentVictim() == nullptr);
}

TEST_CASE("script3 r2 mood target (RW 0x668716 .. 0x668787): the vision range is checked before the passive / -3 branches; a passive unit that "
		  "sees nothing does not answer its last attacker")
{
	// a swordsman without vision (VisionRange 0), otherwise the fixture's
	std::string blind = combattest::kCombatObjects;
	const size_t at = blind.find("Object Swordsman\n");
	REQUIRE(at != std::string::npos);
	blind = blind.substr(at, blind.find("\nEnd\n", at) + 5 - at);
	blind.replace(0, std::string("Object Swordsman").size(), "Object BlindSword");
	const size_t vr = blind.find("VisionRange = 150");
	REQUIRE(vr != std::string::npos);
	blind.replace(vr, std::string("VisionRange = 150").size(), "VisionRange = 0");
	combattest::CombatWorld w(blind.c_str());
	startEngine(*w.logic);
	w.playerOf('A')->setPlayerType(PLAYER_COMPUTER, true);
	Object *dummy = w.unit("Dummy", 'B', 360.0f, 300.0f);
	for (const char *tmpl : { "BlindSword", "Swordsman" })
	{
		Object *a = w.unit(tmpl, 'A', 300.0f, 300.0f);
		AIUpdateInterface *ai = a->getAIUpdateInterface();
		ai->setAttitude(-1);
		DamageInfo info;
		info.m_input.m_damageType = DAMAGE_UNRESISTABLE;
		info.m_input.m_deathType = DEATH_NORMAL;
		info.m_input.m_sourceID = dummy->getID();
		info.m_input.m_amount = 1.0f;
		a->getBodyModule()->attemptDamage(info);
		if (std::string(tmpl) == "BlindSword")
		{
			CHECK(ai->nextMoodTarget() == nullptr); // no range: nothing, not the attacker
		}
		else
		{
			CHECK(ai->nextMoodTarget() == dummy); // with range: the last attacker
		}
	}
}

TEST_CASE("script3 r2 TEAM_ATTACK_NAMED (RW 0x7C4668): the player-command pass is decided by the TEAM parameter's name, not the unit's")
{
	CHECK(ScriptActions::teamAttackNamedAsPlayerFirst(act("TEAM_ATTACK_NAMED", { sp(3, "Aragorn 2"), sp(14, "Somebody") })));
	CHECK_FALSE(ScriptActions::teamAttackNamedAsPlayerFirst(act("TEAM_ATTACK_NAMED", { sp(3, "Some Team"), sp(14, "Aragorn 2") })));
	CHECK_FALSE(ScriptActions::teamAttackNamedAsPlayerFirst(act("TEAM_ATTACK_NAMED", { sp(3, "aragorn 2"), sp(14, "x") }))); // strcmp: case matters
}
