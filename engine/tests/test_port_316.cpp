// OpenBFME. GPL-3.0.
// PORT-316 (fork issue 752): a drawable's model condition flush points — the creation paths and the queries that read the draw modules
// mid-frame. Stops: S-1583 (narrowed by this file, docs/stops.d/S-7646.md).
//
// WHAT ROTWK DOES (read with rw.py dis on RotWK 2.01 game.dat; the flush is the byte Drawable + 0x443 — every instruction of the binary
// that touches it was scanned, so this list is complete):
//   RW 0x67449C Drawable's flush (Bool force): nothing without the force when + 0x443 is clear (RW 0x6744A3), else the pending clear /
//     set masks + 0x2A4 / + 0x2F0 are merged into + 0x258 (RW 0x6744CA -> 0x671F84) and every draw interface's slot 0 takes the set
//     (RW 0x6744EC), then + 0x443 = 0 (RW 0x6744F7). Called from 40 sites, the logic's own included.
//   RW 0x674673, RW 0x6756A1, RW 0x674B1F contain the same four steps inlined: a query of the draw modules flushes first
//     (+ 0x443 at RW 0x674676 / 0x6756A8 / 0x674E1B, + 0x258 handed on at RW 0x6746B4 / 0x67572D / 0x674E6A, cleared at RW 0x6746BF /
//     0x6756F4 / 0x674E74).
//   RW 0x675996 (called from RW 0x6487D6 the client's frame) holds the per-client-frame flush (RW 0x6759C4 .. 0x675A11).
//   RW 0x690F02 Object::friend_bindToDrawable: the CREATION path. The new drawable takes the object's WHOLE condition set at bind time:
//     two 0x4C-byte sets are cleared (RW 0x690F22 / 0x690F2E), then for each of the 0x68 slots the table RW 0xC16958 names the drawable
//     bit (-1: none) and the object's bit goes into the set or the clear buffer (RW 0x690F5B test of Object + 0x38C, RW 0x690F6B /
//     0x690F74 buffer choice, RW 0x690F83 or / and), then NIGHT (slot 7 -> RW 0xC16958[7] = 159) when GlobalData + 0x13E and + 0x134 == 4
//     (RW 0x690F95 .. 0x690FAA) and SNOW (slot 8 -> 161) when + 0x13F and + 0x138 == 1 (RW 0x690FB5 .. 0x690FCD); the pair goes to
//     RW 0x68D607 Object::clearAndSetModelConditionFlags (RW 0x690FE2), which hands the object's + 0x10C set to the drawable
//     (RW 0x68D69A -> RW 0x679512, RW 0x68D688 reads Object + 0x84). So a condition the object carried before its drawable existed —
//     set in step 2 (the constructor, RW 0x6D168B) or step 3 (onCreate, RW 0x6D16E6), both before sendObjectCreated RW 0x628882 in step 4
//     (RW 0x693D0C) — is in the drawable's flags the moment it is made, not at the end of the frame.
//
// THIS PORT: the drawable is made from the CREATED client event (ClientEventRecorder::objectCreated, ClientEventRecorder::created).
// Before this lane that event carried only the map's time of day and weather (RW 0x690F8B .. 0x690FD0), so a condition set before the
// drawable existed had no path at all: the object's later bit changes are MODEL_CONDITION events, and the object only notifies for a
// change made while it already has client hooks (Object::setModelConditionState, RW 0x68D607). The event now carries the object's whole
// set like RW 0x690F02 does, and DrawableManager::created hands it to the draw modules at once — the creation flush.
//
// The launch-bone query of RW 0x6756A1 needs no flush here: DrawableLaunchBones answers it from the object's live condition bits (the set
// RW's flush copies into + 0x258), so the bone of a state change made in the same frame is the new state's. The barrel-count query
// RW 0x674673 has no port reader yet (ObjectWeapons' barrel count is a stub, S-320), and RW 0x674B1F is the per-frame gather RW 0x62E908,
// whose port snapshot reads the object's own state — kept by the last test case.
#include "doctest.h"

#include "CombatTestUtil.h"
#include "W3dSynth.h"

#include "GameClient/ClientEvents.h"
#include "GameClient/Drawable.h"
#include "GameClient/DrawableLaunchBones.h"
#include "GameClient/DrawableManager.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DModelDraw.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DDrawModules.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Object/Object.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <string>
#include <vector>

using namespace combattest;

namespace
{
// The condition the object carries before its drawable exists. MOVING is in the game's model condition table (Common/ModelConditionNames.inc)
// and no module of the synthetic object sets or clears it, so the only writer is the test's creation hook.
const char *const kCondition = "MOVING";

// A draw module with two states: the default one (no condition) and the one this condition selects (matching exactly, RW 0x4B4379's pass 1
// requires the query to contain every bit of the state's set). The models do not exist in the synthetic archive — the state selection is
// data and is what the device layer draws from.
const char kObjects[] =
	"Object Port316Unit\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 6\n"
	"  GeometryHeight = 16\n"
	"  Draw = W3DScriptedModelDraw ModuleTag_Draw\n"
	"    DefaultModelConditionState\n"
	"      Model = Port316Idle\n"
	"    End\n"
	"    ModelConditionState MOVING\n"
	"      Model = Port316Moving\n"
	"    End\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = NoArmor\n"
	"  End\n"
	"  Behavior = LifetimeUpdate ModuleTag_Life\n"
	"  End\n"
	"End\n"
	"Object Port316Plain\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 6\n"
	"  GeometryHeight = 16\n"
	"  Draw = W3DScriptedModelDraw ModuleTag_Draw\n"
	"    DefaultModelConditionState\n"
	"      Model = Port316Idle\n"
	"    End\n"
	"    ModelConditionState MOVING\n"
	"      Model = Port316Moving\n"
	"    End\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = NoArmor\n"
	"  End\n"
	"  Behavior = LifetimeUpdate ModuleTag_Life\n"
	"  End\n"
	"End\n";

struct Port316World : CombatWorld
{
	w3dsynth::MemoryFileSource source;
	WW3DAssetManager assets;
	Port316World()
		: CombatWorld("")
		, assets(source)
	{
		// the draw module data classes first (W3DScriptedModelDraw is not part of the combat fixture), then the objects that use them
		W3DDrawModules::registerTypedDrawModuleData(w.modules);
		const std::string err = w.load(kObjects, INI_LOAD_OVERWRITE, "port316.ini");
		REQUIRE_MESSAGE(err.empty(), err);
	}
};

struct HooksGuard
{
	GameLogic &logic;
	~HooksGuard() { logic.setClientHooks(nullptr); }
};

size_t countEvents(const std::vector<ClientEvent> &events, ClientEvent::Kind kind, ObjectID object)
{
	size_t n = 0;
	for (const ClientEvent &e : events)
	{
		n += e.kind == kind && e.object == object ? 1u : 0u;
	}
	return n;
}

// the model state the drawable's flags select right now (the first draw entry that has draw data)
const ModelConditionInfo *drawnState(const Drawable &d)
{
	for (const DrawEntry &e : d.entries())
	{
		const W3DModelDrawModuleData *data = dynamic_cast<const W3DModelDrawModuleData *>(e.data);
		if (data)
		{
			return data->findBestInfo(d.getModelConditionFlags());
		}
	}
	return nullptr;
}
} // namespace

// RW 0x690F02: the object's whole condition set is in the drawable at bind. The condition is set on the object before its drawable exists
// (the object's constructor / onCreate, RW 0x6D168B steps 2 and 3 — here the module's onObjectCreated, which runs inside Object's
// constructor, before runCreateModules step 3 and initObject step 4), so no MODEL_CONDITION event can exist for it: only the creation
// transfer RW 0x690F02 carries it. Without it the drawable draws the default state for the whole life of that condition.
TEST_CASE("PORT-316 creation: a condition the object carried before its drawable reaches the draw modules at bind (RW 0x690F02)")
{
	Port316World w;
	auto script = w.bind("LifetimeUpdate");
	const int bit = CombatNames::modelCondition(kCondition); // throws when the name is not in the game's table
	script->onObjectCreatedHook = [bit](logictest::ScriptModule &m) { m.getObject()->setModelConditionState(bit, true); };

	ClientEventRecorder events(*w.logic);
	w.logic->setClientHooks(&events);
	HooksGuard unbind{ *w.logic };
	DrawableManager dm(w.assets, *w.logic);

	Object *o = w.unit("Port316Unit", 'A', 400.0f, 400.0f);
	REQUIRE(o != nullptr);
	REQUIRE(o->testModelCondition(bit)); // the object carries it, its drawable does not exist yet
	const std::vector<ClientEvent> taken = events.take();

	// the creation event itself carries the set (RW 0x690F02's buffers), and there is no flush event: the flags arrive with the creation
	const size_t created = countEvents(taken, ClientEvent::CREATED, o->getID());
	REQUIRE(created == 1);
	CHECK(countEvents(taken, ClientEvent::MODEL_FLUSH, o->getID()) == 0);
	CHECK(countEvents(taken, ClientEvent::MODEL_CONDITION, o->getID()) == 0);

	dm.applyEvents(taken);
	Drawable *d = dm.findByObject(o->getID());
	REQUIRE(d != nullptr);
	// the drawable's flags — what the device layer selects the model state with — are the object's set on its first frame
	CHECK(d->getModelConditionFlags().test(bit));
	CHECK_FALSE(d->modelConditionsDirty());
	const ModelConditionInfo *state = drawnState(*d);
	REQUIRE(state != nullptr);
	CHECK(state->modelName() == "Port316Moving");
}

// the same condition set AFTER the drawable exists keeps the original path (RW 0x68D607 -> RW 0x679512 stores and marks + 0x443; the
// modules take it at the next flush, RW 0x6759C4 at the end of the frame): the creation transfer must not turn every change into an
// immediate one, and an object without conditions must not put any into its drawable.
TEST_CASE("PORT-316 creation: a later change still waits for the flush, and an object without conditions makes none (RW 0x679512 / 0x6759C4)")
{
	Port316World w;
	const int bit = CombatNames::modelCondition(kCondition); // throws when the name is not in the game's table

	ClientEventRecorder events(*w.logic);
	w.logic->setClientHooks(&events);
	HooksGuard unbind{ *w.logic };
	DrawableManager dm(w.assets, *w.logic);

	Object *o = w.unit("Port316Plain", 'A', 400.0f, 400.0f); // nothing set on it at creation
	REQUIRE(o != nullptr);
	std::vector<ClientEvent> taken = events.take();
	dm.applyEvents(taken);
	Drawable *d = dm.findByObject(o->getID());
	REQUIRE(d != nullptr);
	CHECK_FALSE(d->getModelConditionFlags().test(bit)); // nothing of its own reached it
	CHECK(countEvents(taken, ClientEvent::MODEL_FLUSH, o->getID()) == 0);

	// a change made now is stored and reaches the modules only at the end-of-frame flush
	o->setModelConditionState(bit, true);
	taken = events.take(false); // no end of frame: what the logic caused so far
	dm.applyEvents(taken);
	CHECK(d->getModelConditionFlags().test(bit)); // the set is the object's at once (RW 0x679512 stores it)
	CHECK(d->modelConditionsDirty());                     // ... and the draw modules have not been told yet
	CHECK(countEvents(taken, ClientEvent::MODEL_FLUSH, o->getID()) == 0);

	taken = events.take(); // the end of the logic frame, RW 0x6759C4
	REQUIRE(countEvents(taken, ClientEvent::MODEL_FLUSH, o->getID()) == 1);
	dm.applyEvents(taken);
	CHECK_FALSE(d->modelConditionsDirty());
	const ModelConditionInfo *state = drawnState(*d);
	REQUIRE(state != nullptr);
	CHECK(state->modelName() == "Port316Moving");
}

// RW 0x6756A1: the launch-bone query flushes + 0x258 and hands that very set to the draw module, so a state change made in the same frame
// is the new state's bone. The port's ProjectileLaunchOffsets answer reads the object's live bits — the same set — with no flush needed;
// this holds that, and the placement bits the drawable starts with.
TEST_CASE("PORT-316 bone queries: the launch flags are the object's live set after a mid-frame change (RW 0x6756A1 -> + 0x258)")
{
	Port316World w;
	const int bit = CombatNames::modelCondition(kCondition); // throws when the name is not in the game's table
	Object *o = w.unit("Port316Unit", 'A', 400.0f, 400.0f);
	REQUIRE(o != nullptr);
	const ModelConditionFlags before = DrawableLaunchBones::launchFlags(*o);
	CHECK_FALSE(before.test(bit));
	// the change the drawable has not been flushed with yet (no logic frame ran)
	o->setModelConditionState(bit, true);
	const ModelConditionFlags after = DrawableLaunchBones::launchFlags(*o);
	CHECK(after.test(bit));
	// a bit the object clears is out of the next query as well
	o->setModelConditionState(bit, false);
	CHECK_FALSE(DrawableLaunchBones::launchFlags(*o).test(bit));
}
