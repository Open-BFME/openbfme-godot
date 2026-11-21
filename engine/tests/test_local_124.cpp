// OpenBFME. GPL-3.0.
// LOCAL-124 (fork issue 808): FakePathfindPortalBehaviour::xfer post-load portal re-registration.
// Sources of expectations:
//   - RW 0x00864BFE (FakePathfindPortalBehaviour::xfer): saves/loads UpdateModule, two portal waypoint ids (+0x28/+0x2C),
//     m_created (+0x30), m_dead (+0x31), m_registered (+0x32).
//   - RW 0x00864CC8 .. 0x00864CDB: when m_registered is non-zero and Xfer::getXferMode() == XFER_LOAD, calls doRegister()
//     (RW 0x0086498E -> Pathfinder::addPortal RW 0x006EA42D on both waypoints).
//   - RW 0x00864CCC: when m_registered is zero (open gate), XFER_LOAD skips doRegister().

#include "doctest.h"

#include "CombatTestUtil.h"

#include "Common/INI.h"
#include "Common/Player.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/GateModules.h"
#include "GameLogic/Module/PortalModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SkirmishAI/SkirmishAIData.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"

#include <string>
#include <vector>

using namespace combattest;

namespace
{
const char kLocal124GateIni[] =
	"Object Local124PortalGate\n"
	"  KindOf = STRUCTURE IMMOBILE BLOCKING_GATE\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 30.0\n"
	"  GeometryMinorRadius = 10.0\n"
	"  GeometryHeight = 40.0\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 2000\n"
	"  End\n"
	"  Behavior = GateOpenAndCloseBehavior ModuleTag_Gate\n"
	"    ResetTimeInMilliseconds = 3000\n"
	"    OpenByDefault = No\n"
	"    PercentOpenForPathing = 50\n"
	"  End\n"
	"  Behavior = FakePathfindPortalBehaviour ModuleTag_FakePortal\n"
	"    AllowEnemies = No\n"
	"    AllowNonSkirmishAIUnits = No\n"
	"  End\n"
	"End\n";

void initSkirmishAI(CombatWorld &w, SkirmishAIStore &store, Player *aiPlayer)
{
	INIEnvironment env;
	store.registerBlocks(env.blocks);
	INI ini(env);
	const std::string text = "SkirmishAIData\nEnd\n";
	std::vector<std::uint8_t> bytes(text.begin(), text.end());
	ini.loadMemory("skirmishaidata.ini", bytes, INI_LOAD_OVERWRITE);

	w.logic->skirmishAI().setStore(&store);
	aiPlayer->setPlayerType(PLAYER_COMPUTER, true);
	aiPlayer->setSkirmishDifficulty(0);
	REQUIRE(w.logic->skirmishAI().newGame(true) == 1);
	REQUIRE(w.logic->skirmishAI().findAI(aiPlayer->getPlayerIndex()) != nullptr);
}
} // namespace

TEST_CASE("LOCAL-124 FakePathfindPortalBehaviour::xfer re-registers portal waypoints on load")
{
	// Source: RW 0x00864BFE (RW 0x00864CC8 .. 0x00864CDB -> RW 0x0086498E doRegister -> RW 0x006EA42D addPortal).
	CombatWorld w(kLocal124GateIni);
	SkirmishAIStore aiStore{ w.w.keys };
	Player *aiPlayer = w.players.findPlayerWithName("Bob");
	REQUIRE(aiPlayer != nullptr);
	initSkirmishAI(w, aiStore, aiPlayer);

	Object *gateObj = w.spawn("Local124PortalGate", 500.0f, 500.0f, 0.0f, aiPlayer->getDefaultTeam());
	REQUIRE(gateObj != nullptr);
	auto *fp = dynamic_cast<FakePathfindPortalBehaviour *>(gateObj->findModule("FakePathfindPortalBehaviour"));
	auto *gate = GateOpenAndCloseBehavior::findGate(*gateObj);
	REQUIRE(fp != nullptr);
	REQUIRE(gate != nullptr);
	REQUIRE_FALSE(gate->isOpen());

	w.frames(2);
	REQUIRE(fp->created());
	REQUIRE(fp->registered());

	Pathfinder &pf = w.ai->pathfinder();
	const int wp0 = fp->waypoint(0);
	const int wp1 = fp->waypoint(1);
	const PathfindWaypoint *a = pf.findWaypoint(wp0);
	const PathfindWaypoint *b = pf.findWaypoint(wp1);
	REQUIRE(a != nullptr);
	REQUIRE(b != nullptr);
	REQUIRE(Pathfinder::isPortalWaypoint(*a));
	REQUIRE(Pathfinder::isPortalWaypoint(*b));

	// Save while m_registered == true.
	XferSave save;
	fp->xfer(save);

	// Simulate fresh pathfinder cells after load before FakePathfindPortalBehaviour::xfer runs:
	// the waypoints exist, but neither cell holds a portal registration yet.
	pf.removePortal(wp0);
	pf.removePortal(wp1);
	REQUIRE_FALSE(Pathfinder::isPortalWaypoint(*pf.findWaypoint(wp0)));
	REQUIRE_FALSE(Pathfinder::isPortalWaypoint(*pf.findWaypoint(wp1)));

	// Load: RW 0x00864CC8 .. 0x00864CDB must call doRegister() (RW 0x0086498E) when m_registered is true.
	XferLoad load(save.data());
	fp->xfer(load);

	CHECK(fp->registered());
	CHECK(Pathfinder::isPortalWaypoint(*pf.findWaypoint(wp0)));
	CHECK(Pathfinder::isPortalWaypoint(*pf.findWaypoint(wp1)));
}

TEST_CASE("LOCAL-124 FakePathfindPortalBehaviour::xfer leaves open gate unregistered on load")
{
	// Source: RW 0x00864BFE (RW 0x00864CC8 .. 0x00864CCC: m_registered == 0 skips doRegister on XFER_LOAD).
	CombatWorld w(kLocal124GateIni);
	SkirmishAIStore aiStore{ w.w.keys };
	Player *aiPlayer = w.players.findPlayerWithName("Bob");
	REQUIRE(aiPlayer != nullptr);
	initSkirmishAI(w, aiStore, aiPlayer);

	Object *gateObj = w.spawn("Local124PortalGate", 500.0f, 500.0f, 0.0f, aiPlayer->getDefaultTeam());
	REQUIRE(gateObj != nullptr);
	auto *fp = dynamic_cast<FakePathfindPortalBehaviour *>(gateObj->findModule("FakePathfindPortalBehaviour"));
	auto *gate = GateOpenAndCloseBehavior::findGate(*gateObj);
	REQUIRE(fp != nullptr);
	REQUIRE(gate != nullptr);

	// Open the gate so FakePathfindPortalBehaviour unregisters the portal pair.
	gate->open();
	w.frames(120);
	REQUIRE(gate->isOpen());
	REQUIRE(fp->created());
	REQUIRE_FALSE(fp->registered());

	Pathfinder &pf = w.ai->pathfinder();
	const int wp0 = fp->waypoint(0);
	const int wp1 = fp->waypoint(1);
	REQUIRE_FALSE(Pathfinder::isPortalWaypoint(*pf.findWaypoint(wp0)));
	REQUIRE_FALSE(Pathfinder::isPortalWaypoint(*pf.findWaypoint(wp1)));

	XferSave save;
	fp->xfer(save);
	XferLoad load(save.data());
	fp->xfer(load);

	CHECK_FALSE(fp->registered());
	CHECK_FALSE(Pathfinder::isPortalWaypoint(*pf.findWaypoint(wp0)));
	CHECK_FALSE(Pathfinder::isPortalWaypoint(*pf.findWaypoint(wp1)));
}
