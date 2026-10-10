// OpenBFME retail tests for GARRISON-1 (garrisons in a live arena): which retail templates use the contain / turret / bridge modules; a Gondor archer horde ordered
// into a Gondor battle tower walks to it, the horde and then its members go in (hidden, held, INSIDE_GARRISON, the tower GARRISONED), they shoot a Mordor horde
// from the tower, the evacuate message brings them out, a destroyed tower ejects them; the run is deterministic. They share the HUD tests' retail world (the file
// name sorts with the test_hud_* files) and SKIP when ROTWK_INSTALL / BFME2_INSTALL are unset.

#include "doctest.h"
#include "HudTestUtil.h"

#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/AI/AIGarrisonStates.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/AI/GarrisonCommands.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Combat/TargetFinder.h"
#include "GameLogic/Combat/WeaponDelivery.h"
#include "GameLogic/Damage.h"
#include "GameLogic/Economy.h"
#include "GameLogic/EconomySettings.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GameLogicDispatch.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/InvisibilityModules.h"
#include "GameLogic/Object/Contain/GarrisonContainRuntime.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/PlayerCommands.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponNugget.h"

#include "PathfindTestUtil.h"
#include "RetailTestMount.h"

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace
{
using hudtest::SharedWorld;
using hudtest::shared;

bool haveWorld()
{
	return hudtest::haveWorld("garrison retail");
}

struct Arena
{
	std::unique_ptr<RetailObjectWorld::ContextScope> context;
	TeamFactory teams;
	PlayerList players;
	GameLogic logic;
	AIWorldConfig cfg;
	pathtest::SyntheticTerrain terrain;
	std::unique_ptr<AIWorld> ai;
	GameLogicDispatch dispatch;

	explicit Arena(SharedWorld &s, unsigned seed = 7)
		: context(s.world->enterContext())
		, players(s.world->nameKeys(), s.world->playerTemplates(), teams)
		, logic(s.world->things(), s.world->modules(), players, RandomAlgorithm::ZH_CarryChain)
		, terrain(120, 120)
		, dispatch(logic)
	{
		SkirmishSetup setup;
		setup.startingMoney = 0;
		setup.players.push_back({ "A", "FactionMen", true, 0, 0, 1 });
		setup.players.push_back({ "B", "FactionMordor", false, 1, 0, 2 });
		const std::vector<std::string> errors = players.setupSkirmish(setup);
		REQUIRE_MESSAGE(errors.empty(), (errors.empty() ? "" : errors[0]));
		std::string err;
		REQUIRE_MESSAGE(GameLogicSettingsLoader::load(*s.mount->fs, logic.settings(), &err), err);
		REQUIRE_MESSAGE(EconomySettings::load(*s.mount->fs, logic.economy().settings(), &err), err);
		logic.random().seedRandom(seed);
		logic.economy().initAllCommandPoints();
		REQUIRE_MESSAGE(AIWorldConfigLoader::load(*s.mount->fs, cfg, &err), err);
		ai = std::make_unique<AIWorld>(logic, cfg, s.world->iniMacros());
		ai->attach();
		ai->newMap(terrain);
		GarrisonCommands::registerHandlers(dispatch);
	}
	~Arena()
	{
		logic.reset();
		ai.reset();
	}
	Player *player(int i) { return players.findPlayerWithName(i == 0 ? "A" : "B"); }
	Object *place(const char *templateName, int side, float x, float y, float angle = 0.0f)
	{
		const ThingTemplate *t = shared().world->things().findTemplate(templateName);
		REQUIRE_MESSAGE(t != nullptr, "retail template " << templateName);
		Object *o = logic.newObject(t, player(side)->getDefaultTeam(), ObjectStatusMaskType{});
		REQUIRE_MESSAGE(o != nullptr, templateName);
		Coord3D p{ x, y, 0.0f };
		o->setPosition(&p);
		o->setOrientation(angle);
		return o;
	}
	void run(int frames)
	{
		for (int i = 0; i < frames; ++i)
		{
			logic.runLogicFrame();
		}
	}
	void send(int side, int type, std::vector<ObjectID> selection, std::vector<ObjectID> args)
	{
		player(side)->selection() = selection;
		GameMessage m(type, player(side)->getPlayerIndex());
		for (ObjectID id : args)
		{
			m.appendObjectIDArgument(id);
		}
		dispatch.dispatch(m);
	}
};

std::vector<Object *> membersOf(Object &horde)
{
	std::vector<Object *> out;
	if (ContainModuleInterface *c = horde.getContain())
	{
		if (const ContainModuleInterface::ContainedItemsList *items = c->getContainedItemsList())
		{
			out.assign(items->begin(), items->end());
		}
	}
	return out;
}

GarrisonContain *garrisonOf(Object &o)
{
	return dynamic_cast<GarrisonContain *>(o.getContain());
}

float healthOf(Object &horde)
{
	float h = 0.0f;
	for (Object *m : membersOf(horde))
	{
		h += (m->getBodyModule() && !m->isEffectivelyDead()) ? m->getBodyModule()->getHealth() : 0.0f;
	}
	return h;
}

bool garrisonDone(Object &tower, Object &horde)
{
	HordeContainInterface *h = horde.getContain() ? horde.getContain()->getHordeContainInterface() : nullptr;
	return tower.getContain()->getContainCount() == 1 && horde.getContainedBy() == &tower && h && h->allMembersEntered() && !membersOf(horde).empty();
}

struct GarrisonRun
{
	int framesToEnter = -1;
	int membersInside = 0;
	float mordorBefore = 0.0f, mordorAfter = 0.0f;
	unsigned pointsTaken = 0;
	std::uint32_t hash = 0;
};

// the archers enter the tower; a Mordor horde then stands in their range for `fightFrames`
GarrisonRun garrisonAndFight(Arena &a, Object *&tower, Object *&archers, Object *&orcs, int fightFrames)
{
	GarrisonRun r;
	tower = a.place("GondorKeep", 0, 600.0f, 600.0f);
	archers = a.place("GondorArcherHorde", 0, 450.0f, 600.0f);
	a.run(20); // the members settle on their slots
	a.send(0, MSG_ENTER, { archers->getID() }, { archers->getID(), tower->getID() });
	for (int f = 0; f < 600; ++f)
	{
		a.run(1);
		if (garrisonDone(*tower, *archers))
		{
			r.framesToEnter = f + 1;
			break;
		}
	}
	r.membersInside = (int)membersOf(*archers).size();
	if (r.framesToEnter < 0)
	{
		return r;
	}
	orcs = a.place("MordorFighterHorde", 1, 780.0f, 600.0f);
	const ObjectID orcsId = orcs->getID();
	a.run(10);
	r.mordorBefore = healthOf(*orcs);
	a.run(fightFrames);
	// a horde whose members all died is deleted by the destroy list: look it up again (never keep the pointer across frames)
	orcs = a.logic.findObjectByID(orcsId);
	r.mordorAfter = orcs && !orcs->isDestroyed() ? healthOf(*orcs) : 0.0f;
	r.pointsTaken = (unsigned)garrisonOf(*tower)->stats().pointsTaken;
	r.hash = a.logic.computeStateHash();
	return r;
}

// a lone unit of side 0 ordered into the tower; true once it is inside
bool enterAlone(Arena &a, Object *unit, Object *tower)
{
	a.send(0, MSG_ENTER, { unit->getID() }, { unit->getID(), tower->getID() });
	for (int f = 0; f < 600 && unit->getContainedBy() != tower; ++f)
	{
		a.run(1);
	}
	return unit->getContainedBy() == tower;
}

bool contains(const std::vector<Object *> &v, const Object *o)
{
	return std::find(v.begin(), v.end(), o) != v.end();
}
} // namespace

TEST_CASE("garrison retail: the 2.01 templates that use each contain / bridge module; GarrisonContain and HordeGarrisonContain run for real")
{
	if (!haveWorld())
	{
		return;
	}
	SharedWorld &s = shared();
	std::map<std::string, int> count;
	const std::set<std::string> classes = { "HordeGarrisonContain", "GarrisonContain", "TransportContain", "HordeTransportContain", "SiegeEngineContain",
		"HordeSiegeEngineContain", "TunnelContain", "BridgeBehavior", "HordeTransportContainDamage", "RiderChangeContain", "OpenContain" };
	for (const ThingTemplate *t : s.world->things().templates())
	{
		for (const ThingTemplate::Nugget &n : t->behaviorModules().nuggets())
		{
			if (classes.count(n.name))
			{
				++count[n.name];
			}
		}
	}
	for (const auto &kv : count)
	{
		MESSAGE(kv.first << " " << kv.second);
	}
	// the census (inheritance resolved, every template of the mounted 2.01 data)
	CHECK(count["HordeGarrisonContain"] == 23);
	CHECK(count["GarrisonContain"] == 2);
	CHECK(count["TransportContain"] == 23);
	CHECK(count["HordeTransportContain"] == 13);
	CHECK(count["TunnelContain"] == 6);
	CHECK(count["SiegeEngineContain"] == 4);
	CHECK(count["HordeSiegeEngineContain"] == 1);
	CHECK(count["BridgeBehavior"] == 1);
	CHECK(s.world->modules().findModuleTemplate("HordeGarrisonContain", MODULETYPE_BEHAVIOR)->portedModule);
	CHECK(s.world->modules().findModuleTemplate("GarrisonContain", MODULETYPE_BEHAVIOR)->portedModule);
	for (const char *ported : { "TransportContain", "HordeTransportContain", "SiegeEngineContain", "HordeSiegeEngineContain", "TunnelContain" }) // lane GARRISON-2
	{
		CHECK(s.world->modules().findModuleTemplate(ported, MODULETYPE_BEHAVIOR)->portedModule);
	}
}

TEST_CASE("garrison retail: Gondor archers enter a battle tower, shoot a Mordor horde from it, come out on the evacuate message")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared());
	Object *tower = nullptr, *archers = nullptr, *orcs = nullptr;
	const GarrisonRun r = garrisonAndFight(a, tower, archers, orcs, 150);
	REQUIRE_MESSAGE(r.framesToEnter > 0, "the archers never got in");
	MESSAGE("entered after " << r.framesToEnter << " frames, " << r.membersInside << " members; Mordor health " << r.mordorBefore << " -> " << r.mordorAfter
		<< "; points taken " << r.pointsTaken);
	GarrisonContain *g = garrisonOf(*tower);
	REQUIRE(g);
	CHECK(g->isHordeGarrison());
	// the horde object: inside, out of the world, unselectable, garrisoned; the tower: GARRISONED, CAN_ATTACK
	CHECK(archers->getContainedBy() == tower);
	CHECK_FALSE(archers->isInWorld());
	CHECK(archers->testStatus((unsigned)CombatNames::status("UNSELECTABLE")));
	CHECK(archers->getContain()->getHordeContainInterface()->isGarrisoned());
	CHECK(tower->testModelCondition(CombatNames::modelCondition("GARRISONED")));
	CHECK(tower->testStatus((unsigned)CombatNames::status("CAN_ATTACK")));
	// every member: back in its horde, held, INSIDE_GARRISON, the GARRISONED weapon bonus, hidden (ENCLOSED)
	REQUIRE(r.membersInside > 0);
	for (Object *m : membersOf(*archers))
	{
		CHECK(m->getContainedBy() == archers);
		CHECK((m->getDisabledMask() & (1u << 3)) != 0u);
		CHECK(m->testStatus((unsigned)CombatNames::statuses().insideGarrison));
		CHECK((m->weaponBonusConditionMask() & 1u) != 0u);
		CHECK(m->isDrawableHidden());
		// ObjectStatusOfContained goes to the horde object (OpenContain's add, RW 0x6901AE), not to the members (they come through RW 0x990DEA)
		CHECK_FALSE(m->testStatus((unsigned)CombatNames::status("ENCLOSED")));
	}
	CHECK(archers->testStatus((unsigned)CombatNames::status("ENCLOSED")));
	CHECK(archers->testStatus((unsigned)CombatNames::status("CAN_ATTACK")));
	// they hurt the orcs from inside; their arrows leave from the tower (the ENCLOSED container lends its transform, RW 0x6CAC62)
	CHECK(r.mordorAfter < r.mordorBefore);
	CHECK(a.logic.combat().counters().garrisonLaunches > 0u);
	CHECK(a.logic.combat().counters().garrisonLaunchUnported == 0u);
	// the evacuate message (the tower selected) brings the horde out; the members join it again, shown, no longer held
	a.send(0, MSG_EVACUATE, { tower->getID() }, {});
	for (int f = 0; f < 100 && archers->getContainedBy(); ++f)
	{
		a.run(1);
	}
	CHECK(archers->getContainedBy() == nullptr);
	CHECK(archers->isInWorld());
	CHECK_FALSE(archers->getContain()->getHordeContainInterface()->isGarrisoned());
	CHECK(tower->getContain()->getContainCount() == 0u);
	CHECK_FALSE(tower->testModelCondition(CombatNames::modelCondition("GARRISONED")));
	for (Object *m : membersOf(*archers))
	{
		CHECK((m->getDisabledMask() & (1u << 3)) == 0u);
		CHECK_FALSE(m->testStatus((unsigned)CombatNames::statuses().insideGarrison));
		CHECK_FALSE(m->isDrawableHidden());
		CHECK(m->isInWorld());
	}
	CHECK(GarrisonCommands::stats().enterOrders > 0u);
	// the stops of the lane reach the game's report (S-1100 .. S-1103)
	const GameLogic::Report rep = a.logic.report();
	for (const char *id : { "[S-1100]", "[S-1101]", "[S-1102]", "[S-1103]" })
	{
		bool found = false;
		for (const std::string &line : rep.stops)
		{
			found = found || line.rfind(id, 0) == 0;
		}
		CHECK_MESSAGE(found, id);
	}
	for (const std::string &line : GarrisonContain::stopLines())
	{
		CHECK(std::find(rep.stops.begin(), rep.stops.end(), line) != rep.stops.end());
	}
}

TEST_CASE("garrison retail: a destroyed tower ejects its garrison (EjectPassengersOnDeath, RW 0x867120)")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared());
	Object *tower = nullptr, *archers = nullptr, *orcs = nullptr;
	const GarrisonRun r = garrisonAndFight(a, tower, archers, orcs, 5);
	REQUIRE(r.framesToEnter > 0);
	const size_t members = membersOf(*archers).size();
	tower->kill(0);
	a.run(2);
	CHECK(archers->getContainedBy() == nullptr);
	CHECK(archers->isInWorld());
	CHECK(membersOf(*archers).size() == members);
	CHECK_FALSE(archers->getContain()->getHordeContainInterface()->isGarrisoned());
}

TEST_CASE("garrison retail: a tower deleted without a death lets its garrison go (S-1100 inference)")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared());
	Object *tower = nullptr, *archers = nullptr, *orcs = nullptr;
	const GarrisonRun r = garrisonAndFight(a, tower, archers, orcs, 1);
	REQUIRE(r.framesToEnter > 0);
	a.logic.destroyObject(tower);
	a.run(2);
	CHECK(archers->getContainedBy() == nullptr);
	CHECK(archers->isInWorld());
	CHECK_FALSE(archers->getContain()->getHordeContainInterface()->isGarrisoned());
	for (Object *m : membersOf(*archers))
	{
		CHECK((m->getDisabledMask() & (1u << 3)) == 0u);
		CHECK_FALSE(m->isDrawableHidden());
		CHECK_FALSE(m->testStatus((unsigned)CombatNames::statuses().insideGarrison));
	}
}

TEST_CASE("garrison retail: who may enter: an enemy horde and cavalry are refused, the tower holds two hordes (RW 0x86603B, 0x87D252)")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared());
	Object *tower = a.place("GondorKeep", 0, 600.0f, 600.0f);
	Object *archers = a.place("GondorArcherHorde", 0, 450.0f, 600.0f);
	Object *orcs = a.place("MordorFighterHorde", 1, 750.0f, 600.0f);
	Object *knights = a.place("GondorKnightHorde", 0, 450.0f, 450.0f);
	ContainModuleInterface *c = tower->getContain();
	CHECK(c->isGarrisonable());
	CHECK(c->isValidContainerFor(*archers, true, false));
	CHECK_FALSE(c->isValidContainerFor(*orcs, true, false));   // AllowEnemiesInside = No
	CHECK_FALSE(c->isValidContainerFor(*knights, true, false)); // GENERIC_FACTION_GARRISONABLE: -CAVALRY
	CHECK(GarrisonRules::canEnterObject(*archers, tower, CMD_FROM_PLAYER, 0, false));
	CHECK_FALSE(GarrisonRules::canEnterObject(*orcs, tower, CMD_FROM_PLAYER, 0, false));
	CHECK(garrisonOf(*tower)->getContainMax() == 2);
}

TEST_CASE("garrison retail: two garrison runs give the same state hash")
{
	if (!haveWorld())
	{
		return;
	}
	std::uint32_t hashes[2] = { 0, 0 };
	for (int i = 0; i < 2; ++i)
	{
		Arena a(shared());
		Object *tower = nullptr, *archers = nullptr, *orcs = nullptr;
		hashes[i] = garrisonAndFight(a, tower, archers, orcs, 60).hash;
	}
	CHECK(hashes[0] != 0u);
	CHECK(hashes[0] == hashes[1]);
}

TEST_CASE("garrison retail: an enemy detector marks a stealth unit garrisoned in a tower through the tower (StealthDetectorUpdate's occupant path, DetectionRate + 2)")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared());
	Object *tower = a.place("GondorKeep", 0, 600.0f, 600.0f);
	Object *warrior = a.place("LindonElvenWarrior", 0, 450.0f, 600.0f); // a lone unit with its own StealthUpdate
	REQUIRE(StealthUpdate::of(*warrior) != nullptr);
	a.run(5);
	a.send(0, MSG_ENTER, { warrior->getID() }, { warrior->getID(), tower->getID() });
	for (int f = 0; f < 600 && warrior->getContainedBy() != tower; ++f)
	{
		a.run(1);
	}
	REQUIRE(warrior->getContainedBy() == tower);
	CHECK_FALSE(warrior->isInWorld()); // ENCLOSED: out of the world (RW 0x865D3D)
	CHECK(warrior->isDrawableHidden());
	Object *totem = a.place("WildSkullTotem", 1, 700.0f, 600.0f);
	StealthDetectorUpdate *det = dynamic_cast<StealthDetectorUpdate *>(totem->findModule("StealthDetectorUpdate"));
	REQUIRE(det != nullptr);
	const auto *data = dynamic_cast<const StealthDetectorUpdateModuleData *>(det->getModuleData());
	REQUIRE(data != nullptr);
	a.run(1);
	const unsigned now = a.logic.getFrame();
	det->update();
	// the tower is a candidate; its contain is garrisonable (slot 0x10), so its occupant is marked for DetectionRate + 2 (RW 0x8A680C / 0x8A6823)
	CHECK(warrior->testStatus((unsigned)CombatNames::status("DETECTED")));
	CHECK(StealthUpdate::of(*warrior)->detectionExpiresFrame() == now + data->m_detectionRate + 2);
}

TEST_CASE("garrison retail: a rider out of the world is no target, takes no splash, collides with nothing and still shoots from the tower (r2)")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared());
	Object *tower = a.place("GondorKeep", 0, 600.0f, 600.0f);
	Object *rider = a.place("LindonElvenWarrior", 0, 450.0f, 600.0f);
	Object *outside = a.place("LindonElvenWarrior", 0, 600.0f, 680.0f); // the same unit beside the tower, in the world: the control
	a.run(5);
	// a TargetFinder built while the rider is still outside: its entry into the tower (a contain's world exit) makes the grid stale within the frame
	TargetFinder finder(a.logic);
	std::vector<Object *> before;
	finder.collectInRadius(*rider->getPosition(), 50.0f, before);
	CHECK(contains(before, rider));
	REQUIRE(enterAlone(a, rider, tower));
	REQUIRE_FALSE(rider->isInWorld()); // ENCLOSED: RW 0x865D3D -> 0x68C18F
	CHECK(rider->testStatus((unsigned)CombatNames::status("INSIDE_GARRISON")));
	CHECK_FALSE(CombatQueries::isAttackable(*rider));
	std::vector<Object *> hits;
	finder.collectInRadius(*tower->getPosition(), 300.0f, hits);
	CHECK_FALSE(contains(hits, rider));
	CHECK(contains(hits, outside));
	// an enemy beside the tower: its target scan finds the tower or the unit outside, never the rider
	Object *enemy = a.place("MordorArcher", 1, 640.0f, 600.0f);
	const Object *found = a.logic.combat().targets().findClosestEnemy(*enemy, 400.0f, TargetFinder::MEMBERS_ONLY | TargetFinder::ALLOW_STRUCTURES, CMD_FROM_AI);
	CHECK(found != rider);
	// collisions: the enemy put on the rider's position collides with nothing (the rider is not in the partition, ZH PartitionManager::processCollisions)
	Coord3D at = *rider->getPosition();
	enemy->setPosition(&at);
	a.ai->processCollisions();
	CHECK(a.ai->collisionPairsLastFrame() == 0u);
	enemy->setPosition(outside->getPosition());
	a.ai->processCollisions();
	CHECK(a.ai->collisionPairsLastFrame() > 0u); // the control collides
	a.logic.destroyObject(enemy);
	a.run(1);
	// a splash (a DamageNugget with a Radius, RW 0x90DEF0) on the tower: the unit outside is hurt, the rider is not
	WeaponTemplate splash;
	auto nugget = std::make_shared<DamageNugget>();
	nugget->m_damage = 25.0f;
	nugget->m_radius = 200.0f;
	nugget->m_damageType = 0;
	splash.m_nuggets.push_back(nugget);
	const float riderBefore = rider->getBodyModule()->getHealth(), outsideBefore = outside->getBodyModule()->getHealth();
	DeliverNuggets(a.logic, INVALID_ID, splash, WeaponBonus{}, nullptr, tower->getPosition(), true, nullptr);
	CHECK(outside->getBodyModule()->getHealth() < outsideBefore);
	CHECK(rider->getBodyModule()->getHealth() == riderBefore);
	// it still shoots from the tower: a Mordor horde in range loses health while the rider stays inside
	a.logic.destroyObject(outside);
	Object *orcs = a.place("MordorFighterHorde", 1, 780.0f, 600.0f);
	const ObjectID orcsId = orcs->getID();
	a.run(10);
	const float orcsBefore = healthOf(*orcs);
	const unsigned launches = a.logic.combat().counters().garrisonLaunches;
	a.run(300);
	orcs = a.logic.findObjectByID(orcsId);
	const float orcsAfter = orcs && !orcs->isDestroyed() ? healthOf(*orcs) : 0.0f;
	MESSAGE("orcs " << orcsBefore << " -> " << orcsAfter << ", garrison launches " << a.logic.combat().counters().garrisonLaunches - launches);
	CHECK(rider->getContainedBy() == tower);
	CHECK(orcsAfter < orcsBefore);
	// out again (RW 0x865D3D add): the grid sees it in the same frame
	tower->getContain()->removeFromContain(rider);
	CHECK(rider->isInWorld());
	std::vector<Object *> again;
	finder.collectInRadius(*tower->getPosition(), 300.0f, again);
	CHECK(contains(again, rider));
	CHECK(CombatQueries::isAttackable(*rider));
}

TEST_CASE("garrison retail: a horde's members stay in the world inside a tower (RW 0x87CD76 -> 0x87BEC8 holds them), the horde object leaves it (RW 0x990E5F)")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared());
	Object *tower = nullptr, *archers = nullptr, *orcs = nullptr;
	REQUIRE(garrisonAndFight(a, tower, archers, orcs, 0).framesToEnter > 0);
	CHECK_FALSE(archers->isInWorld());
	CHECK_FALSE(CombatQueries::isAttackable(*archers));
	// lane INPUT-1 r2: a garrisoned horde is not handed out by a control group's recall (Squad::getLiveObjects RW 0x8DB103 -> Object::isSelectable RW 0x68DE58)
	CHECK_FALSE(PlayerCommands::isSelectable(*archers));
	for (Object *m : membersOf(*archers))
	{
		CHECK(m->isInWorld());
		CHECK((m->getDisabledMask() & (1u << 3)) != 0); // DISABLED_HELD (RW 0x692432(3))
		CHECK(m->isDrawableHidden());
	}
}

TEST_CASE("garrison retail: a HELD unit runs its AI (RW 0x855830) but is not doing ground movement (RW 0x667144); a held garrison member stays on its point")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared());
	Object *unit = a.place("LindonElvenWarrior", 0, 250.0f, 250.0f);
	a.run(3);
	AIUpdateInterface *ai = unit->getAIUpdateInterface();
	REQUIRE(ai != nullptr);
	CHECK(ai->isDoingGroundMovement());
	unit->setDisabled(3, (UnsignedInt)UPDATE_SLEEP_FOREVER);
	CHECK_FALSE(ai->isDoingGroundMovement()); // RW 0x667163: object + 0x1C8 bit 3
	unit->clearDisabled(3);
	CHECK(ai->isDoingGroundMovement());
	// the members of a garrisoned horde are held and fight from their points: none walks off while the Mordor horde is in range
	Object *tower = nullptr, *archers = nullptr, *orcs = nullptr;
	REQUIRE(garrisonAndFight(a, tower, archers, orcs, 0).framesToEnter > 0);
	std::map<ObjectID, Coord3D> at;
	for (Object *m : membersOf(*archers))
	{
		CHECK_FALSE(m->getAIUpdateInterface()->isDoingGroundMovement());
		at[m->getID()] = *m->getPosition();
	}
	a.place("MordorFighterHorde", 1, 780.0f, 600.0f);
	a.run(120);
	for (Object *m : membersOf(*archers))
	{
		CHECK(m->getPosition()->x == at[m->getID()].x);
		CHECK(m->getPosition()->y == at[m->getID()].y);
	}
}

TEST_CASE("garrison retail: a plain GarrisonContain refuses NO_GARRISON objects (none in 2.01) and, with the capacity test, a full container (RW 0x87B935)")
{
	if (!haveWorld())
	{
		return;
	}
	SharedWorld &s = shared();
	static const int noGarrison = CombatNames::kindOf("NO_GARRISON");
	static const int infantry = CombatNames::kindOf("INFANTRY");
	const ThingTemplate *keepTemplate = nullptr;
	for (const ThingTemplate *t : s.world->things().templates())
	{
		for (const ThingTemplate::Nugget &n : t->behaviorModules().nuggets())
		{
			if (n.name == "GarrisonContain" && !keepTemplate)
			{
				keepTemplate = t;
			}
		}
	}
	REQUIRE(keepTemplate != nullptr);
	Arena a(shared());
	Object *keep = a.place(keepTemplate->getName().c_str(), 0, 600.0f, 600.0f);
	GarrisonContain *g = garrisonOf(*keep);
	REQUIRE(g != nullptr);
	REQUIRE(keep->getContain()->getHordeContainInterface() == nullptr);
	const int max = g->getContainMax();
	MESSAGE(keepTemplate->getName() << ": ContainMax " << max);
	// the NO_GARRISON objects RW 0x87B8C5 would let in: the wrapper refuses every one
	int noGarrisonTested = 0, noGarrisonKinds = 0, infantryKinds = 0;
	for (const ThingTemplate *t : s.world->things().templates())
	{
		const KindOfMaskType k = ObjectTemplateInfoBuilder::build(*t).kindOf;
		infantryKinds += ((k[(size_t)infantry >> 5] >> (infantry & 31)) & 1u) ? 1 : 0;
		if (((k[(size_t)noGarrison >> 5] >> (noGarrison & 31)) & 1u) && noGarrisonTested < 40)
		{
			++noGarrisonKinds;
			Object *o = a.logic.newObject(t, a.player(0)->getDefaultTeam(), ObjectStatusMaskType{});
			if (!o)
			{
				continue;
			}
			Coord3D p{ 500.0f, 600.0f, 0.0f };
			o->setPosition(&p);
			if (g->garrisonAllows(*o))
			{
				++noGarrisonTested;
				CHECK_FALSE(g->isValidContainerFor(*o, false, false));
			}
			a.logic.destroyObject(o);
		}
	}
	MESSAGE("INFANTRY templates " << infantryKinds << ", NO_GARRISON templates " << noGarrisonKinds << ", objects RW 0x87B8C5 allows: " << noGarrisonTested);
	// no 2.01 template has KindOf NO_GARRISON: the wrapper's first test never refuses a retail object (the loop above checks any that would appear)
	CHECK(infantryKinds > 100);
	CHECK(noGarrisonKinds == 0);
	// the capacity: fill it with lone soldiers; a full container refuses with the capacity test only
	std::vector<Object *> in;
	const int fill = max > 0 ? max : 0;
	for (int i = 0; i < fill; ++i)
	{
		Object *u = a.place("LindonElvenWarrior", 0, 500.0f, 500.0f + 10.0f * (float)i);
		REQUIRE(g->isValidContainerFor(*u, true, false));
		g->addToContain(u);
		in.push_back(u);
	}
	Object *last = a.place("LindonElvenWarrior", 0, 450.0f, 600.0f);
	CHECK(g->garrisonAllows(*last));
	CHECK(g->isValidContainerFor(*last, false, false));
	CHECK_FALSE(g->isValidContainerFor(*last, true, false)); // count < ContainMax fails (with ContainMax -1 for every count)
	for (Object *u : in)
	{
		g->removeFromContain(u);
	}
}

TEST_CASE("garrison retail: the enter and exit orders pass aiDoCommand's gate (RW 0x667174) like every order: PreventPlayerCommands refuses a player's MSG_ENTER")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared());
	Object *tower = a.place("GondorKeep", 0, 600.0f, 600.0f);
	Object *unit = a.place("LindonElvenWarrior", 0, 450.0f, 600.0f);
	a.run(5);
	AIUpdateInterface *ai = unit->getAIUpdateInterface();
	REQUIRE(ai != nullptr);
	ai->setPreventPlayerCommands(true); // RW + 0x3C5 (an emotion nugget with PreventPlayerCommands)
	const unsigned orders = GarrisonCommands::stats().enterOrders;
	a.send(0, MSG_ENTER, { unit->getID() }, { unit->getID(), tower->getID() });
	CHECK(GarrisonCommands::stats().enterOrders == orders); // refused before anything of the unit changed
	CHECK_FALSE(ai->aiEnter(tower, CMD_FROM_PLAYER));
	a.run(200);
	CHECK(unit->getContainedBy() == nullptr);
	// a script's order is not a player's: it passes
	CHECK(ai->aiEnter(tower, CMD_FROM_SCRIPT));
	ai->setPreventPlayerCommands(false);
	for (int f = 0; f < 600 && unit->getContainedBy() != tower; ++f)
	{
		a.run(1);
	}
	REQUIRE(unit->getContainedBy() == tower);
	// out again: a player's exit is refused while the gate is closed, passes once it opens
	ai->setPreventPlayerCommands(true);
	ai->aiExit(tower, CMD_FROM_PLAYER);
	a.run(60);
	CHECK(unit->getContainedBy() == tower);
	ai->setPreventPlayerCommands(false);
	ai->aiExit(tower, CMD_FROM_PLAYER);
	for (int f = 0; f < 300 && unit->getContainedBy() == tower; ++f)
	{
		a.run(1);
	}
	CHECK(unit->getContainedBy() == nullptr);
}
