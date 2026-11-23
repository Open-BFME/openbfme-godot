// OpenBFME unit tests. GPL-3.0.
// Lane THREATLEVEL-1: the map scripts' threat level. The conditions UNIT_THREAT_LEVEL (115, RW 0x7E5970) / TEAM_THREAT_LEVEL (116, RW 0x7E8A25), the
// actions SET_COUNTER_TO_UNIT_THREAT / SET_COUNTER_TO_TEAM_THREAT (439 / 440, RW 0x608BCA) and the cached threats they read (Object RW 0x697B41 /
// 0x69609F, Team RW 0x7A6A20 / 0x7A5B03; BFME2 0x695844 / 0x7A2DBB). Synthetic INI only; no game data.
//
// Every expectation is the retail branch it is ported from, never this engine's own output: a placed unit that paid nothing is worth its ThreatLevel
// times the rank factor (1 for a fresh unit) times its health ratio in RotWK (RW 0x68F0EC), and its template's ThreatLevel alone in BFME2 1.06
// (template + 0x51C); the cache refreshes only when its deadline (frame + 25: (5000 / 1000) * 5) is behind the frame.

#include "doctest.h"
#include "LogicTestUtil.h"
#include "ScriptTestUtil.h"

#include "Common/Dict.h"
#include "Common/GameEdition.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/LogicModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/ScriptEngine/ScriptConditions.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"
#include "GameLogic/ScriptEngine/ScriptThreat.h"
#include "GameLogic/System/ThreatFinder.h"

#include <cstdint>
#include <string>
#include <vector>

using namespace scripttest;

namespace
{
// TLSwordsman: a unit worth 12 (RotWK: ThreatLevel 12 * 1 * health 1; BFME2: ThreatLevel 12). TLOddSword: 12.75, for the truncation. TLWall: a STRUCTURE that
// cannot attack, worth 0 in RotWK (RW 0x68F0EC) and its ThreatLevel 7 in BFME2. TLArea: a THREAT_FINDER map object with a ThreatFinderUpdate (PORT 17).
const char kThreatObjects[] =
	"Object TLSwordsman\n"
	"  KindOf = INFANTRY CAN_ATTACK SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  ThreatLevel = 12\n"
	"  ThreatBreakdown\n"
	"    AIKindOf = INFANTRY\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"End\n"
	"Object TLOddSword\n"
	"  KindOf = INFANTRY CAN_ATTACK SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  ThreatLevel = 12.75\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"End\n"
	"Object TLWall\n"
	"  KindOf = STRUCTURE SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 10\n"
	"  GeometryHeight = 40\n"
	"  ThreatLevel = 7\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"End\n"
	"Object TLArea\n"
	"  KindOf = STRUCTURE SELECTABLE THREAT_FINDER\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 10\n"
	"  GeometryHeight = 20\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = ThreatFinderUpdate ModuleTag_TF\n"
	"    DefaultRadius = 150\n"
	"  End\n"
	"End\n";

struct ThreatWorld : ScriptGame
{
	explicit ThreatWorld(GameEdition edition = GameEdition::RotWK201)
	{
		LogicModules::registerAll(lw.w.modules);
		const std::string err = lw.w.load(kThreatObjects, INI_LOAD_OVERWRITE, "threatlevel1.ini");
		REQUIRE_MESSAGE(err.empty(), err);
		lw.logic->setEdition(edition);
		lw.logic->partition().setRegion(0.0f, 0.0f, 1000.0f, 1000.0f);
		lw.logic->settings().bodyThresholdsLoaded = true; // test-world convention (MoveTestUtil, test_port_17.cpp)
		lw.logic->settings().unitDamagedThreshold = 0.65f;
		lw.logic->settings().unitReallyDamagedThreshold = 0.4f;
		start();
	}
	Object *unit(const char *templateName, const char *player, float x, float y, const char *name = nullptr)
	{
		Object *o = lw.make(templateName, lw.teamOf(player));
		const Coord3D p{ x, y, 0.0f };
		o->setPosition(&p);
		if (name)
		{
			o->setName(name);
		}
		frames(1); // the partition takes the new position
		return o;
	}
	void frames(int n)
	{
		for (int i = 0; i < n; ++i)
		{
			lw.logic->runLogicFrame();
		}
	}
	unsigned frame() const { return lw.logic->getFrame(); }
	bool teamThreat(const std::string &team, int op, float value, float radius)
	{
		return ScriptConditions::evaluate(engine(), cond("TEAM_THREAT_LEVEL", { sp(3, team), ip(6, op), rp(value), rp(radius) }));
	}
	bool unitThreat(const std::string &unit, int op, float value, float radius)
	{
		return ScriptConditions::evaluate(engine(), cond("UNIT_THREAT_LEVEL", { sp(14, unit), ip(6, op), rp(value), rp(radius) }));
	}
	// the actions as the engine runs them (executeActions RW 0x60C1C9 owns 439 / 440: ScriptEngine::executeAction goes there first)
	void counterToTeam(const std::string &counter, const std::string &team, float radius)
	{
		engine().executeAction(act("SET_COUNTER_TO_TEAM_THREAT", { sp(4, counter), sp(3, team), rp(radius) }));
	}
	void counterToUnit(const std::string &counter, const std::string &unit, float radius)
	{
		engine().executeAction(act("SET_COUNTER_TO_UNIT_THREAT", { sp(4, counter), sp(14, unit), rp(radius) }));
	}
	Team *team(const std::string &name) { return ScriptConditions::team(engine(), name); }
};
} // namespace

TEST_CASE("THREATLEVEL-1 kinds: THREAT_FINDER is KindOf bit 195 (RW 0x608C9E: template + 0x120 bit 3, the mask at + 0x108)")
{
	CHECK(ObjectTemplateInfoBuilder::kindOfIndex("THREAT_FINDER") == 195);
}

TEST_CASE("THREATLEVEL-1 TEAM_THREAT_LEVEL: the enemies within the radius of the team centre, compared by each op at the threshold (RW 0x7E8A25 -> 0x7A6A20)")
{
	ThreatWorld v;
	v.unit("TLSwordsman", "Alice", 400.0f, 400.0f);
	v.unit("TLSwordsman", "Bob", 450.0f, 400.0f); // 50 from the team centre: inside the radius 100
	v.unit("TLSwordsman", "Bob", 600.0f, 400.0f); // 200 away: outside
	// the threat is 12 (one enemy swordsman): ops 0 <, 1 <=, 2 ==, 3 >=, 4 >, 5 !=, anything else false. The cache holds 12 for 5 s, so every question
	// below answers from the first refresh
	struct Row
	{
		float value;
		bool expect[7];
	};
	const Row rows[] = {
		{ 12.0f, { false, true, true, true, false, false, false } },
		{ 13.0f, { true, true, false, false, false, true, false } },
		{ 11.0f, { false, false, false, true, true, true, false } },
	};
	for (const Row &r : rows)
	{
		for (int op = 0; op < 7; ++op)
		{
			CAPTURE(r.value);
			CAPTURE(op);
			CHECK(v.teamThreat("Alice/teamAlice", op, r.value, 100.0f) == r.expect[op]);
		}
	}
	Team *t = v.team("Alice/teamAlice");
	REQUIRE(t != nullptr);
	CHECK(t->scriptThreatCache() == 12.0f);
	CHECK(t->scriptThreatDeadline() == v.frame() + 25); // RW 0x79FE73(5000): (5000 / 1000) * 5 + frame

	// a missing team or a missing parameter: false (RW 0x7E8A25's first tests)
	CHECK_FALSE(v.teamThreat("Alice/noSuchTeam", 3, 0.0f, 100.0f));
	CHECK_FALSE(ScriptConditions::evaluate(v.engine(), cond("TEAM_THREAT_LEVEL", { sp(3, "Alice/teamAlice"), ip(6, 3), rp(0.0f) })));
}

TEST_CASE("THREATLEVEL-1 the 5 s cache: a larger radius inside the deadline answers the first value, the frame after the deadline refreshes (RW 0x697B41)")
{
	ThreatWorld w;
	Object *guard = w.unit("TLSwordsman", "Alice", 400.0f, 400.0f, "Guard");
	w.unit("TLSwordsman", "Bob", 550.0f, 400.0f); // 150 away
	const unsigned asked = w.frame();
	CHECK(w.unitThreat("Guard", 2, 0.0f, 100.0f)); // radius 100: nothing, the cache holds 0 until asked + 25
	CHECK(guard->scriptThreatDeadline() == asked + 25);
	CHECK(w.unitThreat("Guard", 2, 0.0f, 300.0f)); // the same frame, a larger radius: still the cached 0 (the radius only matters on a refresh)
	w.frames(25);
	REQUIRE(w.frame() == asked + 25);
	CHECK(w.unitThreat("Guard", 2, 0.0f, 300.0f)); // deadline == frame: no refresh (cmp; jnc)
	w.frames(1);
	CHECK(w.unitThreat("Guard", 2, 12.0f, 300.0f)); // deadline < frame: refreshed with the radius 300
	CHECK(guard->scriptThreatDeadline() == asked + 26 + 25);
	CHECK(guard->scriptThreatCache() == 12.0f);

	// the same cache feeds the counter action: inside the deadline, a radius of 1 still answers 12
	w.counterToUnit("Alice/guardThreat", "Guard", 1.0f);
	CHECK(w.counter("Alice/guardThreat") == 12);
}

TEST_CASE("THREATLEVEL-1 UNIT_THREAT_LEVEL: a missing unit is false; the dead and the friends do not count (RW 0x7E5970 -> 0x69609F's filters)")
{
	ThreatWorld w;
	w.unit("TLSwordsman", "Alice", 400.0f, 400.0f, "Guard");
	w.unit("TLSwordsman", "Alice", 420.0f, 400.0f); // a friend: RW 0xC11DC0 wants ENEMIES
	Object *dead = w.unit("TLSwordsman", "Bob", 430.0f, 400.0f);
	w.unit("TLSwordsman", "Bob", 440.0f, 400.0f);
	dead->kill(DAMAGE_UNRESISTABLE, DEATH_NORMAL);
	w.frames(1);
	CHECK(w.unitThreat("Guard", 2, 12.0f, 100.0f)); // the one live enemy
	CHECK_FALSE(w.unitThreat("NoSuchUnit", 3, 0.0f, 100.0f));
}

TEST_CASE("THREATLEVEL-1 SET_COUNTER_TO_*_THREAT: the value truncates to an int; a missing unit or team stores 0 (RW 0x608BCA, cvttss2si)")
{
	ThreatWorld w;
	w.unit("TLSwordsman", "Alice", 400.0f, 400.0f, "Guard");
	w.unit("TLOddSword", "Bob", 430.0f, 400.0f);
	w.counterToUnit("Alice/unitThreat", "Guard", 100.0f);
	CHECK(w.counter("Alice/unitThreat") == 12); // 12.75 truncated
	w.counterToTeam("Alice/teamThreat", "Alice/teamAlice", 100.0f);
	CHECK(w.counter("Alice/teamThreat") == 12);

	w.engine().counter("Alice/kept").value = 7;
	w.counterToUnit("Alice/kept", "NoSuchUnit", 100.0f);
	CHECK(w.counter("Alice/kept") == 0); // the value stays 0.0f and is stored
	w.engine().counter("Alice/kept").value = 7;
	w.counterToTeam("Alice/kept", "Alice/noSuchTeam", 100.0f);
	CHECK(w.counter("Alice/kept") == 0);
}

TEST_CASE("THREATLEVEL-1 a team without members keeps its cache untouched (RW 0x7A5B03: no first member, return)")
{
	ThreatWorld w;
	w.unit("TLSwordsman", "Alice", 400.0f, 400.0f);
	Team *bob = w.team("Bob/teamBob");
	REQUIRE(bob != nullptr);
	REQUIRE(bob->getFirstMember() == nullptr);
	bob->setScriptThreatCache(5.0f, 0); // as if a refresh had stored 5 long ago
	CHECK(w.teamThreat("Bob/teamBob", 2, 5.0f, 1000.0f)); // the old value, and no new deadline
	CHECK(bob->scriptThreatDeadline() == 0);
}

TEST_CASE("THREATLEVEL-1 a THREAT_FINDER unit routes SET_COUNTER_TO_UNIT_THREAT to its finder (RW 0x608C9E .. 0x608CE2 -> RW 0x7EE260 / 0x7EE166)")
{
	ThreatWorld w;
	Object *area = w.lw.make("TLArea", w.lw.teamOf("Alice"));
	const Coord3D p{ 400.0f, 400.0f, 0.0f };
	area->setPosition(&p);
	area->setName("GateArea");
	Dict props;
	props.setReal("objectThreatFinderRadius", 150.0f);
	area->friend_onMapPlacement(&props);
	w.frames(1);
	REQUIRE(w.lw.logic->threatFinders().find(*w.lw.logic, "GateArea") != nullptr);
	w.unit("TLSwordsman", "Bob", 520.0f, 400.0f); // 120 away: inside the finder's 150, outside the action's radius 10
	w.counterToUnit("Alice/gate", "GateArea", 10.0f);
	CHECK(w.counter("Alice/gate") == 12); // the finder's answer for Alice's enemies, the radius unused
	CHECK(area->scriptThreatDeadline() == 0); // the object's own cache is not read

	// the same unit without its finder (a structure built on a plot makes none): the value stays 0
	Object *plain = w.lw.make("TLArea", w.lw.teamOf("Alice"));
	plain->setPosition(&p);
	plain->setName("BuiltArea");
	w.frames(1);
	w.engine().counter("Alice/built").value = 9;
	w.counterToUnit("Alice/built", "BuiltArea", 500.0f);
	CHECK(w.counter("Alice/built") == 0);
}

TEST_CASE("THREATLEVEL-1 editions: BFME2 1.06 sums the template ThreatLevel (BFME2 0x695844 / 0x7A2DBB), RotWK the threat value (RW 0x68F0EC)")
{
	for (GameEdition e : { GameEdition::RotWK201, GameEdition::BFME2_106 })
	{
		CAPTURE(gameEditionName(e));
		ThreatWorld w(e);
		w.unit("TLSwordsman", "Alice", 400.0f, 400.0f, "Guard");
		w.unit("TLWall", "Bob", 450.0f, 400.0f);      // RotWK 0 (a structure that cannot attack), BFME2 7
		w.unit("TLSwordsman", "Bob", 430.0f, 400.0f); // 12 in both
		w.counterToUnit("Alice/sum", "Guard", 100.0f);
		CHECK(w.counter("Alice/sum") == (e == GameEdition::BFME2_106 ? 19 : 12));
		w.counterToTeam("Alice/teamSum", "Alice/teamAlice", 100.0f);
		CHECK(w.counter("Alice/teamSum") == (e == GameEdition::BFME2_106 ? 19 : 12));
	}
}

TEST_CASE("THREATLEVEL-1 snapshot: the team and object caches travel (Team::xfer 5, RW 0x7A5C06; Object::xfer 3) and the load gives the saved state hash back")
{
	ThreatWorld w;
	Object *guard = w.unit("TLSwordsman", "Alice", 400.0f, 400.0f, "Guard");
	w.unit("TLSwordsman", "Bob", 430.0f, 400.0f);
	const unsigned asked = w.frame();
	w.counterToTeam("Alice/t", "Alice/teamAlice", 100.0f);
	w.counterToUnit("Alice/u", "Guard", 100.0f);
	Team *alice = w.team("Alice/teamAlice");
	REQUIRE(alice != nullptr);
	const float teamValue = alice->scriptThreatCache();
	const unsigned teamDeadline = alice->scriptThreatDeadline();
	REQUIRE(teamValue == 12.0f);
	REQUIRE(guard->scriptThreatDeadline() == asked + 25);
	REQUIRE(teamDeadline == asked + 25);
	const ObjectID guardID = guard->getID();

	const unsigned guardDeadline = guard->scriptThreatDeadline();
	const std::uint32_t savedHash = w.lw.logic->computeStateHash();
	std::vector<std::uint8_t> saved;
	std::string error;
	REQUIRE_MESSAGE(w.lw.logic->saveSnapshot(saved, &error), error);
	alice->setScriptThreatCache(99.0f, 12345u);
	guard->setScriptThreatCache(77.0f, 54321u);
	REQUIRE_MESSAGE(w.lw.logic->loadSnapshot(saved, &error), error);
	// Sol review (THREATLEVEL-1): the state hash covers both caches, and CommandLogLoad::beginSnapshot refuses a load whose hash is not the saved one
	CHECK(w.lw.logic->computeStateHash() == savedHash);

	Team *loaded = w.team("Alice/teamAlice");
	REQUIRE(loaded != nullptr);
	CHECK(loaded->scriptThreatCache() == teamValue);
	CHECK(loaded->scriptThreatDeadline() == teamDeadline);
	const Object *g = w.lw.logic->findObjectByID(guardID);
	REQUIRE(g != nullptr);
	// RotWK's own xfer (RW 0x697F28) has no such field and a retail load starts the pair cleared; the OpenBFME object snapshot keeps it (S-7184)
	CHECK(g->scriptThreatCache() == 12.0f);
	CHECK(g->scriptThreatDeadline() == guardDeadline);
}
