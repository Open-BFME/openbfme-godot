// OpenBFME unit tests, lane SIEGE-1: the siege ladders' and towers' docking against a wall (GameLogic/Module/SiegeModules.h). GPL-3.0.
//
// On the retail Helm's Deep (the HUD tests' retail world, the W3D models for the bones): the wall sections' SiegeDockingBehavior dock points; a siege ladder
// ordered at a wall (MSG_DO_SPECIAL_POWER_AT_OBJECT) reserves a dock point, drives to its approach point and faces the wall; without WALL-2's
// DynamicPortalBehaviour it waits there (S-4940); with a portal that answers "above the wall" (the integration point, a test double) it lowers (UNPACKING for
// LowerDelay), deploys (DEPLOYED: the status, the model condition, no order but evacuate), its passenger goes onto the wall, and the retract button raises it
// again (PACKING, held for RaiseDelay). The right click on a wall with a ladder selected is the deploy order. SKIPs loudly without the installs.

#include "doctest.h"

#include "HudTestUtil.h"
#include "ObjectTestUtil.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/SpecialPower.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/HudObjects.h"
#include "GameLogic/AI/AIEmotionStates.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/System/ShroudManager.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Combat/WeaponDelivery.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GameLogicDispatch.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/PortalModules.h"
#include "GameLogic/Module/SpecialAbilityModules.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponState.h"
#include "GameLogic/Module/SiegeModules.h"
#include "GameLogic/Module/LogicModules.h"
#include "GameLogic/Object/Contain/SiegeEngineContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "GameLogic/ScriptEngine/ScriptConditions.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"
#include "GameLogic/ScriptEngine/ScriptTemplates.h"
#include "GameLogic/ScriptEngine/Scripts.h"
#include "Common/Team.h"

#include <cmath>
#include <map>
#include <string>
#include <vector>

using namespace hudtest;

TEST_CASE("bfme1 isolation: siege deploy powers retain the second initiation sound only for BFME1")
{
	const initest::FileList bfme1Legend = { { "Data\\INI\\Default\\SubsystemLegend.ini", "; BFME1\r\n" } };
	const initest::FileList bfme2Legend = { { "Data\\INI\\Default\\SubsystemLegend.ini", "; legend\r\n" },
		{ "Data\\INI\\AwardSystem.ini", "; BFME2\r\n" } };
	const initest::FileList rotwkLegend = { { "Data\\INI\\Default\\SubsystemLegend.ini", "; legend\r\n" },
		{ "Data\\INI\\Default\\SubsystemLegendExpansion1.ini", "; RotWK\r\n" } };
	for (const std::string module : { "SiegeDeploySpecialPower", "SiegeDeployHordeSpecialPower" })
	{
		CAPTURE(module);
		const std::string text = "Object TestSiege\n Behavior = " + module + " ModuleTag_Deploy\n"
			" InitiateSound = TestPrimary\n InitiateSound2 = TestSecondary\n StartsPaused = Yes\n End\nEnd\n";
		for (const auto &legend : { bfme2Legend, rotwkLegend })
		{
			objtest::World other;
			LogicModules::registerAll(other.modules);
			other.fx.mount(legend);
			CHECK(other.load(text).find("Unknown field 'InitiateSound2'") != std::string::npos);
		}
		objtest::World bfme1(true);
		LogicModules::registerAll(bfme1.modules);
		bfme1.fx.mount(bfme1Legend);
		const std::string error = bfme1.load(text);
		REQUIRE_MESSAGE(error.empty(), error);
		const auto *data = dynamic_cast<const SpecialPowerUpdateBaseModuleData *>(
			bfme1.get("TestSiege")->behaviorModules().find("ModuleTag_Deploy")->data.get());
		REQUIRE(data);
		CHECK(data->m_initiateSound == "TestPrimary");
		CHECK(data->m_initiateSound2 == "TestSecondary");
		CHECK(data->m_startsPaused);
	}
	CHECK(SiegeDeploySpecialPowerModuleData{}.m_initiateSound2.empty());
	CHECK(SiegeDeployHordeSpecialPowerModuleData{}.m_initiateSound2.empty());
}

namespace
{
float dist2D(const Coord3D &a, const Coord3D &b)
{
	const float dx = a.x - b.x, dy = a.y - b.y;
	return std::sqrt(dx * dx + dy * dy);
}

bool siegeReportHas(GameLogic &logic, const char *id)
{
	const GameLogic::Report rep = logic.report();
	for (const std::string &line : rep.stops)
	{
		if (line.rfind(id, 0) == 0)
		{
			return true;
		}
	}
	return false;
}

const SpecialPowerTemplate *power(const char *name)
{
	REQUIRE(TheSpecialPowerStore);
	const SpecialPowerTemplate *t = TheSpecialPowerStore->findSpecialPowerTemplate(name);
	REQUIRE(t);
	return t;
}

// the player's special power order at an object (MSG_DO_SPECIAL_POWER_AT_OBJECT {id, target, options, source}) through the dispatcher
void orderPowerAt(Rig &r, Object &unit, const char *powerName, Object *target)
{
	const SpecialPowerTemplate *t = power(powerName);
	r.game->dispatch().dispatch([&] {
		r.local->selection() = { unit.getID() };
		GameMessage m(target ? MSG_DO_SPECIAL_POWER_AT_OBJECT : MSG_DO_SPECIAL_POWER, r.index());
		m.appendIntegerArgument((int)t->getID());
		if (target)
		{
			m.appendObjectIDArgument(target->getID());
		}
		m.appendIntegerArgument(0);
		m.appendObjectIDArgument(INVALID_ID);
		if (!target)
		{
			m.appendObjectIDArgument(INVALID_ID);
		}
		return m;
	}());
}

// The WALL-2 integration point played by the test (S-4940): IsEndPortalAboveWall as Open-BFME-2 DynamicPortalBehaviourGetPosition.cpp has it (BFME2 0x460DF6,
// RW 0x863EB3): the bone of the waypoint AboveWall of the template's DynamicPortalBehaviour (the ladder's and the tower's: BonePrefix Ladder, NumberOfBones 4,
// AboveWall 3 -> WayPoint Index:3, the fourth bone "Ladder04") in world space, on a wall (Pathfinder::isPointOnWall(point, false)). The waypoints' creation and
// removal are counted
struct TestPortal : public SiegePortalInterface
{
	Object *obj = nullptr;
	int created = 0, cleared = 0, asked = 0;
	bool isEndPortalAboveWall(Coord3D *out) override
	{
		++asked;
		ProjectileLaunchOffsets *provider = obj->logic().combat().launchOffsets();
		float bones[4][12];
		const int n = provider ? provider->multiLogicalBones(*obj, "Ladder", obj->getModelConditionBits(), 4, bones) : 0;
		if (n < 4)
		{
			return false;
		}
		const float *b = obj->getBasis();
		const Coord3D &p = *obj->getPosition();
		Coord3D w{ b[0] * bones[3][3] + b[1] * bones[3][7] + b[2] * bones[3][11] + p.x, b[3] * bones[3][3] + b[4] * bones[3][7] + b[5] * bones[3][11] + p.y,
			b[6] * bones[3][3] + b[7] * bones[3][7] + b[8] * bones[3][11] + p.z };
		if (out)
		{
			*out = w;
		}
		return obj->logic().aiWorld()->pathfinder().isPointOnWall(w, false);
	}
	void createPortalWaypoints() override { ++created; }
	void clearPortalWaypoints() override { ++cleared; }
	// RW 0x863D77 as read in RotWK: TopAttackPos's X and Y scaled by the length of the object's X axis normalised by its larger planar component, through the
	// object's transform. The ladder's TopAttackPos X:30 Y:0 Z:52 (radius 30), the tower's X:34 Y:0 Z:52 (radius 25)
	Coord3D topAttackPosition() override
	{
		const bool tower = obj->getTemplate()->getName() == "MordorSiegeTower";
		const float tx = tower ? 34.0f : 30.0f, ty = 0.0f, tz = 52.0f;
		const float *b = obj->getBasis();
		const float ax = std::fabs(b[0]), ay = std::fabs(b[3]);
		const float m = ax >= ay ? ax : ay;
		const float x = b[0] / m, y = b[3] / m;
		const float len = std::sqrt(x * x + y * y);
		const float lx = tx * len, ly = ty * len;
		const Coord3D &p = *obj->getPosition();
		return Coord3D{ b[0] * lx + b[1] * ly + b[2] * tz + p.x, b[3] * lx + b[4] * ly + b[5] * tz + p.y, b[6] * lx + b[7] * ly + b[8] * tz + p.z };
	}
	float topAttackRadius() const override { return obj->getTemplate()->getName() == "MordorSiegeTower" ? 25.0f : 30.0f; }
};
std::map<ObjectID, TestPortal> g_portals;
SiegePortalInterface *testPortalOf(Object &obj)
{
	TestPortal &p = g_portals[obj.getID()];
	p.obj = &obj;
	return &p;
}
struct PortalScope
{
	PortalScope()
	{
		g_portals.clear();
		SiegeDeploySpecialPower::setPortalOverrideForTests(&testPortalOf);
	}
	~PortalScope()
	{
		SiegeDeploySpecialPower::setPortalOverrideForTests(nullptr);
		g_portals.clear();
	}
};

// the first ladder dock point of Helm's Deep (HelmsDeepSectionAi #14's SIEGELADDER bone) and a ladder 250 south of it
struct LadderScene
{
	Object *wall = nullptr;
	SiegeDockingBehavior::Entry entry;
	Object *ladder = nullptr;
	SiegeDeploySpecialPower *deploy = nullptr;
	explicit LadderScene(Rig &r, const char *unit = "IsengardSiegeLadder", int type = 1)
	{
		for (Object *o = r.logic().getFirstObject(); o && !wall; o = o->getNextObject())
		{
			SiegeDockingBehavior *d = SiegeDockingBehavior::of(*o);
			for (size_t i = 0; d && i < d->entries().size() && !wall; ++i)
			{
				if (d->entries()[i].type == type)
				{
					wall = o;
					entry = d->entries()[i];
				}
			}
		}
		REQUIRE(wall);
		const Coord3D from = r.freeSpot(entry.position.x - entry.direction.x * 250.0f, entry.position.y - entry.direction.y * 250.0f, 60.0f);
		ladder = r.make(unit, from.x, from.y);
		r.frame(2);
		deploy = SiegeDeploySpecialPower::of(*ladder);
		REQUIRE(deploy);
	}
};
} // namespace

TEST_CASE("siege1 retail: Helm's Deep's wall sections read their dock points from the SIEGETOWER / SIEGELADDER bones (SiegeDockingBehavior RW 0x85CE65)")
{
	if (!haveWorld("siege1 dock points"))
	{
		return;
	}
	Rig r(shared(), "map wor helms deep", "FactionIsengard");
	const auto worldContext = shared().world->enterContext(); // this world's stores: a started game run before may have selected its own
	r.frame(3); // the docks read their bones on their first update (RW 0x85D216), one frame after they were made
	std::map<std::string, int> walls, tower, ladder;
	int uninitialised = 0;
	for (Object *o = r.logic().getFirstObject(); o; o = o->getNextObject())
	{
		SiegeDockingBehavior *d = SiegeDockingBehavior::of(*o);
		if (!d)
		{
			continue;
		}
		const std::string name = o->getTemplate()->getName();
		++walls[name];
		uninitialised += d->initialized() ? 0 : 1;
		int index = 0;
		for (const SiegeDockingBehavior::Entry &e : d->entries())
		{
			++(e.type == 0 ? tower : ladder)[name];
			CHECK(e.index == index++); // the running index: the towers first, then the ladders
			CHECK(e.reservedBy == INVALID_ID);
			CHECK(e.direction.z == 0.0f); // the bones' X axes lie flat
			// the direction is a unit vector (the bone's X axis): its length is 1 within the float rounding of the transform
			const float len = std::sqrt(e.direction.x * e.direction.x + e.direction.y * e.direction.y + e.direction.z * e.direction.z);
			CHECK(std::fabs(len - 1.0f) < 1e-3f); // tolerance: a rotation matrix column computed in binary32
		}
	}
	for (const auto &kv : walls)
	{
		MESSAGE(kv.first << ": " << kv.second << " pieces, " << tower[kv.first] << " SIEGETOWER and " << ladder[kv.first] << " SIEGELADDER dock points");
	}
	CHECK(uninitialised == 0);
	CHECK(walls == std::map<std::string, int>{ { "HelmsDeepCulvert", 1 }, { "HelmsDeepSectionAi", 3 }, { "HelmsDeepSectionB", 1 } });
	CHECK(tower == std::map<std::string, int>{ { "HelmsDeepCulvert", 4 }, { "HelmsDeepSectionAi", 6 }, { "HelmsDeepSectionB", 1 } });
	CHECK(ladder == std::map<std::string, int>{ { "HelmsDeepCulvert", 2 }, { "HelmsDeepSectionAi", 3 }, { "HelmsDeepSectionB", 2 } });
	CHECK(siegeReportHas(r.logic(), "[S-4942]"));
}

TEST_CASE("siege1 retail: a siege ladder ordered at Helm's Deep's wall drives to the wall, faces it and, its own DynamicPortalBehaviour answering IsEndPortalAboveWall, lowers and deploys (S-4940)")
{
	if (!haveWorld("siege1 ladder without portal"))
	{
		return;
	}
	Rig r(shared(), "map wor helms deep", "FactionIsengard");
	const auto worldContext = shared().world->enterContext(); // this world's stores: a started game run before may have selected its own
	r.frame(3);
	r.game->shroud().revealMapForPlayerPermanently(r.index());
	LadderScene sc(r);
	static const int kDeploying = CombatNames::status("DEPLOYING");
	CHECK(sc.deploy->phase() == SiegeDeploySpecialPower::PHASE_IDLE);
	CHECK(sc.deploy->isReady());
	orderPowerAt(r, *sc.ladder, "SpecialAbilitySiegeDeploy", sc.wall);
	// the initiate (RW 0x8CA5C2): the target's dock reserves its closest compatible point for the ladder, DEPLOYING, phase 1, the move to the approach point
	CHECK(sc.deploy->phase() == SiegeDeploySpecialPower::PHASE_APPROACH);
	CHECK(sc.deploy->dockId() == sc.wall->getID());
	CHECK(sc.ladder->testStatus((unsigned)kDeploying));
	const SiegeDockingBehavior *dock = SiegeDockingBehavior::of(*sc.wall);
	REQUIRE(dock);
	int reserved = 0;
	for (const SiegeDockingBehavior::Entry &e : dock->entries())
	{
		reserved += e.reservedBy == sc.ladder->getID() ? 1 : 0;
	}
	CHECK(reserved == 1);
	// the approach point: the reserved dock point pushed out by ExtraWallDistance 15 along the wall's centre -> dock point line (RW 0x8C9D06)
	const Coord3D approach = sc.deploy->approachPoint();
	CHECK(dist2D(approach, sc.entry.position) == doctest::Approx(15.0f).epsilon(1e-4)); // tolerance: 15 along a float unit vector
	float closest = 1e9f;
	bool facedThere = false;
	int maxPhase = 0;
	for (int f = 0; f < 200; ++f)
	{
		r.frame(1);
		const Coord3D &p = *sc.ladder->getPosition();
		const float d = dist2D(p, approach);
		const Coord3D face{ p.x + sc.deploy->dockDirection().x, p.y + sc.deploy->dockDirection().y, p.z };
		if (d < closest)
		{
			closest = d;
			facedThere = std::fabs(emotionRelAngle(*sc.ladder, face)) < 0.15f; // tolerance: the deploy's facing test (RW 0xBE53C8)
		}
		maxPhase = sc.deploy->phase() > maxPhase ? sc.deploy->phase() : maxPhase;
	}
	INFO("the ladder came within " << closest << " of its approach point");
	CHECK(closest < 15.0f); // tolerance: the locomotor stops within its close-enough distance of the approach point
	CHECK(facedThere);
	// the deploy asks the ladder's own DynamicPortalBehaviour (lane WALL-2, merged): its AboveWall bone is on the wall top (IsEndPortalAboveWall), so the ladder
	// lowers (UNPACKING) and stands DEPLOYED on the dock point it reserved; the portal stop S-4940 is not raised any more
	CHECK(maxPhase == SiegeDeploySpecialPower::PHASE_DEPLOYED);
	CHECK(sc.ladder->testStatus((unsigned)CombatNames::status("DEPLOYED")));
	reserved = 0;
	for (const SiegeDockingBehavior::Entry &e : dock->entries())
	{
		reserved += e.reservedBy == sc.ladder->getID() ? 1 : 0;
	}
	CHECK(reserved == 1);
	CHECK_FALSE(siegeReportHas(r.logic(), "[S-4940]"));
	// and the climb is in the pathfinder: the portal made its waypoints
	const DynamicPortalBehaviour *portal = DynamicPortalBehaviour::of(*sc.ladder);
	REQUIRE(portal);
	CHECK(portal->created());
}

TEST_CASE("siege1 retail: with a portal answering IsEndPortalAboveWall a siege ladder lowers onto Helm's Deep's wall (UNPACKING, LowerDelay), deploys, takes no order but evacuate and raises on the retract button (PACKING, RaiseDelay)")
{
	if (!haveWorld("siege1 ladder deploy"))
	{
		return;
	}
	PortalScope portals;
	Rig r(shared(), "map wor helms deep", "FactionIsengard");
	const auto worldContext = shared().world->enterContext(); // this world's stores: a started game run before may have selected its own
	r.frame(3);
	r.game->shroud().revealMapForPlayerPermanently(r.index());
	LadderScene sc(r);
	static const int kUnpacking = CombatNames::modelCondition("UNPACKING"), kDeployedCond = CombatNames::modelCondition("DEPLOYED"),
					 kPacking = CombatNames::modelCondition("PACKING"), kMoving = CombatNames::modelCondition("MOVING");
	static const int kDeployed = CombatNames::status("DEPLOYED");
	CHECK(sc.deploy->data().m_lowerDelay == 6u);  // LowerDelay 1200 ms
	CHECK(sc.deploy->data().m_raiseDelay == 10u); // RaiseDelay 2000 ms
	CHECK(sc.deploy->data().m_extraWallDistance == 15.0f);
	CHECK(sc.deploy->data().m_skipAdjustPosition);
	orderPowerAt(r, *sc.ladder, "SpecialAbilitySiegeDeploy", sc.wall);
	int lowered = -1, deployed = -1;
	for (int f = 1; f <= 120 && deployed < 0; ++f)
	{
		r.frame(1);
		if (lowered < 0 && sc.deploy->phase() == SiegeDeploySpecialPower::PHASE_LOWERING)
		{
			lowered = f;
			CHECK(sc.ladder->testModelCondition(kUnpacking));
			CHECK(sc.ladder->getAIUpdateInterface()->isBusy()); // AI command 0x31
			CHECK(sc.deploy->isActive());
			CHECK_FALSE(sc.deploy->isReady());
		}
		if (sc.deploy->phase() == SiegeDeploySpecialPower::PHASE_DEPLOYED)
		{
			deployed = f;
		}
	}
	// the facing and the portal's answer at the wall (frame 38, a deterministic game), then LowerDelay frames
	CHECK(lowered == 38);
	CHECK(deployed == lowered + 6);
	CHECK(sc.deploy->isDeployed());
	CHECK(sc.ladder->testStatus((unsigned)kDeployed));
	CHECK(sc.ladder->testModelCondition(kDeployedCond));
	CHECK_FALSE(sc.ladder->testModelCondition(kUnpacking));
	CHECK_FALSE(sc.ladder->testModelCondition(kMoving));
	CHECK(g_portals[sc.ladder->getID()].created == 1); // the deployed ladder's waypoints (RW 0x86404D)
	CHECK(g_portals[sc.ladder->getID()].cleared == 0);
	// deployed: the ladder takes no order (SiegeAIUpdate RW 0x893C80: only evacuate) and does not move (Object::isMobile: a deployed SIEGE_TOWER)
	AIUpdateInterface *ai = sc.ladder->getAIUpdateInterface();
	CHECK_FALSE(ai->allowedToRespondToCommand(CMD_FROM_PLAYER, 0));
	CHECK(ai->allowedToRespondToCommand(CMD_FROM_PLAYER, 0x1B));
	const Coord3D at = *sc.ladder->getPosition();
	ai->aiMoveToPosition(Coord3D{ at.x, at.y - 300.0f, 0.0f }, CMD_FROM_PLAYER);
	r.frame(20);
	CHECK(sc.ladder->getPosition()->x == at.x);
	CHECK(sc.ladder->getPosition()->y == at.y);
	CHECK(sc.deploy->isDeployed());
	// the retract button (Command_SpecialAbilitySiegeLadderRetract: SpecialAbilityStop -> StopSpecialPower -> the deploy's stop, RW 0x8CA2D0): PACKING, the
	// ladder held (DISABLED_TEMPORARILY_BUSY) for RaiseDelay frames, then idle
	orderPowerAt(r, *sc.ladder, "SpecialAbilityStop", nullptr);
	CHECK(sc.deploy->isDeployed()); // the stop request is read by the next update
	r.frame(1);
	CHECK(sc.deploy->phase() == SiegeDeploySpecialPower::PHASE_RAISING);
	CHECK(sc.ladder->testModelCondition(kPacking));
	CHECK_FALSE(sc.ladder->testModelCondition(kDeployedCond));
	CHECK_FALSE(sc.ladder->testStatus((unsigned)kDeployed));
	CHECK(sc.ladder->isDisabledByType(DISABLED_TEMPORARILY_BUSY));
	CHECK(g_portals[sc.ladder->getID()].cleared == 1); // the waypoints go (RW 0x8639EF)
	int idleAt = -1;
	for (int f = 1; f <= 30 && idleAt < 0; ++f)
	{
		r.frame(1);
		if (sc.deploy->phase() == SiegeDeploySpecialPower::PHASE_IDLE)
		{
			idleAt = f;
		}
	}
	CHECK(idleAt == 10);
	CHECK_FALSE(sc.ladder->testModelCondition(kPacking));
	CHECK_FALSE(sc.ladder->isDisabledByType(DISABLED_TEMPORARILY_BUSY));
	CHECK(sc.deploy->isReady());
	// raised, it drives again
	ai->aiMoveToPosition(Coord3D{ at.x, at.y - 300.0f, 0.0f }, CMD_FROM_PLAYER);
	r.frame(20);
	CHECK(dist2D(*sc.ladder->getPosition(), at) > 30.0f); // tolerance: it drove off (20 frames at its speed)
}

TEST_CASE("siege1 retail: a deployed siege ladder's berserker goes onto the wall at the ladder's top (Ladder04) on the wall layer (EvacuatePassengersOnDeploy, RW 0x87F87C)")
{
	if (!haveWorld("siege1 ladder rider"))
	{
		return;
	}
	PortalScope portals;
	Rig r(shared(), "map wor helms deep", "FactionIsengard");
	const auto worldContext = shared().world->enterContext(); // this world's stores: a started game run before may have selected its own
	r.frame(3);
	r.game->shroud().revealMapForPlayerPermanently(r.index());
	LadderScene sc(r);
	Object *rider = r.make("IsengardBeserker", sc.ladder->getPosition()->x + 30.0f, sc.ladder->getPosition()->y);
	r.frame(1);
	ContainModuleInterface *c = sc.ladder->getContain();
	REQUIRE(c);
	REQUIRE(c->isValidContainerFor(*rider, true, false)); // PassengerFilter NONE +CAN_RIDE_SIEGE_LADDER, Slots 1
	REQUIRE(c->addToContain(rider));
	CHECK(rider->getContainedBy() == sc.ladder);
	orderPowerAt(r, *sc.ladder, "SpecialAbilitySiegeDeploy", sc.wall);
	for (int f = 0; f < 120 && !sc.deploy->isDeployed(); ++f)
	{
		r.frame(1);
	}
	REQUIRE(sc.deploy->isDeployed());
	// the deploy's evacuate (AI command 0x1B -> the contain's slot 0x80): the rider leaves the ladder through the deployed exit
	int out = -1;
	for (int f = 0; f < 60 && out < 0; ++f)
	{
		out = rider->getContainedBy() == nullptr ? f : -1;
		if (out < 0)
		{
			r.frame(1);
		}
	}
	CHECK(out == 1); // the contain's exit runs in its next update
	CHECK(sc.deploy->isDeployed());
	CHECK(rider->getContainedBy() == nullptr);
	CHECK(c->getContainCount() == 0u);
	float bone[12];
	REQUIRE(r.logic().combat().launchOffsets()->singleLogicalBone(*sc.ladder, "Ladder04", bone));
	const float *b = sc.ladder->getBasis();
	const Coord3D &lp = *sc.ladder->getPosition();
	const Coord3D top{ b[0] * bone[3] + b[1] * bone[7] + b[2] * bone[11] + lp.x, b[3] * bone[3] + b[4] * bone[7] + b[5] * bone[11] + lp.y,
		b[6] * bone[3] + b[7] * bone[7] + b[8] * bone[11] + lp.z };
	const Coord3D &rp = *rider->getPosition();
	INFO("the rider at " << rp.x << ", " << rp.y << ", " << rp.z << "; Ladder04 at " << top.x << ", " << top.y << ", " << top.z);
	CHECK(rp.x == doctest::Approx(top.x).epsilon(1e-5)); // tolerance: the test's double-rounded transform against the engine's binary32 one
	CHECK(rp.y == doctest::Approx(top.y).epsilon(1e-5)); // tolerance: as above
	AIUpdateInterface *rai = rider->getAIUpdateInterface();
	REQUIRE(rai);
	CHECK(isWallLayer(rai->adapter().getLayer()));
	CHECK(r.logic().aiWorld()->pathfinder().isPointOnWall(rp, false));
	CHECK(siegeReportHas(r.logic(), "[S-4943]"));
	// a human player's rider idles there (RW 0x5E821A); it walks on the wall top when ordered
	CHECK(rai->isIdle());
}

TEST_CASE("siege1 retail: a right click on a Helm's Deep wall with a siege ladder selected is its deploy (MSG_DO_SPECIAL_POWER_AT_OBJECT, RW 0x69D674)")
{
	if (!haveWorld("siege1 right click"))
	{
		return;
	}
	Rig r(shared(), "map wor helms deep", "FactionIsengard");
	const auto worldContext = shared().world->enterContext(); // this world's stores: a started game run before may have selected its own
	r.frame(3);
	r.game->shroud().revealMapForPlayerPermanently(r.index());
	LadderScene sc(r);
	r.input->commandTranslator().setUseAlternateMouse(true);
	r.lookAt(*sc.ladder->getPosition());
	r.frame(1);
	const Coord3D lp = *sc.ladder->getPosition();
	const ICoord2D ladderPx = r.screenOf({ lp.x, lp.y, lp.z + 10.0f });
	r.leftClick(ladderPx.x, ladderPx.y);
	REQUIRE(r.input->ui().selected() == std::vector<ObjectID>{ sc.ladder->getID() });
	r.frame(1);
	// the wall under the cursor: a point on the wall section's top near its dock point
	const Coord3D onWall{ sc.entry.position.x + sc.entry.direction.x * 20.0f, sc.entry.position.y + sc.entry.direction.y * 20.0f, sc.entry.position.z };
	r.lookAt(onWall);
	r.frame(1);
	const ICoord2D px = r.screenOf(onWall);
	const Object *picked = HudObjects::pickObject(r.input->context(), px);
	REQUIRE(picked);
	INFO("picked " << picked->getTemplate()->getName());
	CHECK(picked->isKindOfName("WALK_ON_TOP_OF_WALL"));
	const size_t before = r.input->messageLog().size();
	r.rightClick(px.x, px.y);
	std::string line;
	for (size_t i = before; i < r.input->messageLog().size(); ++i)
	{
		const std::string &l = r.input->messageLog()[i];
		line = l.rfind("MSG_DO_SPECIAL_POWER_AT_OBJECT", 0) == 0 ? l : line;
	}
	INFO("sent: " << line);
	REQUIRE(!line.empty());
	r.frame(2);
	CHECK(sc.deploy->phase() == SiegeDeploySpecialPower::PHASE_APPROACH);
	CHECK(sc.deploy->dockId() == picked->getID());
}

TEST_CASE("siege1 retail: Mordor's siege tower docks only at the wall's SIEGETOWER points, deploys with its crew aboard and its riders' exit (HordeSiegeEngineContain)")
{
	if (!haveWorld("siege1 siege tower"))
	{
		return;
	}
	PortalScope portals;
	Rig r(shared(), "map wor helms deep", "FactionMordor");
	const auto worldContext = shared().world->enterContext(); // this world's stores: a started game run before may have selected its own
	r.frame(3);
	r.game->shroud().revealMapForPlayerPermanently(r.index());
	LadderScene sc(r, "MordorSiegeTower", 0);
	CHECK(sc.deploy->data().m_lowerDelay == 10u); // LowerDelay 2000 ms
	CHECK_FALSE(sc.deploy->data().m_evacuateCrewOnDeploy);
	orderPowerAt(r, *sc.ladder, "SpecialAbilitySiegeDeploy", sc.wall);
	CHECK(sc.deploy->phase() == SiegeDeploySpecialPower::PHASE_APPROACH);
	const SiegeDockingBehavior *dock = SiegeDockingBehavior::of(*sc.wall);
	REQUIRE(dock);
	for (const SiegeDockingBehavior::Entry &e : dock->entries())
	{
		if (e.reservedBy == sc.ladder->getID())
		{
			CHECK(e.type == 0); // a tower (not SIEGE_LADDER) takes only SIEGETOWER points (RW 0x85CD20)
		}
	}
	int deployed = -1;
	for (int f = 1; f <= 200 && deployed < 0; ++f)
	{
		r.frame(1);
		deployed = sc.deploy->isDeployed() ? f : -1;
	}
	INFO("deployed at frame " << deployed << "; phase " << sc.deploy->phase());
	CHECK(deployed == 57); // the drive to the dock point, the facing, then LowerDelay 10 frames (a deterministic game)
	CHECK(sc.ladder->testStatus((unsigned)CombatNames::status("DEPLOYED")));
	CHECK(g_portals[sc.ladder->getID()].created == 1);
	HordeSiegeEngineContain *st = dynamic_cast<HordeSiegeEngineContain *>(sc.ladder->getContain());
	REQUIRE(st);
	CHECK(st->crewCount() == 2); // the trolls stay (EvacuateCrewOnDeploy No)
}

TEST_CASE("siege1 retail review of #64: a ladder killed on its approach frees its dock point at the next refresh, while its death still plays (the dead "
		  "bit, RW 0x85CBC8)")
{
	if (!haveWorld("siege1 deploy dead"))
	{
		return;
	}
	PortalScope portals;
	Rig r(shared(), "map wor helms deep", "FactionIsengard");
	const auto worldContext = shared().world->enterContext(); // this world's stores: a started game run before may have selected its own
	r.frame(3);
	r.game->shroud().revealMapForPlayerPermanently(r.index());
	LadderScene sc(r);
	orderPowerAt(r, *sc.ladder, "SpecialAbilitySiegeDeploy", sc.wall);
	r.frame(5);
	REQUIRE(sc.deploy->phase() == SiegeDeploySpecialPower::PHASE_APPROACH);
	const ObjectID ladder = sc.ladder->getID();
	int reserved = 0;
	for (const SiegeDockingBehavior::Entry &e : SiegeDockingBehavior::of(*sc.wall)->entries())
	{
		reserved += e.reservedBy == ladder ? 1 : 0;
	}
	REQUIRE(reserved == 1);
	sc.ladder->kill(DAMAGE_UNRESISTABLE, DEATH_NORMAL);
	r.frame(6); // the dock refreshes its reservations every 5 frames (RW 0x85D216 -> RW 0x85CBC8)
	REQUIRE(r.logic().findObjectByID(ladder) != nullptr); // still there: its death plays
	for (const SiegeDockingBehavior::Entry &e : SiegeDockingBehavior::of(*sc.wall)->entries())
	{
		CHECK(e.reservedBy != ladder);
	}
}

TEST_CASE("siege1 retail review of #64: the Necromancers' banner has no WeaponSet, and its Soul Freeze fires its special weapon (the object's own weapon "
		  "host, the mine carrier's path); it used to fizzle")
{
	if (!haveWorld("siege1 soul freeze"))
	{
		return;
	}
	Rig r(shared(), "map wor helms deep", "FactionAngmar");
	const auto worldContext = shared().world->enterContext(); // this world's stores: a started game run before may have selected its own
	r.frame(3);
	const Coord3D at = r.freeSpot(1640.0f, 3150.0f, 60.0f);
	Object *banner = r.make("AngmarNecromancerBanner", at.x, at.y);
	r.frame(2);
	CHECK(banner->getWeapons() == nullptr); // no WeaponSet (angmarnecromancerbanner.ini)
	WeaponFireSpecialAbilityUpdate *freeze = nullptr;
	for (const auto &m : banner->modules())
	{
		if (auto *p = dynamic_cast<WeaponFireSpecialAbilityUpdate *>(m.get()))
		{
			const Weapon *w = p->ownedWeapon();
			if (w && w->getTemplate()->getName() == "AngmarNecroSoulFreezeWeapon") // ModuleTag_NecroSoulFreezeBlastWeaponFireUpdate (WhichSpecialWeapon 3)
			{
				freeze = p;
			}
		}
	}
	REQUIRE(freeze);
	const Coord3D target{ at.x + 80.0f, at.y, 0.0f };
	const SpecialPowerTemplate *power = TheSpecialPowerStore->findSpecialPowerTemplate("SpecialAbilityNecroSoulFreeze");
	REQUIRE(power);
	freeze->initiateIntentToDoSpecialPower(power, nullptr, &target, 0, 0);
	for (int f = 0; f < 150 && freeze->abilitiesTriggered() == 0; ++f)
	{
		r.frame(1);
	}
	CHECK(freeze->abilitiesTriggered() == 1);
	CHECK(freeze->ownedWeapon()->lastFireFrame() != 0); // tolerance: that it fired at all (0: never); its frame is the unpack's
}

TEST_CASE("siege1 retail: a player's order during the approach ends the deploy (RW 0x8CA3D5); the dock point is freed; a dead wall makes a deployed ladder raise")
{
	if (!haveWorld("siege1 deploy cancel"))
	{
		return;
	}
	PortalScope portals;
	Rig r(shared(), "map wor helms deep", "FactionIsengard");
	const auto worldContext = shared().world->enterContext(); // this world's stores: a started game run before may have selected its own
	r.frame(3);
	r.game->shroud().revealMapForPlayerPermanently(r.index());
	LadderScene sc(r);
	static const int kDeploying = CombatNames::status("DEPLOYING");
	orderPowerAt(r, *sc.ladder, "SpecialAbilitySiegeDeploy", sc.wall);
	r.frame(5);
	REQUIRE(sc.deploy->phase() == SiegeDeploySpecialPower::PHASE_APPROACH);
	// the player's move: the AI is not idle and its last command is the player's -> stop (RW 0x8CA2F1: DEPLOYING off, phase 0, no dock)
	const Coord3D back{ sc.ladder->getPosition()->x - sc.entry.direction.x * 100.0f, sc.ladder->getPosition()->y - sc.entry.direction.y * 100.0f, 0.0f };
	sc.ladder->getAIUpdateInterface()->aiMoveToPosition(back, CMD_FROM_PLAYER);
	r.frame(1);
	CHECK(sc.deploy->phase() == SiegeDeploySpecialPower::PHASE_IDLE);
	CHECK(sc.deploy->dockId() == INVALID_ID);
	CHECK_FALSE(sc.ladder->testStatus((unsigned)kDeploying));
	r.frame(6); // the dock refreshes its reservations every 5 frames (RW 0x85D216 -> RW 0x85CBC8)
	for (const SiegeDockingBehavior::Entry &e : SiegeDockingBehavior::of(*sc.wall)->entries())
	{
		CHECK(e.reservedBy == INVALID_ID);
	}
	// again, to the end; then the wall dies: the deployed ladder raises (phase 4, then 0)
	orderPowerAt(r, *sc.ladder, "SpecialAbilitySiegeDeploy", sc.wall);
	for (int f = 0; f < 200 && !sc.deploy->isDeployed(); ++f)
	{
		r.frame(1);
	}
	REQUIRE(sc.deploy->isDeployed());
	sc.wall->kill(DAMAGE_UNRESISTABLE, 0);
	int raised = -1;
	for (int f = 1; f <= 40 && raised < 0; ++f)
	{
		r.frame(1);
		raised = sc.deploy->phase() == SiegeDeploySpecialPower::PHASE_IDLE ? f : -1;
	}
	INFO("the ladder was idle again " << raised << " frames after the wall died");
	CHECK(raised > 0); // tolerance: the wall's death (a structure's slow death) decides the frame; printed
	CHECK_FALSE(sc.ladder->testStatus((unsigned)CombatNames::status("DEPLOYED")));
}

TEST_CASE("siege1 retail: an Uruk horde's SiegeDeployHordeSpecialPower (HordeDeploy No in 2.01) takes the deploy order, wakes and idles the horde (RW 0x8CAA12 / 0x8CA978)")
{
	if (!haveWorld("siege1 horde power"))
	{
		return;
	}
	Rig r(shared(), "map wor helms deep", "FactionIsengard");
	const auto worldContext = shared().world->enterContext(); // this world's stores: a started game run before may have selected its own
	r.frame(3);
	r.game->shroud().revealMapForPlayerPermanently(r.index());
	LadderScene sc(r);
	const Coord3D c = r.freeSpot(sc.ladder->getPosition()->x + 120.0f, sc.ladder->getPosition()->y, 120.0f);
	Object *horde = r.make("IsengardFighterHorde", c.x, c.y);
	r.frame(3);
	SiegeDeployHordeSpecialPower *hp = SiegeDeployHordeSpecialPower::of(*horde);
	REQUIRE(hp);
	CHECK(hp->getSpecialPowerTemplate() == power("SpecialAbilitySiegeDeploy"));
	CHECK(hp->isReady()); // the base timer: ReloadTime 0
	// the order goes to the horde and the ladder alike (the selection; source NONE)
	r.game->dispatch().dispatch([&] {
		r.local->selection() = { sc.ladder->getID(), horde->getID() };
		GameMessage m(MSG_DO_SPECIAL_POWER_AT_OBJECT, r.index());
		m.appendIntegerArgument((int)power("SpecialAbilitySiegeDeploy")->getID());
		m.appendObjectIDArgument(sc.wall->getID());
		m.appendIntegerArgument(0);
		m.appendObjectIDArgument(INVALID_ID);
		return m;
	}());
	CHECK(sc.deploy->phase() == SiegeDeploySpecialPower::PHASE_APPROACH);
	CHECK(hp->targetId() == INVALID_ID); // no HordeDeploy: the initiate only wakes the module
	r.frame(2);
	// its update: no target, the AI's last command not the player's -> idle, then asleep
	CHECK(horde->getAIUpdateInterface()->isIdle());
	CHECK(hp->friend_getNextCallFrame() == (UnsignedInt)UPDATE_SLEEP_FOREVER);
}

TEST_CASE("siege1 retail: a unit on the wall reaches a deployed ladder at its top (the melee reach's SIEGE_TOWER branch, RW 0x6EDB6F; TopAttackPos / TopAttackRadius)")
{
	if (!haveWorld("siege1 ladder top reach"))
	{
		return;
	}
	PortalScope portals;
	Rig r(shared(), "map wor helms deep", "FactionIsengard");
	const auto worldContext = shared().world->enterContext(); // this world's stores: a started game run before may have selected its own
	r.frame(3);
	r.game->shroud().revealMapForPlayerPermanently(r.index());
	LadderScene sc(r);
	TestPortal &portal = *static_cast<TestPortal *>(testPortalOf(*sc.ladder));
	const Coord3D topBefore = portal.topAttackPosition();
	CHECK_FALSE(siegeTowerTopReach(*sc.ladder, topBefore, 10.0f)); // not deployed: no reach
	orderPowerAt(r, *sc.ladder, "SpecialAbilitySiegeDeploy", sc.wall);
	for (int f = 0; f < 120 && !sc.deploy->isDeployed(); ++f)
	{
		r.frame(1);
	}
	REQUIRE(sc.deploy->isDeployed());
	const Coord3D top = portal.topAttackPosition();
	CHECK(siegeTowerTopReach(*sc.ladder, top, 10.0f));
	CHECK(siegeTowerTopReach(*sc.ladder, Coord3D{ top.x + 39.0f, top.y, top.z }, 10.0f));  // within TopAttackRadius 30 + the radius 10 in x
	CHECK_FALSE(siegeTowerTopReach(*sc.ladder, Coord3D{ top.x + 40.0f, top.y, top.z }, 10.0f));
	CHECK_FALSE(siegeTowerTopReach(*sc.ladder, Coord3D{ top.x, top.y, top.z + 20.0f }, 10.0f)); // 20 in height is too far (RW: below 20)
	// an enemy on the wall top beside it attacks the ladder and hits it
	Player *enemy = r.game->players().findPlayerWithName("Player_2");
	REQUIRE(enemy);
	Object *hero = r.make("GondorBoromir", top.x, top.y, enemy);
	r.frame(1);
	AIUpdateInterface *hai = hero->getAIUpdateInterface();
	REQUIRE(hai);
	hai->settleOnLayer();
	INFO("the hero at " << hero->getPosition()->x << ", " << hero->getPosition()->y << ", " << hero->getPosition()->z << " layer " << (int)hai->adapter().getLayer()
						<< "; the ladder's top at " << top.x << ", " << top.y << ", " << top.z);
	REQUIRE(isWallLayer(hai->adapter().getLayer()));
	const float h0 = sc.ladder->getBodyModule()->getHealth();
	REQUIRE(hai->aiAttackObject(sc.ladder, CMD_FROM_PLAYER));
	int hit = -1;
	for (int f = 1; f <= 100 && hit < 0; ++f)
	{
		r.frame(1);
		hit = sc.ladder->getBodyModule()->getHealth() < h0 ? f : -1;
	}
	// (the hero stands over the ladder's top: the footprint test of the melee reach already passes; the top branch alone is pinned above)
	CHECK(hit == 7);
}

TEST_CASE("siege1 retail: the scripts' DEPLOY_SIEGE_ON_NAMED_WALL / RETRACT_SIEGE_FROM_WALL / DEPLOY_SIEGE_NEAR_TEAM and IS_SEIGE_ATTACHED_TO_WALL (RW 0x7BD7A7 / 0x7BD7FC / 0x7C77EB / 0x7E5760)")
{
	if (!haveWorld("siege1 scripts"))
	{
		return;
	}
	PortalScope portals;
	Rig r(shared(), "map wor helms deep", "FactionIsengard");
	const auto worldContext = shared().world->enterContext(); // this world's stores: a started game run before may have selected its own
	r.frame(3);
	LadderScene sc(r);
	sc.ladder->setName("SIEGE1 Ladder");
	sc.wall->setName("SIEGE1 Wall");
	ScriptEngine &e = r.logic().scriptEngine();
	auto stringParam = [](int type, const std::string &s) {
		ScriptParameter p;
		p.type = type;
		p.stringValue = s;
		return p;
	};
	auto action = [](const char *name, std::vector<ScriptParameter> params) {
		ScriptActionRec a;
		a.internalName = name;
		a.type = a.resolved = ScriptTemplates::findAction(name);
		a.params = std::move(params);
		return a;
	};
	auto attached = [&]() {
		ScriptCondition c;
		c.type = c.resolved = ScriptTemplates::findCondition("IS_SEIGE_ATTACHED_TO_WALL");
		c.params = { stringParam(14, "SIEGE1 Ladder") };
		return ScriptConditions::evaluate(e, c);
	};
	CHECK_FALSE(attached());
	e.executeAction(action("DEPLOY_SIEGE_ON_NAMED_WALL", { stringParam(14, "SIEGE1 Ladder"), stringParam(14, "SIEGE1 Wall") }));
	CHECK(sc.deploy->phase() == SiegeDeploySpecialPower::PHASE_APPROACH);
	CHECK(sc.deploy->dockId() == sc.wall->getID());
	for (int f = 0; f < 120 && !sc.deploy->isDeployed(); ++f)
	{
		r.frame(1);
	}
	CHECK(attached());
	e.executeAction(action("RETRACT_SIEGE_FROM_WALL", { stringParam(14, "SIEGE1 Ladder") }));
	r.frame(1);
	CHECK(sc.deploy->phase() == SiegeDeploySpecialPower::PHASE_RAISING);
	CHECK_FALSE(attached());
	r.frame(12);
	REQUIRE(sc.deploy->phase() == SiegeDeploySpecialPower::PHASE_IDLE);
	// a raised ladder keeps DEPLOYING (RotWK clears it only in the stop RW 0x8CA2F1 and the approach RW 0x8C9D06): the team form passes it over
	static const int kDeploying = CombatNames::status("DEPLOYING");
	CHECK(sc.ladder->testStatus((unsigned)kDeploying));
	// the team form: the ladders' team, the walls around its centre within 5000 near to far: a second ladder deploys at the nearest wall that can dock it
	const Coord3D c2 = r.freeSpot(sc.ladder->getPosition()->x - 150.0f, sc.ladder->getPosition()->y, 60.0f);
	Object *second = r.make("IsengardSiegeLadder", c2.x, c2.y);
	r.frame(2);
	SiegeDeploySpecialPower *deploy2 = SiegeDeploySpecialPower::of(*second);
	REQUIRE(deploy2);
	Team *team = sc.ladder->getTeam();
	REQUIRE(team);
	REQUIRE(second->getTeam() == team);
	const std::string teamName = "Player_1/" + team->getName();
	REQUIRE(ScriptConditions::team(e, teamName) == team);
	ScriptParameter radius;
	radius.type = 1;
	radius.realValue = 5000.0f;
	e.executeAction(action("DEPLOY_SIEGE_NEAR_TEAM", { stringParam(3, teamName), stringParam(3, teamName), radius }));
	CHECK(sc.deploy->phase() == SiegeDeploySpecialPower::PHASE_IDLE);
	CHECK(deploy2->phase() == SiegeDeploySpecialPower::PHASE_APPROACH);
	CHECK(deploy2->dockId() != INVALID_ID);
	CHECK(e.stats().unportedActions.empty());
}

TEST_CASE("siege1 retail: Grond (CrusherLevel 2) drives over an enemy soldier and crushes it; a battering ram (no CrusherLevel) does not")
{
	if (!haveWorld("siege1 crush"))
	{
		return;
	}
	Rig r(shared(), "map mp fall back 4p", "FactionMordor");
	const auto worldContext = shared().world->enterContext(); // this world's stores: a started game run before may have selected its own
	r.frame(2);
	Player *enemy = r.game->players().findPlayerWithName("Player_2");
	REQUIRE(enemy);
	auto crushes = [&](const char *engineName) {
		const Coord3D c0 = r.freeSpot(1500.0f, 1500.0f, 400.0f);
		Object *eng = r.make(engineName, c0.x, c0.y);
		Object *soldier = r.make("GondorFighter", c0.x + 150.0f, c0.y, enemy);
		r.frame(5);
		const ObjectID sid = soldier->getID();
		soldier->getAIUpdateInterface()->setAttitude(-2); // asleep: it stands in the way
		eng->getAIUpdateInterface()->aiMoveToPosition(Coord3D{ c0.x + 400.0f, c0.y, 0.0f }, CMD_FROM_PLAYER);
		bool dead = false;
		for (int f = 0; f < 150 && !dead; ++f)
		{
			r.frame(1);
			const Object *s = r.logic().findObjectByID(sid);
			dead = !s || s->isEffectivelyDead();
		}
		r.logic().findObjectByID(eng->getID())->kill(DAMAGE_UNRESISTABLE, 0);
		if (Object *s = r.logic().findObjectByID(sid))
		{
			s->kill(DAMAGE_UNRESISTABLE, 0);
		}
		r.frame(2);
		return dead;
	};
	CHECK(crushes("MordorGrond"));
	CHECK_FALSE(crushes("MordorBatteringRam"));
}

TEST_CASE("siege1: the stop lines of the siege deploy are S-4940 .. S-4943 and S-4945, and the siege modules have their runtime classes")
{
	const std::vector<std::string> lines = SiegeModules::stopLines();
	REQUIRE(lines.size() == 4u);
	CHECK(lines[0].rfind("[S-4940]", 0) == 0);
	CHECK(lines[1].rfind("[S-4941]", 0) == 0);
	CHECK(lines[2].rfind("[S-4942]", 0) == 0);
	CHECK(lines[3].rfind("[S-4945]", 0) == 0);
	CHECK(std::string(siegeExitStopLine()).rfind("[S-4943]", 0) == 0);
	CHECK(std::string(SiegeModules::portalStopLine()) == lines[0]);
}
