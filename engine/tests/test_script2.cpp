// OpenBFME unit tests: the map script engine, lane SCRIPT-2 (the conditions and actions ported by retail use, the object trigger tracking, the panel
// flags and the map's indestructible / enabled properties). GPL-3.0.
//
// Expected values follow the RotWK functions cited at each check (ScriptActionsUnits.cpp, ScriptConditions.cpp, Object::updateTriggerAreaFlags), never the
// engine's own output.

#include "doctest.h"
#include "LogicTestUtil.h"
#include "ScriptTestUtil.h"
#include "MoveTestUtil.h"
#include "RetailTestMount.h"

#include "Common/MiniJson.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "GameClient/MapChunks.h"
#include "GameLogic/Damage.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/LogicModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ScriptEngine/ScriptActions.h"
#include "GameLogic/ScriptEngine/ScriptConditions.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"

#include <algorithm>
#include <set>

using namespace scripttest;

namespace
{

const char *const kUnitIni = "Object Unit\n"
							 "  KindOf = SELECTABLE INFANTRY\n"
							 "  Draw = W3DDefaultDraw ModuleTag_Draw\n  End\n"
							 "  Body = ActiveBody ModuleTag_Body\n    MaxHealth = 100\n    MaxHealthDamaged = 50\n    MaxHealthReallyDamaged = 10\n  End\n"
							 "End\n"
							 "Object Tower\n"
							 "  KindOf = SELECTABLE STRUCTURE\n"
							 "  Draw = W3DDefaultDraw ModuleTag_Draw\n  End\n"
							 "  Body = ActiveBody ModuleTag_Body\n    MaxHealth = 500\n    MaxHealthDamaged = 250\n    MaxHealthReallyDamaged = 50\n  End\n"
							 "End\n";

TriggerArea box(const std::string &name, float lo, float hi)
{
	TriggerArea t;
	t.name = name;
	t.points = { { lo, lo }, { hi, lo }, { hi, hi }, { lo, hi } };
	return t;
}

void moveTo(Object &o, float x, float y)
{
	const Coord3D p{ x, y, 0.0f };
	o.setPosition(&p);
}

bool evalCond(ScriptGame &g, const ScriptCondition &c)
{
	return ScriptConditions::evaluate(g.engine(), c);
}

void run(ScriptGame &g, const ScriptActionRec &a)
{
	ScriptActions::execute(g.engine(), a);
}

float damage(Object &o, float amount)
{
	const float before = o.getBodyModule()->getHealth();
	DamageInfo info;
	info.m_input.m_damageType = DAMAGE_UNRESISTABLE;
	info.m_input.m_deathType = DEATH_NORMAL;
	info.m_input.m_sourceID = o.getID();
	info.m_input.m_amount = amount;
	o.getBodyModule()->attemptDamage(info);
	return before - o.getBodyModule()->getHealth();
}

struct Fixture
{
	ScriptGame g;
	Object *u = nullptr;
	Fixture()
	{
		LogicModules::registerAll(g.lw.w.modules); // ActiveBody
		const std::string err = g.lw.w.load(kUnitIni, INI_LOAD_OVERWRITE, "units.ini");
		REQUIRE_MESSAGE(err.empty(), err);
		g.triggers.push_back(box("Box", 0.0f, 100.0f));
		g.triggers.push_back(box("Far", 1000.0f, 1100.0f));
		g.start();
		u = g.lw.make("Unit", g.lw.teamOf("Alice"));
		REQUIRE(u != nullptr);
		REQUIRE(u->getBodyModule() != nullptr);
		u->setName("U");
		moveTo(*u, 300.0f, 300.0f);
	}
};

} // namespace

TEST_CASE("script2 trigger tracking (RW 0x69264D): enter at the move's frame, the condition holds this frame and the next, the exit test uses the previous cell")
{
	Fixture f;
	Object &u = *f.u;
	const ScriptCondition entered = cond("NAMED_ENTERED_AREA", { sp(14, "U"), sp(9, "Box") });
	const ScriptCondition exited = cond("NAMED_EXITED_AREA", { sp(14, "U"), sp(9, "Box") });
	const ScriptCondition inside = cond("NAMED_INSIDE_AREA", { sp(14, "U"), sp(9, "Box") });
	const ScriptCondition outside = cond("NAMED_OUTSIDE_AREA", { sp(14, "U"), sp(9, "Box") });
	CHECK(u.triggerCount() == 0);
	CHECK_FALSE(evalCond(f.g, entered));
	CHECK(evalCond(f.g, outside));
	f.g.frame();
	const UnsignedInt enterFrame = f.g.lw.logic->getFrame();
	moveTo(u, 50.0f, 50.0f);
	REQUIRE(u.triggerCount() == 1);
	CHECK(u.triggerEntry(0).trigger == 0);
	CHECK(u.triggerEntry(0).entered);
	CHECK(u.triggerEntry(0).inside);
	CHECK_FALSE(u.triggerEntry(0).exited);
	CHECK(u.enteredOrExitedFrame() == enterFrame);
	CHECK(evalCond(f.g, entered));
	CHECK(evalCond(f.g, inside));
	CHECK_FALSE(evalCond(f.g, outside));
	f.g.frame(); // frame - 1 still counts (RW 0x68DD46)
	CHECK(evalCond(f.g, entered));
	f.g.frame();
	CHECK_FALSE(evalCond(f.g, entered));
	// the same integer cell: nothing happens
	moveTo(u, 50.5f, 50.5f);
	CHECK(u.enteredOrExitedFrame() == enterFrame);
	// out of the box: the exit loop tests the PREVIOUS cell (50, 50), still inside: no exit yet; the first move from a new frame compacts (entered cleared)
	moveTo(u, 150.0f, 150.0f);
	REQUIRE(u.triggerCount() == 1);
	CHECK_FALSE(u.triggerEntry(0).entered);
	CHECK(u.triggerEntry(0).inside);
	CHECK_FALSE(evalCond(f.g, exited));
	CHECK(evalCond(f.g, outside)); // NAMED_OUTSIDE_AREA reads the position (RW 0x7E72B2 = !RW 0x7E6ADC)
	// the next cell outside: (150, 150) is outside, the exit is reported now
	moveTo(u, 160.0f, 160.0f);
	REQUIRE(u.triggerCount() == 1);
	CHECK(u.triggerEntry(0).exited);
	CHECK_FALSE(u.triggerEntry(0).inside);
	CHECK(u.enteredOrExitedFrame() == f.g.lw.logic->getFrame());
	CHECK(evalCond(f.g, exited));
	// a later move compacts the list (RW 0x68B98E keeps the entries still inside)
	f.g.frame();
	f.g.frame();
	moveTo(u, 170.0f, 170.0f);
	CHECK(u.triggerCount() == 0);
	CHECK_FALSE(evalCond(f.g, exited));
	// the tracking state is hashed
	const std::uint32_t h0 = f.g.lw.logic->computeStateHash();
	moveTo(u, 60.0f, 60.0f);
	CHECK(h0 != f.g.lw.logic->computeStateHash());
}

TEST_CASE("script2 UNIT_AFFECT_OBJECT_PANEL_FLAGS (RW 0x7C569E): Indestructible blocks damage (RW 0x8C3FDA), Enabled / Powered disable, Unsellable / Player_Targetable bits")
{
	Fixture f;
	Object &u = *f.u;
	CHECK(damage(u, 10.0f) == doctest::Approx(10.0f));
	const std::uint32_t h0 = f.g.lw.logic->computeStateHash();
	run(f.g, act("UNIT_AFFECT_OBJECT_PANEL_FLAGS", { sp(14, "U"), sp(46, "Indestructible"), ip(8, 1) }));
	CHECK(u.getBodyModule()->isIndestructible());
	CHECK(h0 != f.g.lw.logic->computeStateHash());
	CHECK(damage(u, 10.0f) == 0.0f);
	u.kill(DEATH_NORMAL); // kill is UNRESISTABLE damage: blocked too
	CHECK_FALSE(u.isEffectivelyDead());
	run(f.g, act("UNIT_AFFECT_OBJECT_PANEL_FLAGS", { sp(14, "U"), sp(46, "Enabled"), ip(8, 0) }));
	CHECK((u.scriptStatus() & 1u) != 0);
	CHECK((u.getDisabledMask() & (1u << 9)) != 0); // DISABLED_SCRIPT_DISABLED
	run(f.g, act("UNIT_AFFECT_OBJECT_PANEL_FLAGS", { sp(14, "U"), sp(46, "Enabled"), ip(8, 1) }));
	CHECK((u.scriptStatus() & 1u) == 0);
	CHECK((u.getDisabledMask() & (1u << 9)) == 0);
	run(f.g, act("UNIT_AFFECT_OBJECT_PANEL_FLAGS", { sp(14, "U"), sp(46, "Powered"), ip(8, 0) }));
	CHECK((u.getDisabledMask() & (1u << 10)) != 0);
	run(f.g, act("UNIT_AFFECT_OBJECT_PANEL_FLAGS", { sp(14, "U"), sp(46, "Unsellable"), ip(8, 1) }));
	run(f.g, act("UNIT_AFFECT_OBJECT_PANEL_FLAGS", { sp(14, "U"), sp(46, "Player_Targetable"), ip(8, 1) }));
	CHECK(u.scriptStatus() == (2u | 4u | 0x10u));
	run(f.g, act("UNIT_AFFECT_OBJECT_PANEL_FLAGS", { sp(14, "U"), sp(46, "Selectable"), ip(8, 0) }));
	CHECK_FALSE(u.isScriptSelectable());
	run(f.g, act("UNIT_AFFECT_OBJECT_PANEL_FLAGS", { sp(14, "U"), sp(46, "NoSuchFlag"), ip(8, 1) })); // nothing
	CHECK(u.scriptStatus() == (2u | 4u | 0x10u));
	// the team form: every member
	Object *v = f.g.lw.make("Unit", f.g.lw.teamOf("Alice"));
	run(f.g, act("TEAM_AFFECT_OBJECT_PANEL_FLAGS", { sp(3, "Alice/teamAlice"), sp(46, "Indestructible"), ip(8, 0) }));
	CHECK_FALSE(u.getBodyModule()->isIndestructible());
	CHECK_FALSE(v->getBodyModule()->isIndestructible());
	CHECK(damage(u, 10.0f) == doctest::Approx(10.0f));
}

TEST_CASE("script2 team relationship overrides (RW 0x7C1416 / 0x7C4E89 / 0x7C14C1), COUNTER_MATH (RW 0x7C33E5 / 0x7C3465), TEAM_CHANGE_OBJECT_STATUS, ownership")
{
	Fixture f;
	Team *a = f.g.lw.teamOf("Alice");
	Team *b = f.g.lw.teamOf("Bob");
	const Relationship before = a->getRelationship(b);
	run(f.g, act("TEAM_SET_OVERRIDE_RELATION_TO_TEAM", { sp(3, "Alice/teamAlice"), sp(3, "Bob/teamBob"), ip(19, NEUTRAL) }));
	CHECK(a->getRelationship(b) == NEUTRAL);
	CHECK(a->hasRelationshipOverrides());
	run(f.g, act("TEAM_SET_OVERRIDE_RELATION_TO_PLAYER", { sp(3, "Alice/teamAlice"), sp(11, "Bob"), ip(19, ALLIES) }));
	run(f.g, act("TEAM_REMOVE_ALL_OVERRIDE_RELATIONS", { sp(3, "Alice/teamAlice") }));
	CHECK_FALSE(a->hasRelationshipOverrides());
	CHECK(a->getRelationship(b) == before);

	// COUNTER_MATH: 0 + 1 - 2 * 3 /; the counter is made, the other counter found (0 when absent)
	run(f.g, act("COUNTER_MATH_VALUE", { sp(4, "c"), ip(57, 0), ip(0, 7) }));
	run(f.g, act("COUNTER_MATH_VALUE", { sp(4, "c"), ip(57, 2), ip(0, 3) }));
	run(f.g, act("COUNTER_MATH_VALUE", { sp(4, "c"), ip(57, 1), ip(0, 1) }));
	run(f.g, act("COUNTER_MATH_VALUE", { sp(4, "c"), ip(57, 3), ip(0, 4) }));
	CHECK(f.g.engine().counter("c").value == 5); // ((7 * 3) - 1) / 4
	run(f.g, act("COUNTER_MATH_COUNTER", { sp(4, "c"), ip(57, 2), sp(4, "missing") }));
	CHECK(f.g.engine().counter("c").value == 0);
	CHECK(f.g.engine().findCounter("missing") == nullptr);

	// TEAM_CHANGE_OBJECT_STATUS: the status index (the parser turned the name into it, RW 0x7B66F5)
	run(f.g, act("TEAM_CHANGE_OBJECT_STATUS", { sp(3, "Alice/teamAlice"), ip(41, 104), ip(8, 1) }));
	CHECK(f.u->testStatus(104));
	run(f.g, act("TEAM_CHANGE_OBJECT_STATUS", { sp(3, "Alice/teamAlice"), ip(41, 104), ip(8, 0) }));
	CHECK_FALSE(f.u->testStatus(104));

	// NAMED_TRANSFER_OWNERSHIP_PLAYER: the first player's default team (RW 0x7BC11C)
	run(f.g, act("NAMED_TRANSFER_OWNERSHIP_PLAYER", { sp(14, "U"), sp(11, "Bob") }));
	CHECK(f.u->getTeam() == b);
	// TEAM_MERGE_INTO_TEAM (RW 0x7C0C75): every member joins the second team
	Object *w = f.g.lw.make("Unit", a);
	run(f.g, act("TEAM_MERGE_INTO_TEAM", { sp(3, "Alice/teamAlice"), sp(3, "Bob/teamBob") }));
	CHECK(w->getTeam() == b);
	CHECK(a->getFirstMember() == nullptr);
}

TEST_CASE("script2 area and count conditions: TEAM_INSIDE_AREA_PARTIALLY (RW 0x7E6A5A), PLAYER_HAS_COMPARISON_UNIT_TYPE_IN_TRIGGER_AREA[_COMPLETELY_BUILT] (RW 0x7E6D81), "
		  "PLAYER_HAS_OBJECT_COMPARISON (RW 0x7EAA59), NAMED_DISCOVERED without shroud (RW 0x68D8F7 answers clear)")
{
	Fixture f;
	Object &u = *f.u;
	Object *t = f.g.lw.make("Tower", f.g.lw.teamOf("Alice"));
	moveTo(*t, 500.0f, 500.0f);
	const ScriptCondition partial = cond("TEAM_INSIDE_AREA_PARTIALLY", { sp(3, "Alice/teamAlice"), sp(9, "Box"), ip(37, 0) });
	CHECK_FALSE(evalCond(f.g, partial));
	moveTo(u, 10.0f, 10.0f);
	CHECK(evalCond(f.g, partial)); // one counted member inside, one outside
	moveTo(*t, 20.0f, 20.0f);
	CHECK(evalCond(f.g, partial)); // every counted member inside

	const ScriptCondition inArea = cond("PLAYER_HAS_COMPARISON_UNIT_TYPE_IN_TRIGGER_AREA", { sp(11, "Alice"), ip(6, 2), ip(0, 1), sp(61, "Tower"), sp(9, "Box") });
	CHECK(evalCond(f.g, inArea)); // == 1
	const ScriptCondition built = cond("PLAYER_HAS_COMPARISON_UNIT_TYPE_IN_TRIGGER_AREA_COMPLETELY_BUILT", { sp(11, "Alice"), ip(6, 2), ip(0, 1), sp(61, "Tower"), sp(9, "Box") });
	t->setConstructionPercent(50.0f);
	CHECK_FALSE(evalCond(f.g, built));
	t->setConstructionPercent(-1.0f); // CONSTRUCTION_COMPLETE
	CHECK(evalCond(f.g, built));
	const ScriptCondition wrongPlayer = cond("PLAYER_HAS_COMPARISON_UNIT_TYPE_IN_TRIGGER_AREA", { sp(11, "Bob"), ip(6, 2), ip(0, 1), sp(61, "Tower"), sp(9, "Box") });
	CHECK_FALSE(evalCond(f.g, wrongPlayer));

	// PLAYER_HAS_OBJECT_COMPARISON: alive, not under construction; an object list names several types
	const ScriptCondition count2 = cond("PLAYER_HAS_OBJECT_COMPARISON", { sp(11, "Alice"), ip(6, 2), ip(0, 2), sp(61, "Both") });
	run(f.g, act("OBJECTLIST_ADDOBJECTTYPE", { sp(60, "Both"), sp(15, "Unit") }));
	run(f.g, act("OBJECTLIST_ADDOBJECTTYPE", { sp(60, "Both"), sp(15, "Tower") }));
	CHECK(evalCond(f.g, count2));
	t->setStatus(OBJECT_STATUS_UNDER_CONSTRUCTION, true);
	CHECK_FALSE(evalCond(f.g, count2));
	t->setStatus(OBJECT_STATUS_UNDER_CONSTRUCTION, false);
	CHECK(evalCond(f.g, cond("PLAYER_HAS_OBJECT_COMPARISON", { sp(11, "Alice"), ip(6, 3), ip(0, 1), sp(61, "Unit") })));
	CHECK_FALSE(evalCond(f.g, cond("PLAYER_HAS_OBJECT_COMPARISON", { sp(11, "Alice"), ip(6, 0), ip(0, 1), sp(61, "NoSuchType") })));

	// NAMED_DISCOVERED: no shroud manager in the fixture: the object status is clear (RW 0x68D8F7 returns 1 without a record)
	CHECK(evalCond(f.g, cond("NAMED_DISCOVERED", { sp(14, "U"), sp(11, "Bob") })));
	CHECK_FALSE(evalCond(f.g, cond("NAMED_DISCOVERED", { sp(14, "Nobody"), sp(11, "Bob") })));
}

TEST_CASE("script2 IDLE_ALL_UNITS, NAMED_STOP_FLUSH, the attack and path actions without an AI module do nothing; the follow-path actions report S-1186")
{
	Fixture f;
	run(f.g, act("NAMED_STOP_FLUSH", { sp(14, "U") }));
	run(f.g, act("IDLE_ALL_UNITS", {}));
	run(f.g, act("NAMED_ATTACK_NAMED", { sp(14, "U"), sp(14, "U") }));
	run(f.g, act("NAMED_FOLLOW_WAYPOINTS", { sp(14, "U"), sp(24, "Path") }));
	CHECK(f.g.engine().stats().unportedActions.empty());
}

TEST_CASE("script2 team states (RW 0x7BF72E / 0x7EA28F, RW 0x7A6BD3 / 0x7A6A01), UNIT_HEALTH (RW 0x7E6164), object status conditions, SKIRMISH_PLAYER_FACTION, "
		  "TEAM_INSIDE_AREA_ENTIRELY, SET_PLAYER_OWNERSHIP_OF_TYPE_COUNTER")
{
	Fixture f;
	Object &u = *f.u;
	Team *a = f.g.lw.teamOf("Alice");
	const std::uint32_t h0 = f.g.lw.logic->computeStateHash();
	run(f.g, act("TEAM_SET_STATE", { sp(3, "Alice/teamAlice"), sp(18, "Attacking") }));
	CHECK(a->getState() == "Attacking");
	CHECK(h0 != f.g.lw.logic->computeStateHash());
	CHECK(evalCond(f.g, cond("TEAM_STATE_IS", { sp(3, "Alice/teamAlice"), sp(18, "Attacking") })));
	CHECK_FALSE(evalCond(f.g, cond("TEAM_STATE_IS_NOT", { sp(3, "Alice/teamAlice"), sp(18, "Attacking") })));
	CHECK(evalCond(f.g, cond("TEAM_STATE_IS_NOT", { sp(3, "Alice/teamAlice"), sp(18, "Idle") })));
	run(f.g, act("TEAM_SET_CUSTOM_STATE", { sp(3, "Alice/teamAlice"), sp(18, "Ready"), ip(8, 1) }));
	CHECK(evalCond(f.g, cond("TEAM_HAS_CUSTOM_STATE", { sp(3, "Alice/teamAlice"), sp(18, "Ready") })));
	run(f.g, act("TEAM_SET_CUSTOM_STATE", { sp(3, "Alice/teamAlice"), sp(18, "Ready"), ip(8, 0) }));
	CHECK_FALSE(evalCond(f.g, cond("TEAM_HAS_CUSTOM_STATE", { sp(3, "Alice/teamAlice"), sp(18, "Ready") })));

	// UNIT_HEALTH: (int)(health * 100 / max), SSE single
	damage(u, 33.0f);
	CHECK(evalCond(f.g, cond("UNIT_HEALTH", { sp(14, "U"), ip(6, 2), ip(0, 67) })));
	CHECK(evalCond(f.g, cond("UNIT_HEALTH", { sp(14, "U"), ip(6, 0), ip(0, 68) })));

	u.setStatus(104, true);
	CHECK(evalCond(f.g, cond("UNIT_HAS_OBJECT_STATUS", { sp(14, "U"), ip(41, 104) })));
	Object *v = f.g.lw.make("Unit", a);
	CHECK(evalCond(f.g, cond("TEAM_SOME_HAVE_OBJECT_STATUS", { sp(3, "Alice/teamAlice"), ip(41, 104) })));
	CHECK_FALSE(evalCond(f.g, cond("TEAM_ALL_HAS_OBJECT_STATUS", { sp(3, "Alice/teamAlice"), ip(41, 104) })));
	v->setStatus(104, true);
	CHECK(evalCond(f.g, cond("TEAM_ALL_HAS_OBJECT_STATUS", { sp(3, "Alice/teamAlice"), ip(41, 104) })));

	const std::string side = f.g.lw.players.findPlayerWithName("Alice")->getSide();
	CHECK(evalCond(f.g, cond("SKIRMISH_PLAYER_FACTION", { sp(11, "Alice"), sp(47, side) })));
	CHECK_FALSE(evalCond(f.g, cond("SKIRMISH_PLAYER_FACTION", { sp(11, "Alice"), sp(47, side + "x") })));

	const ScriptCondition entirely = cond("TEAM_INSIDE_AREA_ENTIRELY", { sp(3, "Alice/teamAlice"), sp(9, "Box"), ip(37, 0) });
	moveTo(u, 10.0f, 10.0f);
	moveTo(*v, 400.0f, 400.0f);
	CHECK_FALSE(evalCond(f.g, entirely));
	moveTo(*v, 20.0f, 20.0f);
	CHECK(evalCond(f.g, entirely));

	run(f.g, act("SET_PLAYER_OWNERSHIP_OF_TYPE_COUNTER", { sp(61, "Unit"), sp(11, "Alice"), sp(4, "owned") }));
	CHECK(f.g.engine().counter("owned").value == 2);
}

TEST_CASE("script2 sequential scripts (RW 0x609C3A / 0x60C441 / 0x606FDA): a team's actions one after another while it is idle, loops, the stop actions")
{
	Fixture f;
	// a sequential script on Alice's team: two increments; looping twice more (3 runs); the members have no AI (idle), so every action runs this frame
	auto seq = script("Seq", { { cond("CONDITION_TRUE", {}) } }, { act("INCREMENT_COUNTER", { ip(0, 1), sp(4, "c") }), act("INCREMENT_COUNTER", { ip(0, 1), sp(4, "c") }) },
		{}, true);
	seq->actionsFireSequentially = true;
	seq->sequentialTargetType = 0;
	seq->sequentialTargetName = "teamAlice";
	seq->loopCount = 2;
	f.g.lists[1].items.push_back(item(std::move(seq)));
	f.g.start();
	f.g.frame();
	CHECK(f.g.counter("Alice/c") == 6);
	CHECK(f.g.engine().sequentialScripts().empty());
	CHECK_FALSE(f.g.engine().scriptActive("Alice/Seq"));

	// a missing target: the script goes inactive and nothing is queued
	ScriptGame g2;
	auto miss = script("Miss", { { cond("CONDITION_TRUE", {}) } }, { act("INCREMENT_COUNTER", { ip(0, 1), sp(4, "c") }) });
	miss->actionsFireSequentially = true;
	miss->sequentialTargetType = 1;
	miss->sequentialTargetName = "Nobody";
	g2.lists[1].items.push_back(item(std::move(miss)));
	g2.start();
	g2.frame();
	CHECK_FALSE(g2.engine().scriptActive("Alice/Miss"));
	CHECK(g2.counter("Alice/c") == -999);

	// TEAM_EXECUTE_SEQUENTIAL_SCRIPT queues a (subroutine) script by name; TEAM_STOP_SEQUENTIAL_SCRIPT empties the chain before it runs
	ScriptGame g3;
	auto sub = script("Sub", {}, { act("INCREMENT_COUNTER", { ip(0, 5), sp(4, "d") }) });
	sub->isSubroutine = true;
	g3.lists[1].items.push_back(item(std::move(sub)));
	g3.lists[1].items.push_back(item(script("Go", { { cond("CONDITION_TRUE", {}) } }, { act("TEAM_EXECUTE_SEQUENTIAL_SCRIPT", { sp(3, "Alice/teamAlice"), sp(2, "Sub") }) }, {}, true)));
	g3.start();
	const std::uint32_t h0 = g3.lw.logic->computeStateHash();
	g3.frame();
	CHECK(g3.counter("Alice/d") == 5);
	CHECK(h0 != g3.lw.logic->computeStateHash());
	ScriptEngine::SequentialScript r;
	r.team = g3.lw.teamOf("Alice")->getID();
	r.script = g3.engine().findScript("Alice/Sub")->def;
	r.scriptName = "Sub";
	r.side = "Alice";
	g3.engine().appendSequentialScript(r);
	REQUIRE(g3.engine().sequentialScripts().size() == 1);
	ScriptActions::execute(g3.engine(), act("TEAM_STOP_SEQUENTIAL_SCRIPT", { sp(3, "Alice/teamAlice") }));
	CHECK(g3.engine().sequentialScripts()[0].empty());
	g3.frame();
	CHECK(g3.counter("Alice/d") == 5);
	CHECK(g3.engine().sequentialScripts().empty());
}

namespace
{

struct Coverage
{
	std::int64_t total = 0, uncovered = 0;
	std::vector<std::pair<std::int64_t, std::string>> missing; // (uses, name), most used first
};

// every template of `kind` with a retail case is run once in a game (empty parameters of the template's types; a War of the Ring only template in a
// War of the Ring game); the ones the engine counts as unported (S-1180) are weighed by the census' retail uses
Coverage measure(const JsonValue &census, bool conditions)
{
	Coverage out;
	std::set<std::string> unported;
	for (int warOfTheRing = 0; warOfTheRing < 2; ++warOfTheRing)
	{
		Fixture f;
		std::vector<ScriptEngine::SideScripts> sides(3);
		const char *names[] = { "", "Alice", "Bob" };
		for (size_t i = 0; i < 3; ++i)
		{
			sides[i].sideName = names[i];
			sides[i].lists.push_back(&f.g.lists[i]);
		}
		f.g.engine().newGame(sides, &f.g.triggers, nullptr, warOfTheRing != 0);
		const std::vector<ScriptTemplate> &all = conditions ? ScriptTemplates::conditions() : ScriptTemplates::actions();
		for (const ScriptTemplate &t : all)
		{
			if (!t.retailCase || t.name.empty() || ((t.modeMask & 1) != 0) == (warOfTheRing != 0))
			{
				continue;
			}
			std::vector<ScriptParameter> params;
			for (int type : t.parameterTypes)
			{
				ScriptParameter p;
				p.type = type;
				params.push_back(p);
			}
			if (conditions)
			{
				ScriptCondition c = cond(t.name.c_str(), params);
				f.g.engine().evaluateCondition(c);
			}
			else
			{
				f.g.engine().executeAction(act(t.name.c_str(), params));
			}
		}
		const auto &counted = conditions ? f.g.engine().stats().unportedConditions : f.g.engine().stats().unportedActions;
		for (const auto &kv : counted)
		{
			unported.insert(kv.first);
		}
	}
	for (const char *key : conditions ? std::vector<const char *>{ "condition" } : std::vector<const char *>{ "action", "falseAction" })
	{
		const JsonValue *uses = census.get(key);
		REQUIRE(uses != nullptr);
		for (const auto &kv : uses->object)
		{
			const std::int64_t n = (std::int64_t)kv.second.get("uses")->number;
			out.total += n;
			if (unported.count(kv.first))
			{
				out.uncovered += n;
				out.missing.emplace_back(n, kv.first);
			}
		}
	}
	std::sort(out.missing.begin(), out.missing.end(), [](const auto &a, const auto &b) { return a.first != b.first ? a.first > b.first : a.second < b.second; });
	return out;
}

} // namespace

TEST_CASE("script2 coverage: the share of the retail script uses (skirmish, campaign and War of the Ring maps, the census) that runs a port")
{
	std::vector<unsigned char> bytes;
	std::string err;
	REQUIRE_MESSAGE(retailtest::readLocalFile(retailtest::dataDir() + "/script-census.json", bytes, &err), err);
	JsonValue census;
	REQUIRE_MESSAGE(JsonValue::parse(std::string(bytes.begin(), bytes.end()), census, &err), err);
	const Coverage a = measure(census, false);
	const Coverage c = measure(census, true);
	auto top = [](const Coverage &v) {
		std::string s;
		for (size_t i = 0; i < v.missing.size() && i < 25; ++i)
		{
			s += v.missing[i].second + " " + std::to_string(v.missing[i].first) + ", ";
		}
		return s;
	};
	MESSAGE("actions: " << a.uncovered << " of " << a.total << " uses unported (" << (100.0 * (double)a.uncovered / (double)a.total) << " %): " << top(a));
	MESSAGE("conditions: " << c.uncovered << " of " << c.total << " uses unported (" << (100.0 * (double)c.uncovered / (double)c.total) << " %): " << top(c));
	CHECK(a.total == 28593 + 422); // the census' action and false-action uses (engine/tests/data/script-census.json)
	CHECK(c.total == 14493);
	// the pins only shrink: SCRIPT-1 left 9,985 action uses (34 %) and 3,113 condition uses (21 %) unported, SCRIPT-2 4,788 and 1,448; lane SCRIPT-3
	// (hunt, attitude, the building / science availability, the waypoint paths, HAS_FINISHED_AUDIO, CAN_BUILD_AT_BASE, the script volumes)
	CHECK(a.uncovered <= 3008);
	CHECK(c.uncovered <= 981); // lane AUDIO-4 (merged): PLAYER_ACQUIRED_SCIENCE and PLAYER_HAS_REACHED_LEVEL_CAP ported (21 uses): 1002 -> 981
}

namespace
{

// a sequential script of `target` (a unit when `unit`) with `actions`
std::unique_ptr<Script> sequential(const std::string &name, const std::string &target, bool unit, std::vector<ScriptActionRec> actions)
{
	auto s = script(name, { { cond("CONDITION_TRUE", {}) } }, std::move(actions), {}, true);
	s->actionsFireSequentially = true;
	s->sequentialTargetType = unit ? 1 : 0;
	s->sequentialTargetName = target;
	return s;
}

struct UnitQueues
{
	movetest::MoveWorld w;
	ScriptList list;
	std::int32_t counter(const std::string &q) const
	{
		const ScriptEngine::Counter *c = w.logic->scriptEngine().findCounter(q);
		return c ? c->value : -999;
	}
	void start()
	{
		ScriptEngine::SideScripts side;
		side.sideName = ""; // side 0 is ThePlayerList's player 0, the neutral player: its counters are "/name"
		side.lists.push_back(&list);
		w.logic->scriptEngine().newGame({ side }, nullptr, nullptr, false);
	}
	Object *unit(const char *name, float x)
	{
		Object *o = w.spawn("Walker", x, 500.0f);
		REQUIRE(o != nullptr);
		REQUIRE(o->getAIUpdateInterface() != nullptr);
		o->setName(name);
		return o;
	}
	bool allEmpty() const
	{
		for (const auto &chain : w.logic->scriptEngine().sequentialScripts())
		{
			if (!chain.empty())
			{
				return false;
			}
		}
		return true;
	}
};

} // namespace

TEST_CASE("script2 sequential cancellation: a unit's script stops its own queue (RW 0x604C9F inside RW 0x60C441): no read of the freed record, nothing after it runs")
{
	UnitQueues q;
	q.w.buildMap();
	q.unit("Runner", 500.0f);
	q.list.items.push_back(item(sequential("SelfUnit", "Runner", true,
		{ act("UNIT_STOP_SEQUENTIAL_SCRIPT", { sp(14, "Runner") }), act("INCREMENT_COUNTER", { ip(0, 1), sp(4, "after") }) })));
	q.start();
	q.w.logic->runLogicFrame();
	CHECK(q.counter("/after") == -999);
	CHECK(q.w.logic->scriptEngine().sequentialScripts().empty()); // the emptied slot was erased when the update came back to it
}

TEST_CASE("script2 sequential cancellation: a team's script stops its own queue (RW 0x604CD9): the emptied slot is not read, nothing after it runs")
{
	Fixture f;
	f.g.lists[1].items.push_back(item(sequential("SelfTeam", "teamAlice", false,
		{ act("TEAM_STOP_SEQUENTIAL_SCRIPT", { sp(3, "Alice/teamAlice") }), act("INCREMENT_COUNTER", { ip(0, 1), sp(4, "after") }) })));
	f.g.start();
	f.g.frame();
	CHECK(f.g.counter("Alice/after") == -999);
	CHECK(f.g.engine().sequentialScripts().empty());
}

TEST_CASE("script2 sequential cancellation: a script stops an EARLIER and a LATER queue in the same update; the updater keeps its position")
{
	UnitQueues q;
	q.w.buildMap();
	Object *first = q.unit("First", 400.0f);
	q.unit("Second", 500.0f);
	q.unit("Third", 600.0f);
	// slot 0: First's queue waits (First is walking), slot 1: Second's stops slot 0 and slot 2, then goes on; slot 2: Third's never runs
	q.list.items.push_back(item(sequential("FirstScript", "First", true, { act("INCREMENT_COUNTER", { ip(0, 1), sp(4, "first") }) })));
	q.list.items.push_back(item(sequential("SecondScript", "Second", true,
		{ act("UNIT_STOP_SEQUENTIAL_SCRIPT", { sp(14, "First") }), act("UNIT_STOP_SEQUENTIAL_SCRIPT", { sp(14, "Third") }),
			act("INCREMENT_COUNTER", { ip(0, 1), sp(4, "after") }), act("INCREMENT_COUNTER", { ip(0, 1), sp(4, "after") }) })));
	q.list.items.push_back(item(sequential("ThirdScript", "Third", true, { act("INCREMENT_COUNTER", { ip(0, 1), sp(4, "third") }) })));
	q.start();
	first->getAIUpdateInterface()->aiMoveToPosition(Coord3D{ 1500.0f, 1500.0f, 0.0f }, CMD_FROM_SCRIPT);
	q.w.logic->runLogicFrame();
	CHECK(q.counter("/first") == -999);
	CHECK(q.counter("/third") == -999);
	CHECK(q.counter("/after") == 2); // Second's queue ran on from where it was: both increments once
	CHECK(q.allEmpty());                  // the earlier slot is emptied (erased at its next visit), the later one erased in this update
	q.w.logic->runLogicFrame();
	CHECK(q.w.logic->scriptEngine().sequentialScripts().empty());
	CHECK(q.counter("/after") == 2);
}

TEST_CASE("script2 sequential scripts: at most 21 actions per queue and frame (RW 0x60C441's 0x15 limit), the random counters' draws unchanged")
{
	Fixture a, b;
	for (Fixture *f : { &a, &b })
	{
		std::vector<ScriptActionRec> actions;
		for (int i = 0; i < 42; ++i)
		{
			actions.push_back(act("SET_RANDOM_COUNTER", { sp(4, "r"), ip(0, 1), ip(0, 99) }));
			actions.push_back(act("INCREMENT_COUNTER", { ip(0, 1), sp(4, "c") }));
		}
		f->g.lists[1].items.push_back(item(sequential("Steps", "teamAlice", false, std::move(actions))));
		f->g.start();
		f->g.lw.logic->random().enableCallLog(true);
	}
	a.g.frame();
	b.g.frame();
	CHECK(a.g.counter("Alice/c") == 10); // 21 steps: 11 draws, 10 increments
	CHECK(a.g.lw.logic->random().callLog().size() == 11);
	CHECK(a.g.lw.logic->computeStateHash() == b.g.lw.logic->computeStateHash());
	a.g.frame();
	b.g.frame();
	CHECK(a.g.counter("Alice/c") == 21);
	CHECK(a.g.lw.logic->random().callLog().size() == 21);
	CHECK(a.g.lw.logic->computeStateHash() == b.g.lw.logic->computeStateHash());
}
