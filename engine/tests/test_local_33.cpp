// OpenBFME unit tests: LOCAL-33 castle gates and "man the walls" as retail (GateProxyBehavior, ManTheWallsSpecialPower, issue fork issue 283).
//
// Target facts:
//   * GateProxyBehavior:
//     - Module registration: RW 0x659500, name "GateProxyBehavior", mask 0x11 (MODULETYPE_BEHAVIOR).
//     - ModuleData: factory RW 0x653D6D, buildFieldParse RW 0x653CCA uses table RW 0xC07D08 (kGateOpenAndClose).
//     - Instance: factory RW 0x653D2C, ctor RW 0x64DFA5 calls base GateOpenAndCloseBehavior ctor RW 0x89C04B.
//     - Primary vtable RW 0xC055DC: slot 6 isOpen (RW 0x656747), slot 7 open (RW 0x65667C),
//       slot 8 close (RW 0x656719), slot 9 toggle (RW 0x656730), slot 10 isSettled (RW 0x656760),
//       slots 11/12 percent/float getters (RW 0x656779 / RW 0x6567A1).
//     - Target resolution helper RW 0x656693 (DONOR BFME2 0x00256996) looks up m_linkedObjectId (+0x24).
//     - Link setup in GateOpenAndCloseBehavior::onObjectCreated RW 0x89CB19 (DONOR BFME2 0x0049936A):
//       gate->m_linkedObjectId = proxy->getID(); proxy->m_linkedObjectId = gate->getID().
//     - CommandButton TOGGLE_GATE (RW 0x6974B1 / RW 0x6974D6) falls back to GateProxyBehavior.
//     - Weapon delivery OpenGateNugget (RW 0x911516 / RW 0x911569) falls back to GateProxyBehavior.
//     - MSG_OPEN_GATE (RW 0x77BFDB / RW 0x77C00D) and MSG_CLOSE_GATE (RW 0x77C0A7) fall back to GateProxyBehavior.
//   * ManTheWallsSpecialPower:
//     - Module registration: RW 0x65B809, name "ManTheWallsSpecialPower", mask 0x100 (MODULETYPE_BEHAVIOR).
//     - ModuleData: factory RW 0x65247C, uses SpecialPowerModuleData table RW 0xC64DB0.
//     - Instance: factory RW 0x6521DF, ctor RW 0x8CB3D1, primary vtable RW 0xC74CD8.
//     - Interface slot 10 doSpecialPower(unsigned options) at RW 0x8CBDBA (DONOR BFME2 0x004C799B):
//       1. Query partition manager for allied objects (relationship 4 = ALLIES) with KindOf 115 (TACTICAL_MARKER).
//          If none, return (RW 0x8CBDEE).
//       2. Query partition manager for allied objects (relationship 4 = ALLIES, not self, not dead) with KindOf 109 (HORDE).
//       3. Sort markers by distance to each horde (RW 0x8CBB53, DONOR BFME2 0x004C76AC);
//          pass count 0..9 assigns horde to first marker with members.size() <= count (balanced allocation).
//       4. For each marker with members, create AIGroup (RW 0x70042A), add units (RW 0x8CB4F1 -> 0x770917),
//          call groupMoveToPosition (RW 0x774897) with marker position and formation -1, then destroy group.

#include "doctest.h"
#include "CombatTestUtil.h"

#include "Common/Player.h"
#include "Common/SpecialPower.h"
#include "Common/StateHash.h"
#include "GameClient/ControlBarCommands.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/Combat/WeaponDelivery.h"
#include "GameLogic/GameLogicDispatch.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Module/GateModules.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/ObjectCommandButton.h"
#include "GameLogic/SimMath.h"

using namespace combattest;

namespace {

const char kLocal33Ini[] =
	"SpecialPower SpecialAbilityManTheWallsTest\n"
	"  Enum = SPECIAL_MAN_THE_WALLS\n"
	"  ReloadTime = 0\n"
	"End\n"

	"Weapon ShakeFoundationTestWeapon\n"
	"  AttackRange = 50.0\n"
	"  DamageNugget\n"
	"    Damage = 1\n"
	"    Radius = 0.0\n"
	"    DamageType = MAGIC\n"
	"  End\n"
	"  OpenGateNugget\n"
	"  End\n"
	"End\n"

	"Object TestWallGate\n"
	"  KindOf = STRUCTURE SELECTABLE IMMOBILE\n"
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
	"    Proxy = GateProxyObject\n"
	"  End\n"
	"End\n"

	"Object TestWallProxy\n"
	"  KindOf = STRUCTURE SELECTABLE IMMOBILE\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 10\n"
	"  GeometryMinorRadius = 5\n"
	"  GeometryHeight = 20\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 2000\n"
	"  End\n"
	"  Behavior = GateProxyBehavior ModuleTag_Proxy\n"
	"  End\n"
	"End\n"

	"Object TestGreatKeep\n"
	"  KindOf = STRUCTURE SELECTABLE IMMOBILE\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 30\n"
	"  GeometryMinorRadius = 30\n"
	"  GeometryHeight = 40\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 5000\n"
	"  End\n"
	"  Behavior = ManTheWallsSpecialPower ModuleTag_ManTheWalls\n"
	"    SpecialPowerTemplate = SpecialAbilityManTheWallsTest\n"
	"  End\n"
	"End\n"

	"Object TestWallMarker\n"
	"  KindOf = TACTICAL_MARKER IMMOBILE\n"
	"  Geometry = SPHERE\n"
	"  GeometryMajorRadius = 5\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"End\n";

struct Local33Fixture
{
	SpecialPowerStore powers;
	SpecialPowerStore *savedPowers = TheSpecialPowerStore;
	CombatWorld world;

	Local33Fixture()
		: world("")
	{
		TheSpecialPowerStore = &powers;
		world.w.fx.env.blocks.registerBlock("SpecialPower", [](INI *ini) { SpecialPowerStore::parseSpecialPowerDefinitionGlobal(ini); });
		GateModules::registerAll(world.w.modules);
		SpecialPowerModules::registerAll(world.w.modules);
		std::string err = world.w.load(kLocal33Ini, INI_LOAD_OVERWRITE, "local33.ini");
		REQUIRE_MESSAGE(err.empty(), err);
	}

	~Local33Fixture()
	{
		TheSpecialPowerStore = savedPowers;
	}
};

} // namespace

TEST_CASE("LOCAL-33 gate proxy: links to gate via Proxy name on gate onObjectCreated (both creation orders)")
{
	Local33Fixture f;
	CombatWorld &world = f.world;

	SUBCASE("creation order: gate created first, then proxy")
	{
		Object *gate = world.spawn("TestWallGate", 100.0f, 100.0f, 0.0f, world.teamA());
		REQUIRE(gate);
		gate->setName("GateMainObject");

		Object *proxy = world.spawn("TestWallProxy", 110.0f, 100.0f, 0.0f, world.teamA());
		REQUIRE(proxy);
		proxy->setName("GateProxyObject");

		// Run proxy onObjectCreated to complete link
		proxy->findModule("GateProxyBehavior")->onObjectCreated();

		auto *gateModule = dynamic_cast<GateOpenAndCloseBehavior *>(gate->findModule("GateOpenAndCloseBehavior"));
		auto *proxyModule = dynamic_cast<GateProxyBehavior *>(proxy->findModule("GateProxyBehavior"));
		REQUIRE(gateModule);
		REQUIRE(proxyModule);

		CHECK(gateModule->linkedObjectID() == proxy->getID());
		CHECK(proxyModule->linkedObjectID() == gate->getID());
	}

	SUBCASE("creation order: proxy created first, then gate")
	{
		Object *proxy = world.spawn("TestWallProxy", 210.0f, 100.0f, 0.0f, world.teamA());
		REQUIRE(proxy);
		proxy->setName("GateProxyObject");

		Object *gate = world.spawn("TestWallGate", 200.0f, 100.0f, 0.0f, world.teamA());
		REQUIRE(gate);
		gate->setName("GateMainObject");

		// Gate's onObjectCreated finds proxy by name
		gate->findModule("GateOpenAndCloseBehavior")->onObjectCreated();

		auto *gateModule = dynamic_cast<GateOpenAndCloseBehavior *>(gate->findModule("GateOpenAndCloseBehavior"));
		auto *proxyModule = dynamic_cast<GateProxyBehavior *>(proxy->findModule("GateProxyBehavior"));
		REQUIRE(gateModule);
		REQUIRE(proxyModule);

		CHECK(gateModule->linkedObjectID() == proxy->getID());
		CHECK(proxyModule->linkedObjectID() == gate->getID());
	}
}

TEST_CASE("LOCAL-33 gate proxy: state, percent, geometryState, isOpen, isSettled forward to linked gate")
{
	Local33Fixture f;
	CombatWorld &world = f.world;

	Object *proxy = world.spawn("TestWallProxy", 110.0f, 100.0f, 0.0f, world.teamA());
	proxy->setName("GateProxyObject");
	Object *gate = world.spawn("TestWallGate", 100.0f, 100.0f, 0.0f, world.teamA());
	gate->setName("GateMainObject");
	gate->findModule("GateOpenAndCloseBehavior")->onObjectCreated();

	auto *gateModule = dynamic_cast<GateOpenAndCloseBehavior *>(gate->findModule("GateOpenAndCloseBehavior"));
	auto *proxyModule = dynamic_cast<GateProxyBehavior *>(proxy->findModule("GateProxyBehavior"));
	REQUIRE(gateModule);
	REQUIRE(proxyModule);

	// Initially closed and settled
	CHECK_FALSE(proxyModule->isOpen());
	CHECK(proxyModule->isSettled());
	CHECK(proxyModule->state() == GateOpenAndCloseBehavior::CLOSED);
	CHECK(proxyModule->percent() == 100.0f);

	// Open via proxy interface
	proxyModule->open();
	CHECK(gateModule->state() == GateOpenAndCloseBehavior::OPENING);
	CHECK(proxyModule->state() == GateOpenAndCloseBehavior::OPENING);
	CHECK_FALSE(proxyModule->isOpen());
	CHECK_FALSE(proxyModule->isSettled());

	// Step frames until open
	for (int i = 0; i < 10 && !gateModule->isSettled(); ++i)
	{
		world.frames(1);
	}
	CHECK(gateModule->state() == GateOpenAndCloseBehavior::OPEN);
	CHECK(proxyModule->state() == GateOpenAndCloseBehavior::OPEN);
	CHECK(proxyModule->isOpen());
	CHECK(proxyModule->isSettled());
	CHECK(proxyModule->geometryState() == GateOpenAndCloseBehavior::GEOMETRY_OPEN);

	// Close via proxy interface
	proxyModule->close();
	CHECK(gateModule->state() == GateOpenAndCloseBehavior::CLOSING);
	CHECK(proxyModule->state() == GateOpenAndCloseBehavior::CLOSING);

	for (int i = 0; i < 10 && !gateModule->isSettled(); ++i)
	{
		world.frames(1);
	}
	CHECK(gateModule->state() == GateOpenAndCloseBehavior::CLOSED);
	CHECK(proxyModule->state() == GateOpenAndCloseBehavior::CLOSED);

	// Toggle via proxy interface
	proxyModule->toggle();
	CHECK(gateModule->state() == GateOpenAndCloseBehavior::OPENING);
	CHECK(proxyModule->state() == GateOpenAndCloseBehavior::OPENING);
}

TEST_CASE("LOCAL-33 gate proxy: Object::doCommandButton TOGGLE_GATE on proxy toggles linked gate")
{
	Local33Fixture f;
	CombatWorld &world = f.world;

	Object *proxy = world.spawn("TestWallProxy", 110.0f, 100.0f, 0.0f, world.teamA());
	proxy->setName("GateProxyObject");
	Object *gate = world.spawn("TestWallGate", 100.0f, 100.0f, 0.0f, world.teamA());
	gate->setName("GateMainObject");
	gate->findModule("GateOpenAndCloseBehavior")->onObjectCreated();

	auto *gateModule = dynamic_cast<GateOpenAndCloseBehavior *>(gate->findModule("GateOpenAndCloseBehavior"));
	REQUIRE(gateModule);
	CHECK(gateModule->state() == GateOpenAndCloseBehavior::CLOSED);

	CommandButton b;
	b.m_command = GUI_COMMAND_TOGGLE_GATE;

	// Click toggle on the proxy object
	proxy->doCommandButton(&b, CMD_FROM_PLAYER, false);
	CHECK(gateModule->state() == GateOpenAndCloseBehavior::OPENING);

	// Toggling an unsettled proxy does nothing
	proxy->doCommandButton(&b, CMD_FROM_PLAYER, false);
	CHECK(gateModule->state() == GateOpenAndCloseBehavior::OPENING);

	for (int i = 0; i < 10 && !gateModule->isSettled(); ++i)
	{
		world.frames(1);
	}
	CHECK(gateModule->state() == GateOpenAndCloseBehavior::OPEN);

	// Now settled open, toggle closes it
	proxy->doCommandButton(&b, CMD_FROM_PLAYER, false);
	CHECK(gateModule->state() == GateOpenAndCloseBehavior::CLOSING);
}

TEST_CASE("LOCAL-33 gate proxy: OpenGateNugget opens linked gate via proxy victim")
{
	Local33Fixture f;
	CombatWorld &world = f.world;

	Object *proxy = world.spawn("TestWallProxy", 110.0f, 100.0f, 0.0f, world.teamA());
	proxy->setName("GateProxyObject");
	Object *gate = world.spawn("TestWallGate", 100.0f, 100.0f, 0.0f, world.teamA());
	gate->setName("GateMainObject");
	gate->findModule("GateOpenAndCloseBehavior")->onObjectCreated();

	auto *gateModule = dynamic_cast<GateOpenAndCloseBehavior *>(gate->findModule("GateOpenAndCloseBehavior"));
	REQUIRE(gateModule);
	CHECK(gateModule->state() == GateOpenAndCloseBehavior::CLOSED);

	Object *attacker = world.spawn("Swordsman", 50.0f, 100.0f, 0.0f, world.teamB());
	const WeaponTemplate *w = TheWeaponStore ? TheWeaponStore->findWeaponTemplate("ShakeFoundationTestWeapon") : nullptr;
	REQUIRE(w);

	// Deliver nuggets targeting the proxy
	DeliverNuggets(*world.logic, attacker->getID(), *w, WeaponBonus{}, proxy, proxy->getPosition(), false, nullptr);

	// The linked gate is forced open (RW 0x911569)
	CHECK(gateModule->state() == GateOpenAndCloseBehavior::OPENING);
}

TEST_CASE("LOCAL-33 gate proxy: MSG_OPEN_GATE and MSG_CLOSE_GATE targeting proxy open and close linked gate")
{
	Local33Fixture f;
	CombatWorld &world = f.world;
	GateModules::registerHandlers(world.dispatcher);

	Object *proxy = world.spawn("TestWallProxy", 110.0f, 100.0f, 0.0f, world.teamA());
	proxy->setName("GateProxyObject");
	Object *gate = world.spawn("TestWallGate", 100.0f, 100.0f, 0.0f, world.teamA());
	gate->setName("GateMainObject");
	gate->findModule("GateOpenAndCloseBehavior")->onObjectCreated();

	auto *gateModule = dynamic_cast<GateOpenAndCloseBehavior *>(gate->findModule("GateOpenAndCloseBehavior"));
	REQUIRE(gateModule);
	CHECK(gateModule->state() == GateOpenAndCloseBehavior::CLOSED);

	// Send MSG_OPEN_GATE for proxy ID
	GameMessage msgOpen(MSG_OPEN_GATE, world.playerIndex("Alice"));
	msgOpen.appendObjectIDArgument(proxy->getID());
	world.dispatcher.dispatch(msgOpen);

	CHECK(gateModule->state() == GateOpenAndCloseBehavior::OPENING);

	for (int i = 0; i < 10 && !gateModule->isSettled(); ++i)
	{
		world.frames(1);
	}
	CHECK(gateModule->state() == GateOpenAndCloseBehavior::OPEN);

	// Send MSG_CLOSE_GATE for proxy ID
	GameMessage msgClose(MSG_CLOSE_GATE, world.playerIndex("Alice"));
	msgClose.appendObjectIDArgument(proxy->getID());
	world.dispatcher.dispatch(msgClose);

	CHECK(gateModule->state() == GateOpenAndCloseBehavior::CLOSING);
}

TEST_CASE("LOCAL-33 man the walls: sends allied hordes to nearest tactical markers via groupMoveToPosition")
{
	Local33Fixture f;
	CombatWorld &world = f.world;

	// Great keep at (0, 0)
	Object *keep = world.spawn("TestGreatKeep", 0.0f, 0.0f, 0.0f, world.teamA());
	REQUIRE(keep);

	// Two wall tactical markers
	Object *marker1 = world.spawn("TestWallMarker", 100.0f, 0.0f, 0.0f, world.teamA());
	Object *marker2 = world.spawn("TestWallMarker", 500.0f, 0.0f, 0.0f, world.teamA());
	REQUIRE(marker1);
	REQUIRE(marker2);

	// Three allied hordes (Alice)
	Object *horde1 = world.spawn("SwordHorde", 60.0f, 0.0f, 0.0f, world.teamA());
	Object *horde2 = world.spawn("SwordHorde", 80.0f, 0.0f, 0.0f, world.teamA());
	Object *horde3 = world.spawn("SwordHorde", 460.0f, 0.0f, 0.0f, world.teamA());
	REQUIRE(horde1);
	REQUIRE(horde2);
	REQUIRE(horde3);

	// Enemy horde (Bob) at (450, 50) - should NOT move
	Object *enemyHorde = world.spawn("SwordHorde", 450.0f, 50.0f, 0.0f, world.teamB());
	REQUIRE(enemyHorde);

	// Non-horde infantry unit (Alice) at (50, 50) - should NOT move
	Object *soloUnit = world.spawn("Swordsman", 50.0f, 50.0f, 0.0f, world.teamA());
	REQUIRE(soloUnit);

	// Run initial frame so modules settle
	world.frames(1);

	const Coord3D enemyPosBefore = *enemyHorde->getPosition();
	const Coord3D soloPosBefore = *soloUnit->getPosition();
	const Coord3D horde1PosBefore = *horde1->getPosition();
	const Coord3D horde3PosBefore = *horde3->getPosition();

	// Cast ManTheWalls
	auto *manTheWalls = dynamic_cast<ManTheWallsSpecialPower *>(keep->findModule("ManTheWallsSpecialPower"));
	REQUIRE(manTheWalls);

	manTheWalls->doSpecialPower(0);

	// Step a few frames to allow movement execution
	for (int i = 0; i < 5; ++i)
	{
		world.frames(1);
	}

	// Allied hordes must have moved towards markers
	CHECK(horde1->getPosition()->x > horde1PosBefore.x); // tolerance: movement distance depends on frames stepped and locomotor speed
	CHECK(horde3->getPosition()->x > horde3PosBefore.x); // tolerance: movement distance depends on frames stepped and locomotor speed

	// Enemy horde and non-horde unit did NOT receive movement orders
	CHECK(enemyHorde->getPosition()->x == enemyPosBefore.x);
	CHECK(enemyHorde->getPosition()->y == enemyPosBefore.y);
	CHECK(soloUnit->getPosition()->x == soloPosBefore.x);
	CHECK(soloUnit->getPosition()->y == soloPosBefore.y);
}

TEST_CASE("LOCAL-33 man the walls: no tactical markers on map exits cleanly without orders or errors")
{
	Local33Fixture f;
	CombatWorld &world = f.world;

	// Keep with NO wall markers present
	Object *keep = world.spawn("TestGreatKeep", 0.0f, 0.0f, 0.0f, world.teamA());
	Object *horde = world.spawn("SwordHorde", 60.0f, 0.0f, 0.0f, world.teamA());
	REQUIRE(keep);
	REQUIRE(horde);

	world.frames(1);
	const Coord3D posBefore = *horde->getPosition();

	auto *manTheWalls = dynamic_cast<ManTheWallsSpecialPower *>(keep->findModule("ManTheWallsSpecialPower"));
	REQUIRE(manTheWalls);

	// Should safely return without crash or error (RW 0x8CBDEE)
	manTheWalls->doSpecialPower(0);
	world.frames(1);

	CHECK(horde->getPosition()->x == posBefore.x);
	CHECK(horde->getPosition()->y == posBefore.y);
}

TEST_CASE("LOCAL-33 save4b and crc: GateProxyBehavior and ManTheWallsSpecialPower roundtrip cleanly")
{
	Local33Fixture f;
	CombatWorld &world = f.world;

	Object *proxy = world.spawn("TestWallProxy", 110.0f, 100.0f, 0.0f, world.teamA());
	Object *keep = world.spawn("TestGreatKeep", 0.0f, 0.0f, 0.0f, world.teamA());
	REQUIRE(proxy);
	REQUIRE(keep);

	auto *proxyModule = dynamic_cast<GateProxyBehavior *>(proxy->findModule("GateProxyBehavior"));
	auto *manTheWalls = dynamic_cast<ManTheWallsSpecialPower *>(keep->findModule("ManTheWallsSpecialPower"));
	REQUIRE(proxyModule);
	REQUIRE(manTheWalls);

	// CRC checks
	StateHasher h1;
	proxyModule->crc(h1);
	CHECK(h1.value() != 0u);

	StateHasher h2;
	manTheWalls->crc(h2);
	CHECK(h2.value() != 0u);
}
