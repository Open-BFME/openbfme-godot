// OpenBFME unit tests: the map script engine, lane SCRIPT-3: HAS_FINISHED_AUDIO's logic-side timer, ALLOW_DISALLOW_ONE_BUILDING, the science availability,
// the attitude and its mood matrix, the hunt state, the group attack and the waypoint path states with their link choice. GPL-3.0.
//
// Expected values follow the RotWK functions cited at each check (ScriptEngine.cpp, ScriptActionsUnits.cpp, AIHunt.cpp, AIWaypointPath.cpp,
// AIUpdateCombat.cpp), never the engine's own output.

#include "GameLogic/AI/AIClosestEnemy.h"
#include "doctest.h"
#include "CombatTestUtil.h"
#include "LogicTestUtil.h"
#include "ScriptTestUtil.h"

#include "Common/BuildAssistant.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "GameClient/MapChunks.h"
#include "GameLogic/AI/AIFollowPathAsTeam.h"
#include "GameLogic/AI/AIGuard.h"
#include "GameLogic/AI/AIGroup.h"
#include "GameLogic/AI/AIGroupFormations.h"
#include "GameLogic/AI/AIPathfind.h"
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

#include <algorithm>
#include <cmath>
#include <functional>
#include <cstdint>
#include <cstdio>
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
	CHECK(ai->getNextMoodTarget(false, true) == nullptr); // passive, never hit
	// the dummy hits it: the last damage source is the answer
	DamageInfo info;
	info.m_input.m_damageType = DAMAGE_UNRESISTABLE;
	info.m_input.m_deathType = DEATH_NORMAL;
	info.m_input.m_sourceID = dummy->getID();
	info.m_input.m_amount = 1.0f;
	a->getBodyModule()->attemptDamage(info);
	CHECK(ai->getNextMoodTarget(false, true) == dummy);
	// -3: nothing, and a current victim is dropped (the unit idles)
	REQUIRE(ai->aiAttackObject(dummy, CMD_FROM_PLAYER));
	CHECK(ai->currentVictim() == dummy);
	run(w.logic->scriptEngine(), act("NAMED_SET_ATTITUDE", { sp(14, "A1"), ip(20, -3) }));
	CHECK(ai->attitude() == -3);
	CHECK(ai->currentVictim() == nullptr);
	CHECK(ai->currentStateId() == (unsigned)AI_IDLE);
	CHECK(ai->moodMatrixValue() == 0x2002u);
	// lane TARGET-1: the player's attack order with a sword (a MeleeWeapon) is remembered (AI + 0x1A4, RW 0x66BD33) and RW 0x66844A answers it before the mood
	// tests (RW 0x66854F); without it, -3 looks for nothing (target1 tests)
	CHECK(ai->getNextMoodTarget(false, true) == dummy);
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
	CHECK(ai->getNextMoodTarget(false, true) == dummy);
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
	// review of #73: RotWK's range is RW 0x6FF412(obj, 3), the vision range (RW 0x68E43B) PLUS Object + 0xB8, the bounding circle radius (GeometryInfo +
	// 0x10, never below 0.01: RW 0xAD2860), added at RW 0x6FF42D before the test at RW 0x66871E .. 0x668726 (FLDZ / FCOMIP: not above 0, nothing). A unit
	// with VisionRange 0 still has its radius and answers its attacker; only a range that the radius does not lift above 0 sees nothing: VisionRange -20
	// with the fixture's radius 8 (-12)
	auto variant = [](const char *name, const char *vision) {
		std::string t = combattest::kCombatObjects;
		const size_t at = t.find("Object Swordsman\n");
		REQUIRE(at != std::string::npos);
		t = t.substr(at, t.find("\nEnd\n", at) + 5 - at);
		t.replace(0, std::string("Object Swordsman").size(), std::string("Object ") + name);
		const size_t vr = t.find("VisionRange = 150");
		REQUIRE(vr != std::string::npos);
		t.replace(vr, std::string("VisionRange = 150").size(), std::string("VisionRange = ") + vision);
		return t;
	};
	const std::string extra = variant("BlindSword", "-20") + variant("ZeroSword", "0");
	combattest::CombatWorld w(extra.c_str());
	startEngine(*w.logic);
	w.playerOf('A')->setPlayerType(PLAYER_COMPUTER, true);
	Object *dummy = w.unit("Dummy", 'B', 360.0f, 300.0f);
	for (const char *tmpl : { "BlindSword", "ZeroSword", "Swordsman" })
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
		INFO(tmpl, ": range ", AIGuardMachine::getAdjustedVisionRange(*a));
		if (std::string(tmpl) == "BlindSword")
		{
			CHECK(ai->getNextMoodTarget(false, true) == nullptr); // no range: nothing, not the attacker
		}
		else
		{
			if (std::string(tmpl) == "ZeroSword")
			{
				// lane TARGET-1: RW 0x6FF412's range is the vision range PLUS the bounding circle (+ 0xB8, the cylinder's 8): VisionRange 0 still gives 8, above 0,
				// so the blind passive swordsman answers its attacker as well (a range of 0 needs no vision and no radius)
				CHECK(AI::adjustedVisionRange(*a, 3u) == 8.0f);
			}
			CHECK(ai->getNextMoodTarget(false, true) == dummy); // a range above 0 (ZeroSword: its radius alone): the last attacker, wherever it is
		}
	}
}

TEST_CASE("script3 r2 TEAM_ATTACK_NAMED (RW 0x7C4668): the player-command pass is decided by the TEAM parameter's name, not the unit's")
{
	CHECK(ScriptActions::teamAttackNamedAsPlayerFirst(act("TEAM_ATTACK_NAMED", { sp(3, "Aragorn 2"), sp(14, "Somebody") })));
	CHECK_FALSE(ScriptActions::teamAttackNamedAsPlayerFirst(act("TEAM_ATTACK_NAMED", { sp(3, "Some Team"), sp(14, "Aragorn 2") })));
	CHECK_FALSE(ScriptActions::teamAttackNamedAsPlayerFirst(act("TEAM_ATTACK_NAMED", { sp(3, "aragorn 2"), sp(14, "x") }))); // strcmp: case matters
}

TEST_CASE("decomp2 PLAYER_HUNT (RW 0x7BBCF9 -> Player::setUnitsShouldHunt RW 0x6AF4B1): the player's flag (+ 0x35D) is set and every unit of the player hunts; "
		  "with the flag the hunt does not end when nothing is left (RW 0x74820A)")
{
	combattest::CombatWorld w;
	startEngine(*w.logic);
	Object *h1 = w.unit("Swordsman", 'A', 150.0f, 150.0f);
	Object *h2 = w.unit("Swordsman", 'A', 170.0f, 150.0f);
	Object *far1 = w.unit("Dummy", 'B', 700.0f, 700.0f);
	w.frames(3);
	Player *alice = w.playerOf('A');
	REQUIRE(alice);
	CHECK_FALSE(alice->unitsShouldHunt());
	const std::uint32_t before = w.logic->computeStateHash();
	run(w.logic->scriptEngine(), act("PLAYER_HUNT", { sp(11, "Alice") }));
	CHECK(alice->unitsShouldHunt());
	CHECK_FALSE(w.playerOf('B')->unitsShouldHunt());
	CHECK(w.logic->computeStateHash() != before);
	CHECK(h1->getAIUpdateInterface()->currentStateId() == (unsigned)AI_HUNT);
	CHECK(h2->getAIUpdateInterface()->currentStateId() == (unsigned)AI_HUNT);
	const bool targetHunts = far1->getAIUpdateInterface() && far1->getAIUpdateInterface()->currentStateId() == (unsigned)AI_HUNT;
	CHECK_FALSE(targetHunts);
	const ObjectID id1 = far1->getID();
	const int ran = w.runUntil([&] { return !w.byId(id1); }, 3000);
	CHECK(ran < 3000);
	w.frames(60);
	// the flag keeps the hunt alive with nothing to hunt (the NAMED_HUNT case above ends in idle)
	CHECK(h1->getAIUpdateInterface()->currentStateId() == (unsigned)AI_HUNT);
	CHECK(w.logic->scriptEngine().stats().unportedActions.empty());
}

// ---- lane BACKLOG-8: AIGroup::createFormation (RW 0x94FCBA) ----

namespace
{
const char *const kFormationUnits =
	"Locomotor RiderLoco\n  Surfaces = GROUND\n  TurnTime = 500\n  TurnTimeDamaged = 500\n  Acceleration = 510\n  Braking = 510\n  MinSpeed = 0\n"
	"  ZAxisBehavior = NO_Z_MOTIVE_FORCE\n  Appearance = TWO_LEGS\n  StickToGround = Yes\n  CloseEnoughDist = 1\n  FormationPriority = CAVALRY1\nEnd\n"
	"Locomotor SwordLoco\n  Surfaces = GROUND\n  TurnTime = 500\n  TurnTimeDamaged = 500\n  Acceleration = 510\n  Braking = 510\n  MinSpeed = 0\n"
	"  ZAxisBehavior = NO_Z_MOTIVE_FORCE\n  Appearance = TWO_LEGS\n  StickToGround = Yes\n  CloseEnoughDist = 1\n  FormationPriority = MELEE1\nEnd\n"
	"Object Rider\n  KindOf = INFANTRY SELECTABLE SCORE\n  FormationWidth = 2\n  FormationDepth = 2\n  Geometry = CYLINDER\n  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n  GeometryHeight = 20\n  Body = ActiveBody ModuleTag_Body\n    MaxHealth = 100\n  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n  End\n  Behavior = PhysicsBehavior ModuleTag_Physics\n  End\n"
	"  LocomotorSet\n    Locomotor = RiderLoco\n    Condition = SET_NORMAL\n    Speed = 55\n  End\nEnd\n"
	"Object Sword\n  KindOf = INFANTRY SELECTABLE SCORE\n  Geometry = CYLINDER\n  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n  GeometryHeight = 20\n  Body = ActiveBody ModuleTag_Body\n    MaxHealth = 100\n  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n  End\n  Behavior = PhysicsBehavior ModuleTag_Physics\n  End\n"
	"  LocomotorSet\n    Locomotor = SwordLoco\n    Condition = SET_NORMAL\n    Speed = 55\n  End\nEnd\n";

std::string formationUnit(const char *name, const char *width, const char *depth)
{
	return std::string("Object ") + name + "\n  KindOf = INFANTRY SELECTABLE SCORE\n  FormationWidth = " + width + "\n  FormationDepth = " + depth +
		"\n  Geometry = CYLINDER\n  GeometryMajorRadius = 8\n  GeometryMinorRadius = 8\n  GeometryHeight = 20\n  Body = ActiveBody ModuleTag_Body\n"
		"    MaxHealth = 100\n  End\n  Behavior = AIUpdateInterface ModuleTag_AI\n  End\n  Behavior = PhysicsBehavior ModuleTag_Physics\n  End\n"
		"  LocomotorSet\n    Locomotor = SwordLoco\n    Condition = SET_NORMAL\n    Speed = 55\n  End\nEnd\n";
}

// the retail AIData rows (data\ini\default\aidata.ini): FormationColumnWidth 60, FormationRowDepth 55, FormationSquadSpacing 30, FormationColumns 2
void retailFormationRows(GameLogic &logic)
{
	logic.settings().formationColumnWidth = 60.0f;
	logic.settings().formationRowDepth = 55.0f;
	logic.settings().formationSquadSpacing = 30.0f;
	logic.settings().formationColumns = 2;
}
} // namespace

// The expected offsets come from an independent model of the BFME2 decomp source (float32 arithmetic), not from the port.
TEST_CASE("backlog8 formation: AIGroup::createFormation (RW 0x94FCBA) lays four units out in one row, every one on the slot nearest to it, the front "
		  "member at the origin")
{
	combattest::CombatWorld w(kFormationUnits);
	retailFormationRows(*w.logic);
	std::vector<Object *> units = { w.unit("Dummy", 'A', 100.0f, 290.0f), w.unit("Dummy", 'A', 100.0f, 230.0f), w.unit("Dummy", 'A', 100.0f, 170.0f),
		w.unit("Dummy", 'A', 100.0f, 110.0f) };
	for (Object *o : units)
	{
		CHECK(o->getAIUpdateInterface()->formationId() == 0);
	}
	AIGroupFormations::createFormation(*w.logic, AIGroupFormations::validMembers(units), Coord3D{ 0.0f, 200.0f, 0.0f }, Coord3D{ 10.0f, 200.0f, 0.0f });
	const float expectY[4] = { 60.0f, 0.0f, -60.0f, -120.0f };
	for (size_t i = 0; i < units.size(); ++i)
	{
		const AIUpdateInterface *ai = units[i]->getAIUpdateInterface();
		CHECK(ai->formationId() == 1); // the AI's first formation id (RW 0x70038D)
		CHECK(ai->formationOffset().x == 0.0f);
		CHECK(ai->formationOffset().y == expectY[i]);
	}
	// the next formation has the next id
	AIGroupFormations::createFormation(*w.logic, AIGroupFormations::validMembers(units), Coord3D{ 0.0f, 200.0f, 0.0f }, Coord3D{ 10.0f, 200.0f, 0.0f });
	CHECK(units[0]->getAIUpdateInterface()->formationId() == 2);
	int stops = 0;
	for (const std::string &l : w.logic->report().stops)
	{
		stops += l.rfind("[S-4460]", 0) == 0 ? 1 : 0;
	}
	CHECK(stops == 1);
	// FormationColumns 0 (retail divides by it): no formation, the units keep what they had, and the report names the row
	w.logic->settings().formationColumns = 0;
	AIGroupFormations::createFormation(*w.logic, AIGroupFormations::validMembers(units), Coord3D{ 0.0f, 200.0f, 0.0f }, Coord3D{ 10.0f, 200.0f, 0.0f });
	CHECK(units[0]->getAIUpdateInterface()->formationId() == 2);
	CHECK(units[0]->getAIUpdateInterface()->formationOffset().y == 60.0f);
	int named = 0;
	for (const std::string &l : w.logic->report().stops)
	{
		named += l.find("FormationColumns is 0") != std::string::npos ? 1 : 0;
	}
	CHECK(named == 1);
}

TEST_CASE("backlog8 formation: cavalry, melee and ranged units form ranks by their locomotor's FormationPriority, a FormationWidth 2 x FormationDepth 2 "
		  "unit takes two columns (RW 0x94FA6C / 0x94FB5F / 0x94F90B)")
{
	combattest::CombatWorld w(kFormationUnits);
	retailFormationRows(*w.logic);
	struct Row
	{
		const char *name;
		float x, y, offX, offY;
	};
	const Row rows[] = { { "Rider", 400.0f, 300.0f, -13.75f, -105.0f }, { "Rider", 380.0f, 360.0f, -13.75f, 15.0f }, { "Rider", 420.0f, 250.0f, -123.75f, -45.0f },
		{ "Sword", 300.0f, 280.0f, -206.25f, -135.0f }, { "Sword", 320.0f, 295.0f, -206.25f, 45.0f }, { "Sword", 340.0f, 310.0f, -261.25f, -45.0f },
		{ "Sword", 360.0f, 325.0f, -206.25f, -75.0f }, { "Sword", 380.0f, 340.0f, -206.25f, -15.0f }, { "Dummy", 250.0f, 320.0f, -316.25f, -75.0f },
		{ "Dummy", 260.0f, 380.0f, -316.25f, -15.0f } };
	std::vector<Object *> units;
	for (const Row &r : rows)
	{
		units.push_back(w.unit(r.name, 'A', r.x, r.y));
		REQUIRE(units.back()->getPosition()->x == r.x);
		REQUIRE(units.back()->getPosition()->y == r.y);
	}
	AIGroupFormations::createFormation(*w.logic, AIGroupFormations::validMembers(units), Coord3D{ 300.0f, 300.0f, 0.0f }, Coord3D{ 900.0f, 500.0f, 0.0f });
	for (size_t i = 0; i < units.size(); ++i)
	{
		CAPTURE(i);
		const AIUpdateInterface *ai = units[i]->getAIUpdateInterface();
		CHECK(ai->formationId() == 1);
		CHECK(ai->formationOffset().x == rows[i].offX);
		CHECK(ai->formationOffset().y == rows[i].offY);
	}
}

TEST_CASE("backlog8 formation: TEAM_FOLLOW_WAYPOINTS with the formation flag (RW 0x771E7D) marches the team in its formation: the members end the path "
		  "abreast, 60 apart; as a plain team they end on the one waypoint")
{
	struct End
	{
		std::vector<Coord3D> pos;
		std::vector<int> ids;
		std::vector<Coord2D> offsets;
		int ran = 0;
	};
	auto march = [](int formation) {
		End e;
		PathWorld p(1);
		retailFormationRows(*p.w.logic);
		std::vector<Object *> members = { p.w.unit("Dummy", 'A', 220.0f, 240.0f), p.w.unit("Dummy", 'A', 245.0f, 240.0f), p.w.unit("Dummy", 'A', 270.0f, 240.0f) };
		run(p.w.logic->scriptEngine(), act("TEAM_FOLLOW_WAYPOINTS", { sp(3, qual(p.w.teamA())), sp(24, "Path"), ip(8, 1), ip(8, formation) }));
		for (Object *o : members)
		{
			e.ids.push_back(o->getAIUpdateInterface()->formationId());
			e.offsets.push_back(o->getAIUpdateInterface()->formationOffset());
		}
		e.ran = p.w.runUntil(
			[&] {
				bool allIdle = true;
				for (Object *o : members)
				{
					allIdle = allIdle && o->getAIUpdateInterface()->currentStateId() == (unsigned)AI_IDLE;
				}
				return allIdle;
			},
			3000);
		for (Object *o : members)
		{
			e.pos.push_back(*o->getPosition());
		}
		return e;
	};
	auto gap = [](const Coord3D &a, const Coord3D &b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)); };
	const End team = march(0);
	const End formed = march(1);
	REQUIRE(team.ran < 3000);   // tolerance: the frame limit of runUntil: every member reached idle before it
	REQUIRE(formed.ran < 3000); // tolerance: as above
	// the offsets of the three (the model's): one row along y, the member at x 245 in the middle
	const float expectY[3] = { 60.0f, 0.0f, -60.0f };
	for (int i = 0; i < 3; ++i)
	{
		CHECK(team.ids[i] == 0);
		CHECK(formed.ids[i] == 1);
		CHECK(formed.offsets[i].x == 0.0f);
		CHECK(formed.offsets[i].y == expectY[i]);
	}
	std::printf("  info: formation march: team gaps %.2f %.2f, formation gaps %.2f %.2f; formed ends (%.1f, %.1f) (%.1f, %.1f) (%.1f, %.1f)\n", gap(team.pos[0], team.pos[1]),
		gap(team.pos[1], team.pos[2]), gap(formed.pos[0], formed.pos[1]), gap(formed.pos[1], formed.pos[2]), formed.pos[0].x, formed.pos[0].y, formed.pos[1].x,
		formed.pos[1].y, formed.pos[2].x, formed.pos[2].y);
	// the middle member ends on the last waypoint (600, 600), its neighbours one FormationColumnWidth (60) to each side of the path
	// tolerance: the members stop within their locomotor's CloseEnoughDist and the destination adjustment's cell (10) of their goals
	CHECK(gap(formed.pos[1], Coord3D{ 600.0f, 600.0f, 0.0f }) < 12.0f);
	// tolerance: as above, two members' stopping errors
	CHECK(std::fabs(gap(formed.pos[0], formed.pos[1]) - 60.0f) < 15.0f);
	// tolerance: as above
	CHECK(std::fabs(gap(formed.pos[1], formed.pos[2]) - 60.0f) < 15.0f);
	// tolerance: as above; the outer two are two columns apart
	CHECK(std::fabs(gap(formed.pos[0], formed.pos[2]) - 120.0f) < 15.0f);
	// the plain team's members all go to the waypoint itself: they end packed around it, far closer than a formation column
	// tolerance: the destination adjustment spreads them over neighbouring cells (10 each)
	CHECK(gap(team.pos[0], team.pos[2]) < 45.0f);
}

TEST_CASE("backlog8 formation: hostile FormationColumns / FormationWidth / FormationDepth values: just below the int32 limits a formation is made; at the "
		  "first value whose arithmetic overflows nothing changes and the report names the row (RW 0x94FDD5 / 0x94FE4C)")
{
	// the per-unit limit is INT32_MAX / 40 = 53687091 (a squad's row sums add up to 36 widths); the values are float-exact (the movement info reads them
	// through a float); 46340 x 46341 = 2147441940 fits alone, twice it does not
	const std::string extra = std::string(kFormationUnits) + formationUnit("WideOk", "53687088", "1") + formationUnit("WideBad", "53687092", "1") +
		formationUnit("DeepBad", "1", "-53687092") + formationUnit("Blocky", "46340", "46341");
	combattest::CombatWorld w(extra.c_str());
	retailFormationRows(*w.logic);
	auto named = [&](const std::string &what) {
		int n = 0;
		for (const std::string &l : w.logic->report().stops)
		{
			n += l.find(what) != std::string::npos ? 1 : 0;
		}
		return n;
	};
	std::vector<Object *> four = { w.unit("Dummy", 'A', 100.0f, 290.0f), w.unit("Dummy", 'A', 100.0f, 230.0f), w.unit("Dummy", 'A', 100.0f, 170.0f),
		w.unit("Dummy", 'A', 100.0f, 110.0f) };
	const Coord3D start{ 0.0f, 200.0f, 0.0f }, end{ 10.0f, 200.0f, 0.0f };
	auto refused = [&](const std::vector<Object *> &units, const std::string &what) {
		const std::uint32_t before = w.logic->computeStateHash();
		const int seen = named(what);
		AIGroupFormations::createFormation(*w.logic, AIGroupFormations::validMembers(units), start, end);
		CHECK(w.logic->computeStateHash() == before); // nothing changed: ids, offsets and the AI's formation counter
		CHECK(named(what) == seen + 1);
	};
	// FormationColumns: 178956970 * 12 = 2147483640 fits, 178956971 does not; 0 and negative divide by nothing useful
	w.logic->settings().formationColumns = 178956970;
	AIGroupFormations::createFormation(*w.logic, AIGroupFormations::validMembers(four), start, end);
	CHECK(four[0]->getAIUpdateInterface()->formationId() == 1);
	for (int columns : { 178956971, 0, -1, -2147483647 - 1 })
	{
		w.logic->settings().formationColumns = columns;
		refused(four, "FormationColumns is " + std::to_string(columns));
	}
	retailFormationRows(*w.logic);
	// FormationWidth / FormationDepth of one unit
	Object *wideOk = w.unit("WideOk", 'A', 300.0f, 300.0f);
	AIGroupFormations::createFormation(*w.logic, AIGroupFormations::validMembers({ wideOk }), start, end);
	CHECK(wideOk->getAIUpdateInterface()->formationId() == 2);
	refused({ four[0], w.unit("WideBad", 'A', 320.0f, 300.0f) }, "FormationWidth of WideBad (53687092 x 1)");
	refused({ four[0], w.unit("DeepBad", 'A', 340.0f, 300.0f) }, "FormationDepth of DeepBad (1 x -53687092)");
	// the area: one Blocky fits, two overflow the sum
	Object *b1 = w.unit("Blocky", 'A', 360.0f, 300.0f);
	AIGroupFormations::createFormation(*w.logic, AIGroupFormations::validMembers({ b1 }), start, end);
	CHECK(b1->getAIUpdateInterface()->formationId() == 3);
	refused({ b1, w.unit("Blocky", 'A', 380.0f, 300.0f) }, "the summed FormationWidth * FormationDepth of Blocky");
}

// ---- lane BACKLOG-8 r2: the formation march (RW 0x774897's head, 0x7747DE, 0x76F115, 0x6FAA6D, 0x77348C, state 54) ----

namespace
{
// the retail AIData / GameData rows of the march (data\ini\default\aidata.ini, gamedata.ini)
void retailMarchRows(GameLogic &logic)
{
	retailFormationRows(logic);
	logic.settings().minDistanceForGroup = 200.0f;
	logic.settings().formationEnemyDistance = 500.0f;
	logic.settings().narrowPassageScale = 0.4f;
	logic.settings().waitForOthers = false;
	logic.settings().useFormations = true;
	logic.settings().groupMoveClickToGatherAreaFactor = 0.5f;
}

struct MarchRun
{
	bool groundPath = false;
	std::vector<unsigned> states;          ///< every unit's AI state right after the order
	std::vector<std::vector<Coord3D>> track; ///< per frame, every unit's position
	std::vector<Coord3D> goals;            ///< every unit's last path point (the march) or its move goal
	std::vector<Coord2D> offsets;          ///< every unit's formation slot (the march)
	std::vector<std::vector<Coord3D>> paths; ///< every unit's march path (the machine's goal path)
	int frames = 0;
	bool stop = false;
	std::vector<unsigned> endStates;       ///< every unit's AI state when the run ended
};

// six units in two rows west of a block, ordered east past it; blockFirstSlot: a second block is built on the first unit's last path point after the
// order (its final slot becomes blocked while the unit still has the whole march ahead)
MarchRun march(bool formations, bool blockFirstSlot = false)
{
	MarchRun r;
	combattest::CombatWorld w(kFormationUnits);
	retailMarchRows(*w.logic);
	w.logic->settings().useFormations = formations;
	Object *block = w.unit("Block", 'A', 500.0f, 500.0f);
	w.ai->addObjectToPathfindMap(*block);
	std::vector<Object *> units;
	for (int i = 0; i < 6; ++i)
	{
		units.push_back(w.unit("Dummy", 'A', 180.0f + 40.0f * (float)(i / 3), 460.0f + 40.0f * (float)(i % 3)));
	}
	std::vector<ObjectID> ids;
	for (Object *o : units)
	{
		ids.push_back(o->getID());
	}
	AIGroup group(*w.logic, ids);
	group.groupMoveToPosition(Coord3D{ 850.0f, 500.0f, 0.0f }, false, CMD_FROM_PLAYER);
	r.groundPath = group.groundPath() != nullptr;
	for (Object *o : units)
	{
		AIUpdateInterface *ai = o->getAIUpdateInterface();
		r.states.push_back(ai->currentStateId());
		const std::vector<Coord3D> &path = ai->stateMachine().goalPath();
		r.goals.push_back(path.empty() ? ai->mover().goalPosition() : path.back());
		r.offsets.push_back(ai->formationOffset());
		r.paths.push_back(path);
	}
	if (blockFirstSlot)
	{
		Object *slotBlock = w.unit("Block", 'A', r.goals[0].x, r.goals[0].y);
		w.ai->addObjectToPathfindMap(*slotBlock);
	}
	r.frames = w.runUntil(
		[&] {
			std::vector<Coord3D> now;
			bool idle = true;
			for (Object *o : units)
			{
				now.push_back(*o->getPosition());
				idle = idle && o->getAIUpdateInterface()->currentStateId() == (unsigned)AI_IDLE;
			}
			r.track.push_back(now);
			return idle;
		},
		3000);
	for (Object *o : units)
	{
		r.endStates.push_back(o->getAIUpdateInterface()->currentStateId());
	}
	for (const std::string &l : w.logic->report().stops)
	{
		r.stop = r.stop || l.rfind("[S-4462]", 0) == 0;
	}
	return r;
}

float spreadAt(const std::vector<Coord3D> &p)
{
	float best = 0.0f;
	for (size_t i = 0; i < p.size(); ++i)
	{
		for (size_t j = i + 1; j < p.size(); ++j)
		{
			best = std::max(best, (float)std::hypot((double)(p[i].x - p[j].x), (double)(p[i].y - p[j].y)));
		}
	}
	return best;
}
} // namespace

TEST_CASE("backlog8 march: a player's group of six marches around a block in formation: each unit in state 54 walks its own lane (the ground path at "
		  "its slot, turned with every segment) through the turns and ends in its slot; without UseFormations the same order is the ZH group move")
{
	const MarchRun formed = march(true);
	const MarchRun plain = march(false);
	REQUIRE(formed.frames < 3000); // tolerance: the frame limit of runUntil
	REQUIRE(plain.frames < 3000);  // tolerance: as above
	CHECK(formed.groundPath);
	CHECK(formed.stop);
	CHECK_FALSE(plain.groundPath);
	CHECK_FALSE(plain.stop);
	for (size_t i = 0; i < formed.states.size(); ++i)
	{
		CAPTURE(i);
		CHECK(formed.states[i] == (unsigned)AI_FOLLOW_PATH_AS_TEAM);
		CHECK(plain.states[i] == (unsigned)AI_MOVE_TO);
	}
	// informative: across the group's direction of travel (its mean step of the frame), how often a pair of units two columns apart (slot y at least 30
	// apart) stands on the wrong side of each other; the march's flips come from the outer slot whose lane runs into the block's cells and is cut back
	// toward the ground path (RW 0x6EF387), the plain move's from the units choosing their own ways around the block
	auto sideFlips = [](const MarchRun &r, const std::vector<Coord2D> &slots) {
		int flips = 0, frames = 0;
		for (size_t f = 1; f < r.track.size(); ++f)
		{
			double mx = 0.0, my = 0.0;
			for (size_t i = 0; i < r.track[f].size(); ++i)
			{
				mx += r.track[f][i].x - r.track[f - 1][i].x;
				my += r.track[f][i].y - r.track[f - 1][i].y;
			}
			const double len = std::hypot(mx, my) / (double)r.track[f].size();
			if (len < 1.0)
			{
				continue; // the group stands (it starts or waits)
			}
			++frames;
			const double px = -my, py = mx; // the left of the travel direction (rotateOffset: y along (-dy, dx))
			for (size_t i = 0; i < slots.size(); ++i)
			{
				for (size_t j = 0; j < slots.size(); ++j)
				{
					if (slots[i].y - slots[j].y < 30.0f)
					{
						continue;
					}
					const double side = (r.track[f][i].x - r.track[f][j].x) * px + (r.track[f][i].y - r.track[f][j].y) * py;
					flips += side <= 0.0 ? 1 : 0;
				}
			}
		}
		return std::make_pair(flips, frames);
	};
	const auto formedSides = sideFlips(formed, formed.offsets);
	const auto plainSides = sideFlips(plain, formed.offsets); // the same pairs, judged by the slots the formation gave them
	const float formedEnd = spreadAt(formed.track.back()), plainEnd = spreadAt(plain.track.back());
	std::printf("  info: march frames %d / %d; side flips %d in %d moving frames / %d in %d; spread at the end %.1f / %.1f\n", formed.frames, plain.frames,
		formedSides.first, formedSides.second, plainSides.first, plainSides.second, formedEnd, plainEnd);
	// through the turn every marching unit walks its own lane: it passes each point of its path (the ground path's nodes at its slot, turned with each
	// segment, RW 0x77348C), in order
	for (size_t i = 0; i < formed.paths.size(); ++i)
	{
		CAPTURE(i);
		REQUIRE(formed.paths[i].size() == 4); // the point past the start, the two corner nodes of the ground path around the block, the end
		size_t from = 0;
		for (const Coord3D &pt : formed.paths[i])
		{
			double best = 1e30;
			size_t at = from;
			for (size_t f = from; f < formed.track.size(); ++f)
			{
				const double d = std::hypot((double)(formed.track[f][i].x - pt.x), (double)(formed.track[f][i].y - pt.y));
				if (d < best)
				{
					best = d;
					at = f;
				}
			}
			CHECK(best < 12.0); // tolerance: a unit steps up to its speed per frame (about 11) past a point it passes
			from = at;
		}
	}
	// every marching unit ends on its own slot: the last point of its path
	for (size_t i = 0; i < formed.goals.size(); ++i)
	{
		CAPTURE(i);
		const Coord3D &end = formed.track.back()[i];
		// tolerance: the locomotor stops within its CloseEnoughDist of the goal and the destination adjustment's cell (10)
		CHECK(std::hypot((double)(end.x - formed.goals[i].x), (double)(end.y - formed.goals[i].y)) < 12.0);
	}
	// tolerance: positions in the world; the formation spans its slots (three columns of 60 across), the ZH move keeps the units' own spacing (80 here)
	CHECK(formedEnd > 150.0f);
	CHECK(plainEnd < formedEnd); // tolerance: a comparison of the two runs
}

// RW 0x75040D-0x75042F: on the last leg the destination adjusts when AI + 0x54 and (AI + 0x55 or AI vslot 0x224 = canFollowTeamPath) (the port had it
// reversed: a ground marcher never adjusted). The expected end comes from the block's geometry (a 40-half-width box, the unit's radius 8), not from the port.
TEST_CASE("backlog8 march: a marcher's final slot blocked during the march: the last leg adjusts the destination (RW 0x75040D), so the unit ends on a clear "
		  "cell beside the block instead of at the slot")
{
	const MarchRun free = march(true);
	const MarchRun blocked = march(true, true);
	REQUIRE(free.frames < 3000);    // tolerance: the frame limit of runUntil
	REQUIRE(blocked.frames < 3000); // tolerance: as above
	REQUIRE(blocked.paths[0].size() == 4); // a multi-point march: the corner nodes, then the slot
	REQUIRE(blocked.goals[0].x == free.goals[0].x);
	REQUIRE(blocked.goals[0].y == free.goals[0].y);
	const Coord3D slot = blocked.goals[0];
	const Coord3D freeEnd = free.track.back()[0], end = blocked.track.back()[0];
	// the control: nothing in the way, the unit stands on its slot
	CHECK(std::hypot((double)(freeEnd.x - slot.x), (double)(freeEnd.y - slot.y)) < 12.0); // tolerance: the locomotor's stop distance and the adjustment's cell
	// the blocked slot: the unit stands outside the block's box grown by its own radius (40 + 8), and near the slot (the adjustment moves it by cells)
	const double dx = std::fabs((double)(end.x - slot.x)), dy = std::fabs((double)(end.y - slot.y));
	CAPTURE(dx);
	CAPTURE(dy);
	CHECK(std::max(dx, dy) >= 48.0 - 1.0); // tolerance: a cell of the pathfind grid is 10 and the unit's centre may touch the grown box
	CHECK(std::hypot(dx, dy) < 100.0);      // tolerance: the nearest clear cell for a unit of radius 8 is within a few cells of the box's edge
	CHECK(blocked.endStates[0] == (unsigned)AI_IDLE);
}

// The formation / march rows are hashed unconditionally (a desync check: two peers with other rows simulate other marches). The retail rows and the
// constructors' (RW 0x6FFD0E .. 0x6FFDE5) differ in every row but two and simulate differently, so they must not collide; neither does any single row.
TEST_CASE("backlog8 settings hash: the retail and the constructor tuples of the formation and march rows hash apart, and every single row moves the hash")
{
	GameLogicSettings construct;
	GameLogicSettings retail;
	retail.formationColumnWidth = 60.0f;
	retail.formationRowDepth = 55.0f;
	retail.formationSquadSpacing = 30.0f;
	retail.formationColumns = 2;
	retail.minDistanceForGroup = 200.0f;
	retail.formationEnemyDistance = 500.0f;
	retail.narrowPassageScale = 0.4f;
	retail.waitForOthers = false;
	retail.useFormations = true;
	retail.groupMoveClickToGatherAreaFactor = 0.5f;
	StateHasher constructHash, retailHash;
	construct.crc(constructHash);
	retail.crc(retailHash);
	CHECK(retailHash.value() != constructHash.value());
	const std::vector<std::pair<const char *, std::function<void(GameLogicSettings &)>>> edits = {
		{ "FormationColumnWidth", [](GameLogicSettings &s) { s.formationColumnWidth = 61.0f; } },
		{ "FormationRowDepth", [](GameLogicSettings &s) { s.formationRowDepth = 56.0f; } },
		{ "FormationSquadSpacing", [](GameLogicSettings &s) { s.formationSquadSpacing = 31.0f; } },
		{ "FormationColumns", [](GameLogicSettings &s) { s.formationColumns = 3; } },
		{ "MinDistanceForGroup", [](GameLogicSettings &s) { s.minDistanceForGroup = 201.0f; } },
		{ "FormationEnemyDistance", [](GameLogicSettings &s) { s.formationEnemyDistance = 501.0f; } },
		{ "NarrowPassageScale", [](GameLogicSettings &s) { s.narrowPassageScale = 0.5f; } },
		{ "WaitForOthers", [](GameLogicSettings &s) { s.waitForOthers = true; } },
		{ "UseFormations", [](GameLogicSettings &s) { s.useFormations = false; } },
		{ "GroupMoveClickToGatherAreaFactor", [](GameLogicSettings &s) { s.groupMoveClickToGatherAreaFactor = 0.6f; } },
	};
	for (const auto &e : edits)
	{
		for (const GameLogicSettings *base : { &retail, &construct })
		{
			GameLogicSettings s = *base;
			e.second(s);
			StateHasher h, hb;
			s.crc(h);
			base->crc(hb);
			CAPTURE(e.first);
			CHECK(h.value() != hb.value());
		}
	}
}
