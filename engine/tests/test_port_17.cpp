// OpenBFME unit tests. GPL-3.0.
// PORT-17 (issue #73): ThreatFinderUpdate and the script action SET_COUNTER_TO_THREAT_FINDER_THREAT. Synthetic INI only; no game data.
//
// Every expectation is the RotWK branch it is ported from, never this engine's own output: the module's slot 45 (RW 0x7EDECE) makes a finder named after the
// map object and registers it in TheThreatFinderManager (RW 0xDE8888), the update (RW 0x7EDDB6) copies the object's position into it every frame, the finder
// scans with RW 0x7EE057 (the port's AIThreatFinder scan, an object's threat is RW 0x68F0EC), the answer is RW 0x7EE1F4 / 0x7EE166 and the action is RW 0x7C68AA.

#include "doctest.h"
#include "LogicTestUtil.h"
#include "ScriptTestUtil.h"

#include "Common/Dict.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/LogicModules.h"
#include "GameLogic/Module/ThreatFinderUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/ScriptEngine/ScriptActions.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/SkirmishAI/AIThreatFinder.h"
#include "GameLogic/System/ThreatFinder.h"

#include <cstdint>
#include <string>
#include <vector>

using namespace scripttest;

namespace
{
// The area is the object P17Area (DefaultRadius 150, the table RW 0xC4C320's one row); the units' ThreatLevel decides what they count: RW 0x68F0EC takes the
// template's ThreatLevel when the object paid nothing (a placed object), times the body's health ratio (1 here). CAN_ATTACK is what the scan looks for
// (RW 0x7EE057's KindOf test); a STRUCTURE counts for a map area (+ 0x5B8 = 0: the map's finder is made "count the structures in it").
const char kPort17Objects[] =
	"Object P17Area\n"
	"  KindOf = STRUCTURE SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 10\n"
	"  GeometryHeight = 20\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = ThreatFinderUpdate ModuleTag_TF\n"
	"    DefaultRadius = 150\n"
	"  End\n"
	"End\n"
	"Object P17AreaSmallData\n"
	"  KindOf = STRUCTURE SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 10\n"
	"  GeometryHeight = 20\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = ThreatFinderUpdate ModuleTag_TF\n"
	"    DefaultRadius = 150\n"
	"  End\n"
	"End\n"
	"Object P17Tower\n"
	"  KindOf = STRUCTURE CAN_ATTACK SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 10\n"
	"  GeometryHeight = 40\n"
	"  ThreatLevel = 120\n"
	"  ThreatBreakdown\n"
	"    AIKindOf = SIEGEWEAPON\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"End\n"
	"Object P17Blocker\n"
	"  KindOf = STRUCTURE SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 10\n"
	"  GeometryHeight = 40\n"
	"  ThreatLevel = 7\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"End\n"
	"Object P17Swordsman\n"
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
	"End\n";

// a script game whose world has the object layer's modules registered, the templates above and a partition region (the map's extent)
struct Port17World : ScriptGame
{
	Port17World()
	{
		LogicModules::registerAll(lw.w.modules);
		const std::string err = lw.w.load(kPort17Objects, INI_LOAD_OVERWRITE, "port17.ini");
		REQUIRE_MESSAGE(err.empty(), err);
		lw.logic->partition().setRegion(0.0f, 0.0f, 1000.0f, 1000.0f);
		// the bodies' damage thresholds: the logic's settings here have no GameData behind them (test-world convention, MoveTestUtil)
		lw.logic->settings().bodyThresholdsLoaded = true;
		lw.logic->settings().unitDamagedThreshold = 0.65f;
		lw.logic->settings().unitReallyDamagedThreshold = 0.4f;
		start();
	}

	// a map object: made, named, placed, then the map loop's slot-45 call (RW 0x694692) with the map object's properties (`props` may be null: a structure
	// built on a plot, RW 0x858A94)
	Object *mapObject(const char *templateName, const char *name, float x, float y, const Dict *props)
	{
		Object *o = lw.make(templateName, lw.teamOf("Alice"));
		const Coord3D p{ x, y, 0.0f };
		o->setPosition(&p);
		o->setName(name);
		o->friend_onMapPlacement(props);
		frames(1); // the partition takes the new position before the area is asked
		return o;
	}
	// a unit of a side standing at a point
	Object *unit(const char *templateName, const char *player, float x, float y)
	{
		Object *o = lw.make(templateName, lw.teamOf(player));
		const Coord3D p{ x, y, 0.0f };
		o->setPosition(&p);
		frames(1);
		return o;
	}
	ThreatFinderUpdate *finderModule(Object &o)
	{
		auto *m = dynamic_cast<ThreatFinderUpdate *>(o.findModule("ThreatFinderUpdate"));
		REQUIRE_MESSAGE(m != nullptr, "the synthetic area has a ThreatFinderUpdate");
		return m;
	}
	void frames(int n)
	{
		for (int i = 0; i < n; ++i)
		{
			lw.logic->runLogicFrame();
		}
	}
	ThreatFinder *finder(const char *name) { return lw.logic->threatFinders().find(*lw.logic, name); }
	// RW 0x7C68AA: the action as a map script stores it (counter, the area's name, the relationship, the player)
	void threatAction(const std::string &counter, const std::string &area, const std::string &relationship, const std::string &player)
	{
		ScriptActions::execute(engine(), act("SET_COUNTER_TO_THREAT_FINDER_THREAT", { sp(4, counter), sp(67, area), sp(66, relationship), sp(11, player) }));
	}
};

Dict radiusProperty(float r)
{
	Dict d;
	d.setReal("objectThreatFinderRadius", r); // RW 0x7EDECE's literal (INFERENCE BFME2 0x00C0953C)
	return d;
}

unsigned long long notesOf(ScriptEngine &engine, const std::string &text)
{
	const auto it = engine.stats().notes.find(text);
	return it == engine.stats().notes.end() ? 0 : it->second;
}
} // namespace

TEST_CASE("PORT-17 module: a placed map object registers a finder named after itself, with the map property's radius or the data's DefaultRadius")
{
	Port17World w;
	Dict props = radiusProperty(90.0f);
	Object *area = w.mapObject("P17Area", "GateArea", 400.0f, 400.0f, &props);
	// RW 0x7EDECE: the finder, its name (Object + 0x88) and its radius (the property before the data's + 0x08)
	ThreatFinder *f = w.finder("GateArea");
	REQUIRE(f != nullptr);
	CHECK(f == w.finderModule(*area)->finder());
	CHECK(f->name() == "GateArea");
	CHECK(f->radius() == 90.0f);
	CHECK_FALSE(f->noStructures()); // a map area counts the structures in it (+ 0x5B8 = 0)
	CHECK(f->position().x == 400.0f);
	CHECK(f->position().y == 400.0f);
	CHECK(w.lw.logic->threatFinders().size() == 1);

	// without the property the data's DefaultRadius (table RW 0xC4C320, + 0x08) is the radius
	Dict none;
	Object *plain = w.mapObject("P17AreaSmallData", "PlainArea", 700.0f, 400.0f, &none);
	CHECK_FALSE(none.known("objectThreatFinderRadius"));
	ThreatFinder *p = w.finder("PlainArea");
	REQUIRE(p != nullptr);
	CHECK(p == w.finderModule(*plain)->finder());
	CHECK(p->radius() == 150.0f);
	CHECK(w.lw.logic->threatFinders().size() == 2);
}

TEST_CASE("PORT-17 module gates: a null properties argument makes no finder, an empty name registers nothing, and the object's death takes its finder out")
{
	Port17World w;
	// RW 0x858A94: a structure built on a plot has no map properties, so slot 45 makes no finder at all
	Object *built = w.mapObject("P17Area", "BuiltArea", 400.0f, 400.0f, nullptr);
	CHECK(w.finderModule(*built)->finder() == nullptr);
	CHECK(w.finder("BuiltArea") == nullptr);
	CHECK(w.lw.logic->threatFinders().size() == 0);

	// a map object without a name (Object + 0x88 empty): the finder exists, the manager holds no key for it (INFERENCE BFME2 0x003ECDB7)
	Object *anon = w.mapObject("P17Area", "", 500.0f, 500.0f, nullptr);
	Dict props;
	anon->friend_onMapPlacement(&props);
	REQUIRE(w.finderModule(*anon)->finder() != nullptr);
	CHECK(w.finderModule(*anon)->finder()->name().empty());
	CHECK(w.lw.logic->threatFinders().size() == 0);

	// the object leaves the world: the module's destructor has its finder unregister first (RW 0x7EDF5B / the BFME2 dtor's 0x003ECDB7)
	Dict named;
	Object *area = w.mapObject("P17Area", "Doomed", 600.0f, 600.0f, &named);
	REQUIRE(w.finder("Doomed") != nullptr);
	w.lw.logic->destroyObject(area);
	w.frames(1); // the destroy list runs at the end of the logic frame: the object and its module go there
	CHECK(w.finder("Doomed") == nullptr);
	CHECK(w.lw.logic->threatFinders().size() == 0);
}

TEST_CASE("PORT-17 script: SET_COUNTER_TO_THREAT_FINDER_THREAT takes the area's enemies' threat (RW 0x7C68AA -> 0x7EE1F4)")
{
	Port17World w;
	Dict props = radiusProperty(150.0f);
	w.mapObject("P17Area", "GateArea", 400.0f, 400.0f, &props);
	// inside the radius: an enemy swordsman (12: ThreatLevel * 1.0, RW 0x68F0EC), an enemy tower (a CAN_ATTACK STRUCTURE with ThreatLevel 120, above the
	// function's floor of 100, so 120) and an enemy blocker (a STRUCTURE that cannot attack: 0); outside: an enemy swordsman that must not count. Both
	// counting templates carry a ThreatBreakdown: an object without one has the category -1 and RW 0x7EDB72 adds it to the total TWICE (S-1300's note;
	// test_skirmish_ai_u22.cpp pins that on the skirmish finder)
	w.unit("P17Swordsman", "Bob", 430.0f, 400.0f);
	w.unit("P17Tower", "Bob", 400.0f, 460.0f);
	w.unit("P17Blocker", "Bob", 470.0f, 400.0f);
	w.unit("P17Swordsman", "Bob", 900.0f, 900.0f);
	// and one of Alice's own, which an "Enemies" answer leaves out
	w.unit("P17Swordsman", "Alice", 410.0f, 410.0f);

	w.threatAction("Alice/gateThreat", "GateArea", "Enemies", "Alice");
	CHECK(w.counter("Alice/gateThreat") == 132); // 12 + 120 (the blocker's 0 adds nothing)
	w.threatAction("Alice/gateAllies", "GateArea", "Allies", "Alice");
	CHECK(w.counter("Alice/gateAllies") == 12);
	// Bob's view is the other way round
	w.threatAction("Bob/gateThreat", "GateArea", "Enemies", "Bob");
	CHECK(w.counter("Bob/gateThreat") == 12);
	w.threatAction("Bob/gateAllies", "GateArea", "Allies", "Bob");
	CHECK(w.counter("Bob/gateAllies") == 132);
	// nothing was reported: the finder was found
	CHECK(notesOf(w.engine(), "WARNING - Threat Finder not found during script execution: GateArea.") == 0);
}

TEST_CASE("PORT-17 script gates: another relationship, a missing area and a mask of no player leave the counter alone")
{
	Port17World w;
	Dict props = radiusProperty(150.0f);
	w.mapObject("P17Area", "GateArea", 400.0f, 400.0f, &props);
	w.unit("P17Swordsman", "Bob", 430.0f, 400.0f);

	// parameter 2 is "Enemies" or "Allies" exactly: any other text returns before the counter is touched (INFERENCE BFME2 0x003C5E10)
	w.threatAction("Alice/any", "GateArea", "Both", "Alice");
	CHECK(w.counter("Alice/any") == -999); // not created: the counter map has no such entry

	// RW 0x7EE260 misses: retail appends its warning and writes nothing (INFERENCE BFME2 0x003C5E10)
	w.threatAction("Alice/missing", "NoSuchArea", "Enemies", "Alice");
	CHECK(w.counter("Alice/missing") == -999);
	CHECK(notesOf(w.engine(), "WARNING - Threat Finder not found during script execution: NoSuchArea.") == 1);

	// RW 0x6A85B6: the mask names one player; several name none, and the action does nothing
	w.engine().counter("Alice/kept").value = 7;
	ScriptActionRec many = act("SET_COUNTER_TO_THREAT_FINDER_THREAT", { sp(4, "Alice/kept"), sp(67, "GateArea"), sp(66, "Enemies"), sp(11, "<All Players>") });
	ScriptActions::execute(w.engine(), many);
	CHECK(w.engine().counter("Alice/kept").value == 7);

	// a counter that already exists keeps its value when the area is not found
	ScriptActionRec gone = act("SET_COUNTER_TO_THREAT_FINDER_THREAT", { sp(4, "Alice/kept"), sp(67, "NoSuchArea"), sp(66, "Enemies"), sp(11, "Alice") });
	ScriptActions::execute(w.engine(), gone);
	CHECK(w.engine().counter("Alice/kept").value == 7);
	CHECK(notesOf(w.engine(), "WARNING - Threat Finder not found during script execution: NoSuchArea.") == 2);
}

TEST_CASE("PORT-17 update: the finder follows its object every frame, and the area's answer follows it (RW 0x7EDDB6)")
{
	Port17World w;
	Dict props = radiusProperty(150.0f);
	Object *area = w.mapObject("P17Area", "GateArea", 400.0f, 400.0f, &props);
	ThreatFinder *f = w.finder("GateArea");
	REQUIRE(f != nullptr);
	// the enemy stands within 150 of (400, 400) and far from (700, 400)
	w.unit("P17Swordsman", "Bob", 450.0f, 400.0f);
	w.threatAction("Alice/at", "GateArea", "Enemies", "Alice");
	CHECK(w.counter("Alice/at") == 12);

	const Coord3D moved{ 700.0f, 400.0f, 0.0f };
	area->setPosition(&moved);
	w.lw.logic->runLogicFrame();
	CHECK(f->position().x == 700.0f); // the module's update copied Object + 0x38
	CHECK(f->position().y == 400.0f);
	// the scan is 3 s old at most (RW 0xBDD42C); the frames since the first answer are 1, so 5 * 1 >= 3 and the action rescans there
	w.threatAction("Alice/away", "GateArea", "Enemies", "Alice");
	CHECK(w.counter("Alice/away") == 0); // the swordsman is 250 away now
}

TEST_CASE("PORT-17 snapshot: the finder's records, radius and name travel in the object's block and the area answers the same after a load (RW 0x7EDF70)")
{
	Port17World w;
	Dict props = radiusProperty(150.0f);
	Object *area = w.mapObject("P17Area", "GateArea", 400.0f, 400.0f, &props);
	w.unit("P17Swordsman", "Bob", 430.0f, 400.0f);
	w.threatAction("Alice/before", "GateArea", "Enemies", "Alice");
	REQUIRE(w.counter("Alice/before") == 12);
	ThreatFinder *f = w.finder("GateArea");
	REQUIRE(f != nullptr);
	CHECK(f->record(2).v[0] == 12.0f); // the enemy player's record: the total (Bob is index 2)
	const float infantry = f->record(2).v[1 + AIThreatFinder::threatCategory(*w.lw.w.get("P17Swordsman"))];
	CHECK(infantry == 12.0f); // and its ThreatBreakdown category (RW 0x7EDB72)

	const ObjectID areaID = area->getID();
	std::vector<std::uint8_t> saved;
	std::string error;
	REQUIRE_MESSAGE(w.lw.logic->saveSnapshot(saved, &error), error);
	// the world drifts: another enemy enters the radius and the records change
	w.unit("P17Tower", "Bob", 460.0f, 400.0f);
	w.threatAction("Alice/drift", "GateArea", "Enemies", "Alice");
	CHECK(w.counter("Alice/drift") == 132);
	REQUIRE_MESSAGE(w.lw.logic->loadSnapshot(saved, &error), error);
	area = w.lw.logic->findObjectByID(areaID);
	REQUIRE(area != nullptr);
	// RW 0x7EDF70: the finder is made again on the load and registered under the object's name
	ThreatFinder *loaded = w.finder("GateArea");
	REQUIRE(loaded != nullptr);
	CHECK(loaded == w.finderModule(*area)->finder());
	CHECK(loaded->radius() == 150.0f);
	CHECK(loaded->record(2).v[0] == 12.0f);
	// a second save of the loaded game is byte-identical: nothing of the finder's hides outside its module's block
	std::vector<std::uint8_t> again;
	REQUIRE_MESSAGE(w.lw.logic->saveSnapshot(again, &error), error);
	CHECK(again == saved);
	w.threatAction("Alice/after", "GateArea", "Enemies", "Alice");
	CHECK(w.counter("Alice/after") == 12);
}

TEST_CASE("PORT-17 neighbor: the skirmish AI's own finder still leaves structures out, which is the one difference from a map area's finder")
{
	Port17World w;
	Dict props = radiusProperty(150.0f);
	w.mapObject("P17Area", "GateArea", 400.0f, 400.0f, &props);
	Object *tower = w.unit("P17Tower", "Bob", 430.0f, 400.0f);
	REQUIRE(tower->isKindOfName("STRUCTURE"));
	REQUIRE(tower->isKindOfName("CAN_ATTACK"));
	const Player &alice = *w.lw.players.findPlayerWithName("Alice");
	// RW 0x7EE057 with + 0x5B8 = 1 (the AITarget's finder, RW 0x6C6939): the tower's 120 is left out; the map area (+ 0x5B8 = 0) counts it
	const Coord3D at{ 400.0f, 400.0f, 0.0f };
	CHECK(AIThreatFinder::threatTotal(*w.lw.logic, at, 150.0f, true, alice, 0) == 0.0f);
	CHECK(AIThreatFinder::threatTotal(*w.lw.logic, at, 150.0f, false, alice, 0) == 120.0f);
	w.threatAction("Alice/gate", "GateArea", "Enemies", "Alice");
	CHECK(w.counter("Alice/gate") == 120);
}
