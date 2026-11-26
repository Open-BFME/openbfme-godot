// OpenBFME. PORT-101 tests: Object/SelectionPriority (table row RW 0xDA49D8, INI::parseInt RW 0x42EC5E, ThingTemplate + 0x64C,
// ctor default 0x7fffffff at RW 0x740069..0x740074, ThingTemplate::copyFrom RW 0x6D271E, sole reader addDrawableToList RW 0x7108F2 at RW 0x710A41).
// GPL-3.0. No retail game files required.

#include "doctest.h"

#include "ObjectTestUtil.h"
#include "SyntheticHudTestUtil.h"

#include "Common/Thing/ThingTemplate.h"
#include "GameClient/HudObjects.h"
#include "GameLogic/Object/Object.h"

#include <climits>
#include <vector>

namespace
{
Object *placeAt(synthetichud::Rig &r, const char *name, float x, float y, float z, const char *owner = "Alice")
{
	Object *o = r.make(name, r.teamOf(owner), INVALID_ID, false);
	const Coord3D p{ x, y, z };
	o->setPosition(&p);
	synthetichud::Rig::completeHorde(o);
	return o;
}
} // namespace

TEST_CASE("PORT-101 ThingTemplate SelectionPriority: parses integers and macros, defaults to INT_MAX, and inherits across ChildObject, ObjectReskin and overrides")
{
	objtest::World w;
	const std::string ini =
		"#define SELECTION_PRIORITY_CREBAIN 1\n"
		"#define SELECTION_PRIORITY_MORDOR_WORKER 2\n"
		"#define SELECTION_PRIORITY_PORTER 3\n"
		"Object DefaultUnit\n"
		"  KindOf = SELECTABLE INFANTRY\n"
		"End\n"
		"Object CrebainUnit\n"
		"  KindOf = SELECTABLE AIRCRAFT\n"
		"  SelectionPriority = SELECTION_PRIORITY_CREBAIN\n"
		"End\n"
		"Object WorkerUnit\n"
		"  KindOf = SELECTABLE INFANTRY\n"
		"  SelectionPriority = SELECTION_PRIORITY_MORDOR_WORKER\n"
		"End\n"
		"Object PorterUnit\n"
		"  KindOf = SELECTABLE DOZER\n"
		"  SelectionPriority = SELECTION_PRIORITY_PORTER\n"
		"End\n"
		"ChildObject InheritedWorker WorkerUnit\n"
		"End\n"
		"ChildObject OverriddenWorker WorkerUnit\n"
		"  SelectionPriority = 1\n"
		"End\n"
		"ObjectReskin ReskinnedWorker WorkerUnit\n"
		"End\n"
		"Object MinusOneUnit\n"
		"  KindOf = SELECTABLE INFANTRY\n"
		"  SelectionPriority = -1\n"
		"End\n"
		"Object ExcludedUnit\n"
		"  KindOf = SELECTABLE INFANTRY\n"
		"  SelectionPriority = -2\n"
		"End\n";
	REQUIRE(w.load(ini).empty());

	const ThingTemplate *def = w.get("DefaultUnit");
	const ThingTemplate *crebain = w.get("CrebainUnit");
	const ThingTemplate *worker = w.get("WorkerUnit");
	const ThingTemplate *porter = w.get("PorterUnit");
	const ThingTemplate *inherited = w.get("InheritedWorker");
	const ThingTemplate *overridden = w.get("OverriddenWorker");
	const ThingTemplate *reskinned = w.get("ReskinnedWorker");
	const ThingTemplate *minusOne = w.get("MinusOneUnit");
	const ThingTemplate *excluded = w.get("ExcludedUnit");
	REQUIRE(def);
	REQUIRE(crebain);
	REQUIRE(worker);
	REQUIRE(porter);
	REQUIRE(inherited);
	REQUIRE(overridden);
	REQUIRE(reskinned);
	REQUIRE(minusOne);
	REQUIRE(excluded);

	CHECK(def->selectionPriority() == INT_MAX);
	CHECK(crebain->selectionPriority() == 1);
	CHECK(worker->selectionPriority() == 2);
	CHECK(porter->selectionPriority() == 3);
	CHECK(inherited->selectionPriority() == 2);
	CHECK(overridden->selectionPriority() == 1);
	CHECK(reskinned->selectionPriority() == 2);
	CHECK(minusOne->selectionPriority() == -1);
	CHECK(excluded->selectionPriority() == -2);

	const std::string overrideIni =
		"Object DefaultUnit\n"
		"  SelectionPriority = -2\n"
		"End\n";
	REQUIRE(w.load(overrideIni, INI_LOAD_CREATE_OVERRIDES, "map.ini").empty());
	CHECK(def->selectionPriority() == -2);
}

TEST_CASE("PORT-101 single click: overlapping objects pick by collision tier and nearest ray hit without reading SelectionPriority, while priority < -1 rejects")
{
	synthetichud::Rig r;
	REQUIRE(r.w.load(
		"#define SELECTION_PRIORITY_CREBAIN 1\n"
		"Object DefaultNear\n"
		"  KindOf = SELECTABLE INFANTRY\n"
		"End\n"
		"Object DefaultFar\n"
		"  KindOf = SELECTABLE INFANTRY\n"
		"End\n"
		"Object LowPriorityUnit\n"
		"  KindOf = SELECTABLE AIRCRAFT\n"
		"  SelectionPriority = SELECTION_PRIORITY_CREBAIN\n"
		"End\n"
		"Object HighPriorityUnit\n"
		"  KindOf = SELECTABLE INFANTRY\n"
		"End\n"
		"Object ExcludedUnit\n"
		"  KindOf = SELECTABLE INFANTRY\n"
		"  SelectionPriority = -2\n"
		"End\n"
		"Object MinusOneUnit\n"
		"  KindOf = SELECTABLE INFANTRY\n"
		"  SelectionPriority = -1\n"
		"End\n").empty());

	// Control without SelectionPriority: nearer hit (z = 20) wins over farther (z = 0)
	Object *defFar = placeAt(r, "DefaultFar", 60.0f, 60.0f, 0.0f);
	Object *defNear = placeAt(r, "DefaultNear", 60.0f, 60.0f, 20.0f);
	(void)defFar;
	r.click(60, 60);
	r.selected({ defNear->getID() });

	// Two own objects under one click point: RW W3DView::pickDrawable (RW 0x48AB28) ignores +0x64C and picks nearer hit
	Object *highFar = placeAt(r, "HighPriorityUnit", 140.0f, 100.0f, 0.0f);
	Object *lowNear = placeAt(r, "LowPriorityUnit", 140.0f, 100.0f, 20.0f);
	(void)highFar;
	r.click(140, 100);
	r.selected({ lowNear->getID() });

	// Reversed Z: high priority nearer wins by ray distance
	Object *lowFar = placeAt(r, "LowPriorityUnit", 220.0f, 100.0f, 0.0f);
	Object *highNear = placeAt(r, "HighPriorityUnit", 220.0f, 100.0f, 20.0f);
	(void)lowFar;
	r.click(220, 100);
	r.selected({ highNear->getID() });

	// Enemy nearer vs own farther: collision tier 0x104 (own) beats 0x14 (enemy) regardless of SelectionPriority
	Object *ownFar = placeAt(r, "LowPriorityUnit", 300.0f, 100.0f, 0.0f, "Alice");
	placeAt(r, "HighPriorityUnit", 300.0f, 100.0f, 20.0f, "Bob");
	r.click(300, 100);
	r.selected({ ownFar->getID() });

	// RW 0x485D6F -> addDrawableToList RW 0x710A47: priority < -1 returns 2 (rejected even on point click), priority == -1 is accepted
	placeAt(r, "ExcludedUnit", 380.0f, 100.0f, 0.0f);
	r.click(380, 100);
	r.selected({});
	CHECK(HudObjects::objectsInRegion(r.input->context(), { { 380, 100 }, { 380, 100 } }).empty());

	Object *minusOne = placeAt(r, "MinusOneUnit", 460.0f, 100.0f, 0.0f);
	r.click(460, 100);
	r.selected({ minusOne->getID() });
}

TEST_CASE("PORT-101 drag select: low-priority units near soldiers follow reverse-creation priority and RW jle retention")
{
	synthetichud::Rig r;
	REQUIRE(r.w.load(
		"#define SELECTION_PRIORITY_CREBAIN 1\n"
		"#define SELECTION_PRIORITY_MORDOR_WORKER 2\n"
		"Object MordorFighter\n"
		"  KindOf = SELECTABLE INFANTRY\n"
		"End\n"
		"Object MordorWorker\n"
		"  KindOf = SELECTABLE INFANTRY\n"
		"  SelectionPriority = SELECTION_PRIORITY_MORDOR_WORKER\n"
		"End\n"
		"Object MordorCrebain\n"
		"  KindOf = SELECTABLE AIRCRAFT\n"
		"  SelectionPriority = SELECTION_PRIORITY_CREBAIN\n"
		"End\n"
		"Object ExcludedCritter\n"
		"  KindOf = SELECTABLE INFANTRY\n"
		"  SelectionPriority = -2\n"
		"End\n"
		"Object CastleBuildPlot\n"
		"  KindOf = STRUCTURE SELECTABLE IMMOBILE BASE_FOUNDATION\n"
		"End\n"
		"Object ClickProp\n"
		"  KindOf = SELECTABLE CLICK_THROUGH\n"
		"End\n"
		"Object InertTree\n"
		"  KindOf = SHRUBBERY\n"
		"End\n").empty());

	// 1) Soldiers exist first; workers and crebain spawn later: reverse creation order visits crebain (1), then worker (2), then fighter (INT_MAX).
	// Each strictly higher priority clears lower-priority objects (RW 0x710A16..0x710A25) -> only fighter stays.
	Object *fighter1 = r.place("MordorFighter", 100, 100);
	r.place("MordorWorker", 130, 100);
	r.place("MordorCrebain", 160, 100);
	r.drag(80, 80, 180, 120);
	r.selected({ fighter1->getID() });

	// 2) Reverse spawn order: worker (2) spawned BEFORE fighter (INT_MAX).
	// Reverse creation order visits fighter (INT_MAX) first -> bestPriority = INT_MAX -> worker (2 <= INT_MAX) falls through jle 0x710a29 -> BOTH kept.
	Object *worker2 = r.place("MordorWorker", 260, 100);
	Object *fighter2 = r.place("MordorFighter", 290, 100);
	const std::vector<Object *> picked2 = HudObjects::objectsInRegion(r.input->context(), { { 240, 80 }, { 320, 120 } });
	REQUIRE(picked2.size() == 2);
	CHECK(picked2[0] == fighter2);
	CHECK(picked2[1] == worker2);
	r.drag(240, 80, 320, 120);
	r.selected({ worker2->getID(), fighter2->getID() });

	// 3) Crebain (1) vs Worker (2) without fighter: crebain spawned AFTER worker -> crebain visited first, cleared by worker -> only worker.
	Object *worker3 = r.place("MordorWorker", 380, 100);
	r.place("MordorCrebain", 410, 100);
	r.drag(360, 80, 440, 120);
	r.selected({ worker3->getID() });

	// 4) Pre-gates before SelectionPriority (RW 0x83C3E7..0x83C3EE m_mustNotBe BASE_FOUNDATION / CLICK_THROUGH, RW 0x710964 isSelectable) + < -1 reject:
	// these default-INT_MAX unselectable / excluded objects are spawned BEFORE worker4 (so visited AFTER worker4) and must NOT clear worker4.
	r.place("CastleBuildPlot", 100, 260);
	r.place("ClickProp", 120, 260);
	r.place("InertTree", 140, 260);
	r.place("MordorFighter", 155, 260)->friend_setEffectivelyDead(true);
	r.place("MordorFighter", 168, 260)->setScriptSelectable(false);
	Object *worker4 = r.place("MordorWorker", 180, 260);
	r.place("ExcludedCritter", 200, 260);
	r.drag(80, 240, 220, 280);
	r.selected({ worker4->getID() });

	// 5) Control without SelectionPriority: both fighters selected unchanged.
	Object *plainA = r.place("MordorFighter", 380, 260);
	Object *plainB = r.place("MordorFighter", 420, 260);
	r.drag(360, 240, 440, 280);
	r.selected({ plainA->getID(), plainB->getID() });
}

TEST_CASE("PORT-101 horde promotion: drag box over a horde member reads the HORDE container SelectionPriority (RW 0x7109CE..0x710A41)")
{
	synthetichud::Rig r;
	REQUIRE(r.w.load(
		"Object HighSoldier\n"
		"  KindOf = SELECTABLE INFANTRY\n"
		"End\n"
		"Object LowPriorityHorde\n"
		"  KindOf = SELECTABLE HORDE INFANTRY\n"
		"  SelectionPriority = 1\n"
		"End\n"
		"Object HordeSoldier\n"
		"  KindOf = SELECTABLE INFANTRY HORDE_MEMBER\n"
		"End\n").empty());

	// Spawn HighSoldier first, then LowPriorityHorde (off-box) and its member (inside box).
	// Reverse creation order visits member -> promotes stand = LowPriorityHorde (priority 1, NOT member's INT_MAX) -> cleared by HighSoldier (INT_MAX).
	Object *soldier = r.place("HighSoldier", 100, 100);
	Object *horde = r.place("LowPriorityHorde", 800, 600);
	Object *member = r.place("HordeSoldier", 150, 100);
	member->friend_setContainedBy(horde);
	member->setProducer(horde);

	r.drag(80, 80, 180, 120);
	r.selected({ soldier->getID() });

	// Alone in a drag box (member only, horde center outside), LowPriorityHorde itself is selected via promotion.
	r.drag(130, 80, 180, 120);
	r.selected({ horde->getID() });
}
