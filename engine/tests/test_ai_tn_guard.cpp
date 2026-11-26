// OpenBFME unit tests. PORT-203 (fork issue 470): TunnelTracker nemesis, removeFromContain(exposeStealthUnits),
// TunnelContain ExitInterface (isExitBusy / exitObjectInAHurry), AITunnelNetworkGuardState (0x18) / AITNGuardMachine (5000..5005),
// aiGuardTunnelNetwork (AICMD 0x37), and TEAM_GUARD_IN_TUNNEL_NETWORK (326).
// Synthetic INI, no retail files. Expectations come from RotWK game.dat instructions cited at each case.

#include "doctest.h"
#include "CombatTestUtil.h"
#include "ScriptTestUtil.h"

#include "Common/GameState.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "GameLogic/AI/AIGarrisonStates.h"
#include "GameLogic/AI/AITNGuard.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/InvisibilityModules.h"
#include "GameLogic/Object/Contain/GarrisonContainRuntime.h"
#include "GameLogic/Object/Contain/OpenContainRuntime.h"
#include "GameLogic/Object/Contain/TransportContainRuntime.h"
#include "GameLogic/Object/Contain/TunnelContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/ScriptEngine/ScriptActions.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"
#include "GameLogic/System/InvisibilityManager.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using namespace combattest;
using namespace scripttest;

namespace
{
const char kPort203Objects[] =
	"Object Port203StealthRider\n"
	"  TransportSlotCount = 1\n"
	"  KindOf = INFANTRY SELECTABLE STEALTH_GARRISON\n"
	"  VisionRange = 200\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 5\n"
	"  GeometryMinorRadius = 5\n"
	"  GeometryHeight = 20\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = PlainArmor\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = StealthUpdate ModuleTag_Stealth\n"
	"    StealthDelay = 200\n"
	"    InnateStealth = Yes\n"
	"  End\n"
	"End\n"
	"Object Port203StealthHero\n"
	"  TransportSlotCount = 1\n"
	"  KindOf = INFANTRY HERO SELECTABLE STEALTH_GARRISON\n"
	"  VisionRange = 200\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 5\n"
	"  GeometryMinorRadius = 5\n"
	"  GeometryHeight = 20\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = PlainArmor\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = StealthUpdate ModuleTag_Stealth\n"
	"    StealthDelay = 200\n"
	"    InnateStealth = Yes\n"
	"  End\n"
	"  Behavior = SlowDeathBehavior ModuleTag_SlowDeath\n"
	"    DestructionDelay = 1000\n"
	"  End\n"
	"End\n"
	"Object Port203Transport\n"
	"  KindOf = STRUCTURE IMMOBILE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 20\n"
	"  GeometryHeight = 30\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 200\n"
	"  End\n"
	"  Behavior = TransportContain ModuleTag_Contain\n"
	"    ContainMax = 4\n"
	"    Slots = 4\n"
	"    AllowAlliesInside = Yes\n"
	"    AllowEnemiesInside = Yes\n"
	"    AllowNeutralInside = Yes\n"
	"    EjectPassengersOnDeath = Yes\n"
	"    PassengerFilter = ALL\n"
	"  End\n"
	"End\n"
	"Object Port203Garrison\n"
	"  KindOf = STRUCTURE IMMOBILE SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 20\n"
	"  GeometryHeight = 30\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 200\n"
	"  End\n"
	"  Behavior = GarrisonContain ModuleTag_Contain\n"
	"    ContainMax = 4\n"
	"    AllowAlliesInside = Yes\n"
	"    AllowEnemiesInside = Yes\n"
	"    AllowNeutralInside = Yes\n"
	"    EjectPassengersOnDeath = Yes\n"
	"    PassengerFilter = ALL\n"
	"  End\n"
	"End\n"
	"Object Port203HordeGarrison\n"
	"  KindOf = STRUCTURE IMMOBILE SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 20\n"
	"  GeometryHeight = 30\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 200\n"
	"  End\n"
	"  Behavior = HordeGarrisonContain ModuleTag_Contain\n"
	"    ContainMax = 4\n"
	"    AllowAlliesInside = Yes\n"
	"    AllowEnemiesInside = Yes\n"
	"    AllowNeutralInside = Yes\n"
	"    EjectPassengersOnDeath = Yes\n"
	"    PassengerFilter = ALL\n"
	"  End\n"
	"End\n"
	"Object Port203HordeTransport\n"
	"  KindOf = STRUCTURE IMMOBILE SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 20\n"
	"  GeometryHeight = 30\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 200\n"
	"  End\n"
	"  Behavior = HordeTransportContain ModuleTag_Contain\n"
	"    ContainMax = 4\n"
	"    Slots = 4\n"
	"    AllowAlliesInside = Yes\n"
	"    AllowEnemiesInside = Yes\n"
	"    AllowNeutralInside = Yes\n"
	"    EjectPassengersOnDeath = Yes\n"
	"    PassengerFilter = ALL\n"
	"  End\n"
	"End\n"
	"Object Port203Tunnel\n"
	"  KindOf = STRUCTURE IMMOBILE SELECTABLE MP_COUNT_FOR_VICTORY\n"
	"  VisionRange = 200\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 20\n"
	"  GeometryMinorRadius = 20\n"
	"  GeometryHeight = 30\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = NoArmor\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 1000\n"
	"  End\n"
	"  Behavior = TunnelContain ModuleTag_Contain\n"
	"    ObjectStatusOfContained = UNSELECTABLE ENCLOSED\n"
	"    ContainMax = 5\n"
	"    PassengerFilter = ALL\n"
	"    AllowEnemiesInside = No\n"
	"    AllowAlliesInside = No\n"
	"    AllowNeutralInside = No\n"
	"    AllowOwnPlayerInsideOverride = Yes\n"
	"    NumberOfExitPaths = 0\n"
	"    EntryPosition = X:25.0 Y:0.0 Z:0.0\n"
	"    EntryOffset = X:0.0 Y:0.0 Z:0.0\n"
	"    ExitOffset = X:25.0 Y:0.0 Z:0.0\n"
	"    EnterSound = NoSound\n"
	"    ExitSound = NoSound\n"
	"    TimeForFullHeal = 1000\n"
	"  End\n"
	"End\n"
	"Object Port203TunnelDelayPaths\n"
	"  KindOf = STRUCTURE IMMOBILE SELECTABLE\n"
	"  VisionRange = 200\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 20\n"
	"  GeometryMinorRadius = 20\n"
	"  GeometryHeight = 30\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 1000\n"
	"  End\n"
	"  Behavior = TunnelContain ModuleTag_Contain\n"
	"    ObjectStatusOfContained = UNSELECTABLE ENCLOSED\n"
	"    ContainMax = 5\n"
	"    PassengerFilter = ANY +INFANTRY\n"
	"    AllowOwnPlayerInsideOverride = Yes\n"
	"    NumberOfExitPaths = 1\n"
	"    DoorOpenTime = 1000\n"
	"    ExitDelay = 1000\n"
	"    EnterSound = NoSound\n"
	"    ExitSound = NoSound\n"
	"  End\n"
	"End\n"
	"Object Port203GuardSoldier\n"
	"  TransportSlotCount = 1\n"
	"  KindOf = INFANTRY SELECTABLE PRELOAD\n"
	"  VisionRange = 180\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 5\n"
	"  GeometryMinorRadius = 5\n"
	"  GeometryHeight = 20\n"
	"  WeaponSet\n"
	"    Conditions = None\n"
	"    Weapon = PRIMARY SwordWeapon\n"
	"  End\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = PlainArmor\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 500\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"    AutoAcquireEnemiesWhenIdle = Yes\n"
	"  End\n"
	"  Behavior = PhysicsBehavior ModuleTag_Physics\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 40\n"
	"  End\n"
	"End\n"
	"Object Port203Attacker\n"
	"  TransportSlotCount = 1\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  VisionRange = 180\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 5\n"
	"  GeometryMinorRadius = 5\n"
	"  GeometryHeight = 20\n"
	"  WeaponSet\n"
	"    Conditions = None\n"
	"    Weapon = PRIMARY SwordWeapon\n"
	"  End\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = NoArmor\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 20\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"    AutoAcquireEnemiesWhenIdle = No\n"
	"  End\n"
	"  Behavior = PhysicsBehavior ModuleTag_Physics\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 25\n"
	"  End\n"
	"End\n"
	"Object Port203Cavalry\n"
	"  KindOf = CAVALRY SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 5\n"
	"  GeometryHeight = 20\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"End\n"
	"Object Port203Aircraft\n"
	"  KindOf = AIRCRAFT SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 5\n"
	"  GeometryHeight = 20\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"End\n"
	"Object Port203Monster\n"
	"  KindOf = MONSTER SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 5\n"
	"  GeometryHeight = 20\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"End\n"
	"Object Port203Machine\n"
	"  KindOf = MACHINE SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 5\n"
	"  GeometryHeight = 20\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"End\n"
	"Object Port203Projectile\n"
	"  KindOf = PROJECTILE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 1\n"
	"  GeometryHeight = 1\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 10\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"    AutoAcquireEnemiesWhenIdle = No\n"
	"  End\n"
	"End\n"
	"Object Port203Horde\n"
	"  TransportSlotCount = 1\n"
	"  KindOf = HORDE MELEE_HORDE LARGE_RECTANGLE_PATHFIND SELECTABLE CAN_ATTACK\n"
	"  VisionRange = 200\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 30\n"
	"  GeometryMinorRadius = 45\n"
	"  GeometryHeight = 20\n"
	"  WeaponSet\n"
	"    Conditions = None\n"
	"    Weapon = PRIMARY SwordWeapon\n"
	"  End\n"
	"  Body = ImmortalBody ModuleTag_Body\n"
	"    MaxHealth = 1\n"
	"  End\n"
	"  Behavior = HordeAIUpdate ModuleTag_AI\n"
	"    AutoAcquireEnemiesWhenIdle = Yes\n"
	"  End\n"
	"  Behavior = HordeContain ModuleTag_Contain\n"
	"    RankInfo = RankNumber:1 UnitType:Swordsman Position:X:50 Y:0 Position:X:50 Y:20\n"
	"    InitialPayload = Swordsman 2\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = HordeLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 50\n"
	"  End\n"
	"End\n";

struct Port203World : CombatWorld
{
	Port203World() : CombatWorld(kPort203Objects)
	{
		logic->settings().maxTunnelCapacity = 5;
		logic->settings().guardChaseUnitsFrames = 100;
		logic->partition().setRegion(0.0f, 0.0f, 1000.0f, 1000.0f);
		ScriptEngine::SideScripts side;
		side.sideName = "";
		static ScriptList empty;
		side.lists.push_back(&empty);
		logic->scriptEngine().newGame({ side }, nullptr, nullptr, false);
	}
};

std::string qual(Team *t)
{
	return t->getControllingPlayer()->getPlayerName() + "/" + t->getName();
}

std::uint32_t readU32LE(const std::vector<std::uint8_t> &buf, size_t off)
{
	std::uint32_t v = 0;
	std::memcpy(&v, buf.data() + off, sizeof(v));
	return v;
}
} // namespace

TEST_CASE("PORT-203 openContainRemove exposeStealthUnits: OpenContain::onDelete exposes STEALTH_GARRISON while onDie does not (RW 0x86708F, RW 0x867120, RW 0x865EF5..0x865F22)")
{
	SUBCASE("onDelete calls removeFromContain(rider, true) and exposes STEALTH_GARRISON")
	{
		Port203World w;
		Object *transport = w.unit("Port203Transport", 'A', 400.0f, 400.0f);
		Object *stealth = w.unit("Port203StealthHero", 'B', 400.0f, 400.0f);
		const ObjectID stealthId = stealth->getID();
		transport->getContain()->addToContain(stealth);
		REQUIRE(stealth->getContainedBy() == transport);
		CHECK_FALSE(stealth->testStatus((unsigned)CombatNames::status("DETECTED")));
		const size_t entriesBefore = w.logic->invisibility().entryCount();

		const unsigned deleteFrame = w.logic->getFrame();
		w.logic->destroyObject(transport); // friend_onDestroy -> OpenContain::onDelete runs at deleteFrame

		Object *found = w.logic->findObjectByID(stealthId);
		REQUIRE(found != nullptr);
		CHECK(found->testStatus((unsigned)CombatNames::status("DETECTED")));
		CHECK(w.logic->invisibility().entryCount() == entriesBefore + 1u);
		StealthUpdate *su = StealthUpdate::of(*found);
		REQUIRE(su != nullptr);
		CHECK(su->detectionExpiresFrame() == deleteFrame + 1u);

		w.logic->runLogicFrame();
	}

	SUBCASE("onDie EjectPassengersOnDeath calls removeAllContained(false) and does NOT expose STEALTH_GARRISON")
	{
		Port203World w;
		Object *transport = w.unit("Port203Transport", 'A', 400.0f, 400.0f);
		Object *stealth = w.unit("Port203StealthRider", 'B', 400.0f, 400.0f);
		transport->getContain()->addToContain(stealth);
		REQUIRE(stealth->getContainedBy() == transport);
		const size_t entriesBefore = w.logic->invisibility().entryCount();

		transport->kill(DAMAGE_UNRESISTABLE, 0);
		w.logic->runLogicFrame();

		CHECK(stealth->getContainedBy() == nullptr);
		CHECK_FALSE(stealth->testStatus((unsigned)CombatNames::status("DETECTED")));
		CHECK(w.logic->invisibility().entryCount() == entriesBefore);
	}
}

TEST_CASE("PORT-203 TunnelContain::update records enemy attacker as TunnelTracker nemesis and serializes it in obfme:TunnelTracker (RW 0x8812B0, RW 0x8FA702, RW 0x8FA569)")
{
	Port203World w2;
	Object *bobTunnel = w2.unit("Port203Tunnel", 'B', 300.0f, 300.0f);
	bobTunnel->friend_onBuildComplete();
	Object *aliceAttacker = w2.unit("Port203Attacker", 'A', 320.0f, 300.0f);
	w2.logic->runLogicFrame();
	const unsigned hitFrame = w2.logic->getFrame();
	DamageInfo dmg2;
	dmg2.m_input.m_sourceID = aliceAttacker->getID();
	dmg2.m_input.m_damageType = DAMAGE_SLASH;
	dmg2.m_input.m_amount = 10.0f;
	bobTunnel->attemptDamage(dmg2);
	w2.logic->runLogicFrame();

	std::vector<std::uint8_t> bobBytes;
	w2.logic->gameState().saveBlocks({ "obfme:TunnelTracker" }, bobBytes);
	const size_t nemesisAt = bobBytes.size() - 7 - 8;
	CHECK(readU32LE(bobBytes, nemesisAt) == aliceAttacker->getID());
	CHECK(readU32LE(bobBytes, nemesisAt + 4) == hitFrame + 1u);
}

TEST_CASE("PORT-203 TEAM_GUARD_IN_TUNNEL_NETWORK (326, RW 0x7CE522 / 0x7CA692 -> AICMD 0x37 -> state 0x18 -> AITNGuardMachine 5000..5005)")
{
	Port203World w;
	Object *t1 = w.unit("Port203Tunnel", 'A', 200.0f, 500.0f);
	Object *t2 = w.unit("Port203Tunnel", 'A', 700.0f, 500.0f);
	t1->friend_onBuildComplete();
	t2->friend_onBuildComplete();
	Object *guard = w.unit("Port203GuardSoldier", 'A', 240.0f, 500.0f);
	Object *enemy = w.unit("Port203Attacker", 'B', 740.0f, 500.0f);
	w.logic->runLogicFrame();

	ScriptEngine &se = w.logic->scriptEngine();
	ScriptActions::execute(se, act("TEAM_GUARD_IN_TUNNEL_NETWORK", { sp(3, qual(w.teamA())) }));
	CHECK(se.stats().unportedActions.empty());
	REQUIRE(guard->getAIUpdateInterface()->currentStateId() == 0x18u);

	// Guard walks into t1 and waits inside.
	w.runUntil([&]() { return guard->getContainedBy() == t1; }, 40);
	REQUIRE(guard->getContainedBy() == t1);

	// Enemy hits t2 across the map -> guard exits out of t2 (closest tunnel to enemy), kills enemy, then returns to tunnel network.
	DamageInfo dmg;
	dmg.m_input.m_sourceID = enemy->getID();
	dmg.m_input.m_damageType = DAMAGE_SLASH;
	dmg.m_input.m_amount = 10.0f;
	t2->attemptDamage(dmg);

	w.runUntil([&]() { return guard->getContainedBy() == nullptr; }, 20);
	REQUIRE(guard->getContainedBy() == nullptr);
	const bool exitedNearT2 = std::abs(guard->getPosition()->x - t2->getPosition()->x) < 100.0f;
	CHECK(exitedNearT2);

	w.runUntil([&]() { return enemy->isEffectivelyDead(); }, 60);
	REQUIRE(enemy->isEffectivelyDead());

	// Wait for nemesis 4s timer + return to tunnel network (t2).
	w.runUntil([&]() { return guard->getContainedBy() != nullptr; }, 80);
	CHECK(guard->getContainedBy() == t2);
}

TEST_CASE("PORT-203 removeFromContain exposeStealthUnits across contain hierarchy (RW 0x865EF5..0x865F22, RW 0x87CE3E, RW 0x87A369)")
{
	SUBCASE("TransportContain and GarrisonContain forward exposeStealthUnits true vs false")
	{
		Port203World w;
		Object *transport = w.unit("Port203Transport", 'A', 300.0f, 300.0f);
		Object *garrison = w.unit("Port203Garrison", 'A', 500.0f, 300.0f);
		Object *r1 = w.unit("Port203StealthRider", 'B', 300.0f, 300.0f);
		Object *r2 = w.unit("Port203StealthRider", 'B', 300.0f, 300.0f);
		Object *r3 = w.unit("Port203StealthRider", 'B', 500.0f, 300.0f);
		Object *r4 = w.unit("Port203StealthRider", 'B', 500.0f, 300.0f);
		w.logic->runLogicFrame();

		transport->getContain()->addToContain(r1);
		transport->getContain()->addToContain(r2);
		transport->getContain()->removeFromContain(r1, false);
		transport->getContain()->removeFromContain(r2, true);
		CHECK_FALSE(r1->testStatus((unsigned)CombatNames::status("DETECTED")));
		CHECK(r2->testStatus((unsigned)CombatNames::status("DETECTED")));

		garrison->getContain()->addToContain(r3);
		garrison->getContain()->addToContain(r4);
		garrison->getContain()->removeFromContain(r3);
		garrison->getContain()->removeFromContain(r4, true);
		CHECK_FALSE(r3->testStatus((unsigned)CombatNames::status("DETECTED")));
		CHECK(r4->testStatus((unsigned)CombatNames::status("DETECTED")));
	}

	SUBCASE("HordeGarrisonContain and HordeTransportContain match game.dat push 0 (RW 0x87CE3E / RW 0x87A369 / 0x87A60F)")
	{
		Port203World w;
		Object *hg = w.unit("Port203HordeGarrison", 'A', 300.0f, 300.0f);
		Object *ht = w.unit("Port203HordeTransport", 'A', 500.0f, 300.0f);
		Object *r1 = w.unit("Port203StealthRider", 'B', 300.0f, 300.0f);
		Object *r2 = w.unit("Port203StealthRider", 'B', 500.0f, 300.0f);
		w.logic->runLogicFrame();

		hg->getContain()->addToContain(r1);
		hg->getContain()->removeFromContain(r1, true);
		CHECK(r1->getContainedBy() == nullptr);
		CHECK_FALSE(r1->testStatus((unsigned)CombatNames::status("DETECTED")));

		ht->getContain()->addToContain(r2);
		ht->getContain()->removeFromContain(r2, true);
		CHECK(r2->getContainedBy() == nullptr);
		CHECK_FALSE(r2->testStatus((unsigned)CombatNames::status("DETECTED")));
	}
}

TEST_CASE("PORT-203 TunnelContain ExitInterface: isExitBusy (RW 0x87CE66) and exitObjectInAHurry (RW 0x868759)")
{
	Port203World w;
	Object *tunnel = w.unit("Port203TunnelDelayPaths", 'A', 300.0f, 300.0f);
	tunnel->friend_onBuildComplete();
	Object *guard = w.unit("Port203GuardSoldier", 'A', 300.0f, 300.0f);
	w.logic->runLogicFrame();

	ContainModuleInterface *c = tunnel->getContain();
	REQUIRE(c != nullptr);
	CHECK_FALSE(c->isExitBusy());
	REQUIRE(c->addToContain(guard));
	REQUIRE(guard->getContainedBy() == tunnel);
	CHECK((guard->getDisabledMask() & (1u << 3)) != 0u);

	c->exitObjectInAHurry(guard);
	CHECK(guard->getContainedBy() == nullptr);
	CHECK((guard->getDisabledMask() & (1u << 3)) == 0u);
	CHECK(c->isExitBusy()); // ExitDelay = 1000ms = 5 frames
	CHECK(tunnel->testModelCondition(CombatNames::modelCondition("DOOR_1_OPENING")));
	CHECK(guard->getAIUpdateInterface()->currentStateId() == (unsigned)AI_FOLLOW_EXITPRODUCTION_PATH);

	for (int f = 0; f < 4; ++f)
	{
		w.logic->runLogicFrame();
		CHECK(c->isExitBusy());
	}
	w.logic->runLogicFrame();
	CHECK_FALSE(c->isExitBusy());
}

TEST_CASE("PORT-203 TunnelTracker nemesis rules: KindOf filter (RW 0x8FA702), 4s expiry, dead/stealth clear (RW 0x8FA451), and snapshot round-trip (RW 0x8FA569)")
{
	Port203World w;
	Object *tunnel = w.unit("Port203Tunnel", 'A', 300.0f, 300.0f);
	tunnel->friend_onBuildComplete();
	Object *cav = w.unit("Port203Cavalry", 'B', 320.0f, 300.0f);
	Object *air = w.unit("Port203Aircraft", 'B', 340.0f, 300.0f);
	Object *mon = w.unit("Port203Monster", 'B', 360.0f, 300.0f);
	Object *mach = w.unit("Port203Machine", 'B', 380.0f, 300.0f);
	TunnelTracker &tt = w.playerOf('A')->tunnelTracker();

	// MONSTER and MACHINE are rejected by RW 0x8FA719..0x8FA726.
	tt.updateNemesis(mon, *w.logic);
	CHECK(tt.getCurNemesis(*w.logic) == nullptr);
	tt.updateNemesis(mach, *w.logic);
	CHECK(tt.getCurNemesis(*w.logic) == nullptr);

	// CAVALRY sets nemesis; another valid target does not overwrite while active, same target refreshes timestamp.
	tt.updateNemesis(cav, *w.logic);
	CHECK(tt.getCurNemesis(*w.logic) == cav);
	tt.updateNemesis(air, *w.logic);
	CHECK(tt.getCurNemesis(*w.logic) == cav);

	for (int i = 0; i < 19; ++i)
	{
		w.logic->runLogicFrame();
	}
	CHECK(tt.getCurNemesis(*w.logic) == cav);
	tt.updateNemesis(cav, *w.logic); // refreshed at frame 19
	for (int i = 0; i < 20; ++i)
	{
		w.logic->runLogicFrame();
	}
	CHECK(tt.getCurNemesis(*w.logic) == cav); // exact 4*5 = 20 frame boundary still active
	w.logic->runLogicFrame();                 // frame +21 expires
	CHECK(tt.getCurNemesis(*w.logic) == nullptr);
	CHECK(tt.curNemesisID() == INVALID_ID);

	// AIRCRAFT accepted once cleared; stealthed and dead targets clear it.
	tt.updateNemesis(air, *w.logic);
	CHECK(tt.getCurNemesis(*w.logic) == air);
	air->kill(DAMAGE_UNRESISTABLE, 0);
	CHECK(tt.getCurNemesis(*w.logic) == nullptr);

	// Fresh stealth rider enters stealth after StealthDelay, clearing nemesis in getCurNemesis.
	Object *stealth = w.unit("Port203StealthRider", 'B', 800.0f, 800.0f);
	tt.updateNemesis(stealth, *w.logic);
	REQUIRE(tt.getCurNemesis(*w.logic) == stealth);
	w.runUntil([&]() { return InvisibilityManager::isStealthedAndUndetected(*stealth, nullptr); }, 10);
	REQUIRE(InvisibilityManager::isStealthedAndUndetected(*stealth, nullptr));
	CHECK(tt.getCurNemesis(*w.logic) == nullptr);

	// Snapshot round-trip with live nemesis preserves nemesis id, timestamp, and state hash.
	tt.updateNemesis(cav, *w.logic);
	REQUIRE(tt.getCurNemesis(*w.logic) == cav);
	std::vector<std::uint8_t> stream;
	std::string err;
	REQUIRE_MESSAGE(w.logic->saveSnapshot(stream, &err), err);
	const std::uint32_t expectedHash = w.hash();

	Port203World loaded;
	REQUIRE_MESSAGE(loaded.logic->loadSnapshot(stream, &err), err);
	CHECK(loaded.playerOf('A')->tunnelTracker().curNemesisID() == cav->getID());
	CHECK(loaded.playerOf('A')->tunnelTracker().nemesisTimestamp() == tt.nemesisTimestamp());
	CHECK(loaded.hash() == expectedHash);

	// Stop line updated for S-1107.
	const std::string stop = TunnelContain::stopLine();
	CHECK(stop.rfind("[S-1107]", 0) == 0);
}

TEST_CASE("PORT-203 aiGuardTunnelNetwork gates (RW 0x664ABF), aggressor state 5005 (RW 0x94D02C), AIWorld snapshot, and baseline CRC")
{
	Port203World w;
	Object *t1 = w.unit("Port203Tunnel", 'A', 200.0f, 500.0f);
	t1->friend_onBuildComplete();
	Object *guard = w.unit("Port203GuardSoldier", 'A', 240.0f, 500.0f);
	Object *immobile = w.unit("Port203Garrison", 'A', 300.0f, 500.0f);
	Object *proj = w.unit("Port203Projectile", 'A', 320.0f, 500.0f);
	Object *enemy = w.unit("Port203Attacker", 'B', 250.0f, 500.0f);
	w.logic->runLogicFrame();

	CHECK(guard->getAIUpdateInterface()->aiGuardTunnelNetwork(0, CMD_FROM_SCRIPT));
	CHECK(guard->getAIUpdateInterface()->currentStateId() == (unsigned)AI_TUNNEL_NETWORK_GUARD);
	CHECK_FALSE(guard->getAIUpdateInterface()->isIdle());
	REQUIRE(guard->getAIUpdateInterface()->stateMachine().currentState() != nullptr);
	CHECK(guard->getAIUpdateInterface()->stateMachine().currentState()->isActive());
	CHECK(immobile->getAIUpdateInterface() == nullptr);
	CHECK_FALSE(proj->getAIUpdateInterface()->aiGuardTunnelNetwork(0, CMD_FROM_SCRIPT));

	// HORDE_MEMBER is rejected by privateGuardTunnelNetwork (RW 0x664AC6); HORDE enters 0x18 cleanly.
	Object *horde = w.unit("Port203Horde", 'A', 400.0f, 500.0f);
	REQUIRE(horde->getContain() != nullptr);
	REQUIRE(horde->getContain()->getContainedItemsList() != nullptr);
	REQUIRE(!horde->getContain()->getContainedItemsList()->empty());
	Object *member = horde->getContain()->getContainedItemsList()->front();
	CHECK_FALSE(member->getAIUpdateInterface()->aiGuardTunnelNetwork(0, CMD_FROM_SCRIPT));
	CHECK(horde->getAIUpdateInterface()->aiGuardTunnelNetwork(GUARDMODE_GUARD_WITHOUT_PURSUIT, CMD_FROM_SCRIPT));
	CHECK(horde->getAIUpdateInterface()->currentStateId() == (unsigned)AI_TUNNEL_NETWORK_GUARD);
	CHECK(horde->getAIUpdateInterface()->guardMode() == GUARDMODE_GUARD_WITHOUT_PURSUIT);

	// Wait until guard enters t1 (5001 Idle).
	w.runUntil([&]() { return guard->getContainedBy() == t1; }, 40);
	REQUIRE(guard->getContainedBy() == t1);

	// Full snapshot round-trip while guarding inside tunnel network preserves hash and subsequent frames.
	std::vector<std::uint8_t> stream;
	std::string err;
	REQUIRE_MESSAGE(w.logic->saveSnapshot(stream, &err), err);
	const std::uint32_t savedHash = w.hash();

	Port203World restored;
	REQUIRE_MESSAGE(restored.logic->loadSnapshot(stream, &err), err);
	CHECK(restored.hash() == savedHash);
	w.logic->runLogicFrame();
	restored.logic->runLogicFrame();
	CHECK(restored.hash() == w.hash());

	// Now test 5005 AttackAggressor directly on a guard outside a tunnel (in 5003 Return), then tunnel-hit retaliation.
	Object *retaliator = w.unit("Port203GuardSoldier", 'A', 400.0f, 300.0f);
	REQUIRE(retaliator->getAIUpdateInterface()->aiGuardTunnelNetwork(GUARDMODE_GUARD_WITHOUT_PURSUIT, CMD_FROM_SCRIPT));
	DamageInfo hit;
	hit.m_input.m_sourceID = enemy->getID();
	hit.m_input.m_damageType = DAMAGE_SLASH;
	hit.m_input.m_amount = 5.0f;
	retaliator->attemptDamage(hit);
	w.logic->runLogicFrame();
	auto *tnState = dynamic_cast<AITunnelNetworkGuardState *>(retaliator->getAIUpdateInterface()->stateMachine().currentState());
	REQUIRE(tnState != nullptr);
	REQUIRE(tnState->guardMachine() != nullptr);
	REQUIRE(tnState->guardMachine()->currentState() != nullptr);
	CHECK(tnState->guardMachine()->currentState()->id() == (unsigned)AI_TN_GUARD_ATTACK_AGGRESSOR);
	CHECK(w.playerOf('A')->tunnelTracker().getCurNemesis(*w.logic) == enemy);

	w.runUntil([&]() { return enemy->isEffectivelyDead(); }, 60);
	CHECK(enemy->isEffectivelyDead());
}

TEST_CASE("PORT-203 TunnelContain::leaveNetwork (RW 0x88121A): non-last tunnel exposes entering horde (true), last tunnel ejects without exposure (false)")
{
	Port203World w;
	Object *t1 = w.unit("Port203Tunnel", 'A', 200.0f, 300.0f);
	Object *t2 = w.unit("Port203Tunnel", 'A', 500.0f, 300.0f);
	t1->friend_onBuildComplete();
	t2->friend_onBuildComplete();
	Object *stealth = w.unit("Port203StealthRider", 'A', 200.0f, 300.0f);
	w.logic->runLogicFrame();

	REQUIRE(t1->getContain()->addToContain(stealth));
	REQUIRE(w.playerOf('A')->tunnelTracker().containCount() == 1);

	// Destroy t1 (not the last tunnel): stealth stays in t2 via TunnelTracker::onTunnelDestroyed.
	w.logic->destroyObject(t1);
	w.logic->runLogicFrame();
	CHECK(w.playerOf('A')->tunnelTracker().tunnelCount() == 1u);
	CHECK(w.playerOf('A')->tunnelTracker().containCount() == 1);
	CHECK(stealth->getContainedBy() == t2);

	// Destroy t2 (last tunnel): removeAllContained(false) lets stealth out WITHOUT exposing stealth.
	w.logic->destroyObject(t2);
	w.logic->runLogicFrame();
	CHECK(w.playerOf('A')->tunnelTracker().tunnelCount() == 0u);
	CHECK(stealth->getContainedBy() == nullptr);
	CHECK_FALSE(stealth->testStatus((unsigned)CombatNames::status("DETECTED")));
}
