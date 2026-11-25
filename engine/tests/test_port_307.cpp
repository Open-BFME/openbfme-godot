// OpenBFME unit tests, PORT-307: the own functions of GateOpenAndCloseBehavior and WallHubBehavior that the function map found uncited.
// GPL-3.0. Synthetic INI and synthetic objects only: no retail data, no models.
//
// The behaviour this file pins is the gate's disabled-types mask:
//
//   GateOpenAndCloseBehavior's update interface (RW 0xC667E0, the secondary vtable the constructor RW 0x89C04B installs at +0x14) overrides slot 1,
//   `getDisabledTypesToProcess`, with RW 0x654FD1: memset 4 bytes (RW 0xA3CF28), then `or byte ptr [buf + 1], 8`, then the buffer out. The 4-byte mask is a
//   BitFlags<12>; byte + 1 bit 3 is bit 11, DISABLED_USER_FROZEN of the name table RW 0xDAD904. The base class's slot 1 is the empty mask RW 0x8B3313
//   (`*out = 0`), and the logic's scheduler (RW 0x62EA46 .. 0x62EA7A: the object's mask, its any() RW 0x9325B4, then the module's slot 1 through the update
//   interface, the `test` RW 0x6252E4) runs an update only while the object's disabled mask is empty OR shares a bit with this one. So a frozen gate kept
//   opening and closing in RotWK, and a port without the override drops the update instead: an opening gate stays half open for ever.
//
//   WallHubBehavior's own secondary interface (RW 0xC565A8) has NO disabled-mask slot: slot 10 is RW 0x855E38, a static resetter of the Options byte
//   0xDAE264 (`mov dword [0xDAE264], 0xFFFFFE`), and its update is UPDATE_SLEEP_FOREVER (RW 0x88B211). The test below pins that absence as the neighbour of
//   the gate's, so a later change cannot give the hub a mask retail never had.
//
// Each expectation comes from the disassembly and the name table, never from the port's code.

#include "doctest.h"

#include "CombatTestUtil.h"

#include "GameLogic/Module/GateModules.h"
#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTypes.h"
#include "GameLogic/WallSpan.h"

namespace
{
// TestWallGate307: a closed gate with a one second reset time, no proxy, so onObjectCreated finds nothing to link.
// TestHub307: a WALL_HUB with a WallHubBehavior (the module the mask question is about).
const char kPort307Objects[] =
	"Object TestWallGate307\n"
	"  KindOf = STRUCTURE SELECTABLE IMMOBILE WALL_GATE\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 15\n"
	"  GeometryMinorRadius = 5\n"
	"  GeometryHeight = 20\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 2000\n"
	"  End\n"
	"  Behavior = GateOpenAndCloseBehavior ModuleTag_Gate\n"
	"    OpenByDefault = No\n"
	"    ResetTimeInMilliseconds = 1000\n"
	"    PercentOpenForPathing = 50\n"
	"  End\n"
	"End\n"
	"Object TestHub307\n"
	"  KindOf = STRUCTURE IMMOBILE WALL_HUB\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 10\n"
	"  GeometryMinorRadius = 10\n"
	"  GeometryHeight = 20\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 1000\n"
	"  End\n"
	"  Behavior = WallHubBehavior ModuleTag_Hub\n"
	"    SegmentTemplateName = TestHub307\n"
	"    HubCapTemplateName = TestHub307\n"
	"    DefaultSegmentTemplateName = TestHub307\n"
	"    BuilderRadius = 20.0\n"
	"  End\n"
	"End\n";

struct Port307World : combattest::CombatWorld
{
	Port307World()
	{
		GateModules::registerAll(w.modules);
		WallSpan::registerModules(w.modules);
		const std::string error = w.load(kPort307Objects, INI_LOAD_OVERWRITE, "port307.ini");
		REQUIRE_MESSAGE(error.empty(), error);
	}
	GateOpenAndCloseBehavior *gate(Object *o)
	{
		return dynamic_cast<GateOpenAndCloseBehavior *>(o->findModule("GateOpenAndCloseBehavior"));
	}
};
} // namespace

// RW 0x654FD1 vs the base RW 0x8B3313: the gate answers DISABLED_USER_FROZEN, not the empty mask
TEST_CASE("PORT-307 GateOpenAndCloseBehavior: the update interface's disabled mask is DISABLED_USER_FROZEN, not the base's empty mask")
{
	Port307World w;
	Object *obj = w.spawn("TestWallGate307", 300.0f, 300.0f, 0.0f, w.teamA());
	REQUIRE(obj != nullptr);
	GateOpenAndCloseBehavior *gate = w.gate(obj);
	REQUIRE(gate != nullptr);

	// RW 0x654FD1: memset(0) then `or byte [buf + 1], 8` -> bit 11
	CHECK(gate->getDisabledTypesToProcess() == (DisabledMaskType)(1u << DISABLED_USER_FROZEN));
	// the name table RW 0xDAD904 puts it at 11; the mask is one bit and no other
	CHECK(DISABLED_USER_FROZEN == 11);
	CHECK(gate->getDisabledTypesToProcess() != DISABLEDMASK_NONE); // the base's mask (RW 0x8B3313), what the port answered
	CHECK((gate->getDisabledTypesToProcess() & (1u << DISABLED_HELD)) == 0); // a held gate is not updated
}

// The neighbour of the gate's mask: WallHubBehavior keeps UpdateModule's empty mask (RW 0x8B3313). Its secondary interface RW 0xC565A8 has a slot 10
// that looks like a mask writer (RW 0x855E38) but writes the STATIC Options byte 0xDAE264, not a per-object answer.
TEST_CASE("PORT-307 WallHubBehavior answers UpdateModule's empty disabled mask: RW 0x855E38 is a static resetter, not the hub's slot")
{
	Port307World w;
	Object *obj = w.spawn("TestHub307", 500.0f, 500.0f, 0.0f, w.teamA());
	REQUIRE(obj != nullptr);
	WallHubBehavior *hub = WallHubBehavior::find(*obj, 0);
	REQUIRE(hub != nullptr);
	CHECK(hub->getDisabledTypesToProcess() == DISABLEDMASK_NONE);
	// and its update never runs: UPDATE_SLEEP_FOREVER (RW 0x88B211), so the mask has no consequence either way
	CHECK(hub->update() == UPDATE_SLEEP_FOREVER);
	// the module data the hub was made from is the INI's (RW 0x855E43 parses the table RW 0xC56700)
	REQUIRE(hub->data() != nullptr);
	// the INI's own literal, not a computed value: BuilderRadius = 20.0 parses to exactly 20.0f
	CHECK(hub->data()->m_builderRadius == 20.0f);
	CHECK(hub->data()->m_maxBuildoutDistance == 54321.0f); // the unset default (RW 0xC565A4)
}

// The scheduler's own test (RW 0x62EA46 .. 0x62EA7A): a frozen gate still runs its update, so an open order reaches OPEN while frozen.
TEST_CASE("PORT-307 the logic keeps updating a frozen gate, so its open order completes while frozen")
{
	Port307World w;
	Object *obj = w.spawn("TestWallGate307", 300.0f, 300.0f, 0.0f, w.teamA());
	REQUIRE(obj != nullptr);
	GateOpenAndCloseBehavior *gate = w.gate(obj);
	REQUIRE(gate != nullptr);
	REQUIRE(gate->isSettled());
	REQUIRE_FALSE(gate->isOpen());

	// RW 0x692432(11): DISABLED_USER_FROZEN for a hundred frames
	obj->setDisabled(DISABLED_USER_FROZEN, w.logic->getFrame() + 100);
	REQUIRE(obj->isDisabledByType(DISABLED_USER_FROZEN));

	gate->open(); // the toggle the command button and MSG_OPEN_GATE reach
	REQUIRE_FALSE(gate->isSettled());
	CHECK(gate->percent() == 0.0f);
	w.frames(3);
	// the update ran under the freeze: the percent advanced off 0 (RW 0x89CE33; the reset time is 1000 ms = 100 frames, so it is not 0 any more)
	// tolerance: the percent is frame / reset * 100 at the x87's PC24 precision, so only its class is pinned here (0 before the update, not 0 after)
	CHECK(gate->percent() > 0.0f);
	w.frames(90);
	CHECK(gate->state() == GateOpenAndCloseBehavior::OPEN);
	CHECK(gate->percent() == 100.0f);
}

TEST_CASE("PORT-307 the logic skips a gate disabled by another type, as the empty base mask asks")
{
	Port307World w;
	Object *obj = w.spawn("TestWallGate307", 300.0f, 300.0f, 0.0f, w.teamA());
	REQUIRE(obj != nullptr);
	GateOpenAndCloseBehavior *gate = w.gate(obj);
	REQUIRE(gate != nullptr);
	// DISABLED_HELD (bit 3) is not in the gate's mask: the scheduler skips its update
	obj->setDisabled(DISABLED_HELD, w.logic->getFrame() + 100);
	gate->open();
	REQUIRE_FALSE(gate->isSettled());
	w.frames(3);
	CHECK(gate->percent() == 0.0f); // nothing ran
	obj->clearDisabled(DISABLED_HELD);
	w.frames(3);
	// and it runs again once the type is gone
	// tolerance: the first frame after the clear, the update's own percent for that frame; its class is what is pinned
	CHECK(gate->percent() > 0.0f);
}
