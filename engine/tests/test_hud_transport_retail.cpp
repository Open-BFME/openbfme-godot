// OpenBFME retail tests for GARRISON-2 (the transports and siege carriers in a live game with the W3D assets, so the PassengerBonePrefix bones are known): a mumak
// carries its Haradrim archer horde on its cargo bones, moves with it, the archers shoot from it, and when it dies they are thrown off and killed; the Dwarven battle
// wagon's upgrade makes its passengers and its death kills them; a battering ram and Grond run with their crews (the crew's speed, a crew member's loss); a siege
// tower takes a horde in and lets it out. They share the HUD tests' retail world (the file name sorts with the test_hud_* files) and SKIP when ROTWK_INSTALL /
// BFME2_INSTALL are unset.

#include "doctest.h"
#include "HudTestUtil.h"

#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Damage.h"
#include "GameLogic/AI/AIGarrisonStates.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GameLogicDispatch.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/TransportContainBehavior.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Contain/SiegeEngineContainRuntime.h"
#include "GameLogic/Object/Contain/TransportContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/System/ShroudManager.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace hudtest;

namespace
{
std::vector<Object *> ridersOf(Object &o)
{
	std::vector<Object *> out;
	if (ContainModuleInterface *c = o.getContain())
	{
		if (const ContainModuleInterface::ContainedItemsList *items = c->getContainedItemsList())
		{
			out.assign(items->begin(), items->end());
		}
	}
	return out;
}

float dist2D(const Object &a, const Object &b)
{
	const float dx = a.getPosition()->x - b.getPosition()->x, dy = a.getPosition()->y - b.getPosition()->y;
	return std::sqrt(dx * dx + dy * dy);
}

float healthOfMembers(Object &horde)
{
	float h = 0.0f;
	for (Object *m : ridersOf(horde))
	{
		h += (m->getBodyModule() && !m->isEffectivelyDead()) ? m->getBodyModule()->getHealth() : 0.0f;
	}
	return h;
}

void kill(Object &o)
{
	o.kill(0); // RW 0x698EC3: UNRESISTABLE damage of the maximum health
}

bool reportHas(GameLogic &logic, const char *id)
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

// the middle of the pathfinder's grid (the terrain's extent can be larger than the pathfind map)
Coord3D gridCentre(Rig &r, float clear)
{
	REQUIRE(r.logic().aiWorld() != nullptr);
	const ICoord2D *hi = r.logic().aiWorld()->pathfinder().getExtent();
	const Coord3D c = r.freeSpot((float)hi->x * 5.0f, (float)hi->y * 5.0f, clear);
	REQUIRE(c.x > 400.0f);
	REQUIRE(c.y > 400.0f);
	REQUIRE(c.x < (float)hi->x * 10.0f - 400.0f);
	REQUIRE(c.y < (float)hi->y * 10.0f - 400.0f);
	return c;
}

Coord3D centre(Rig &r, float clear)
{
	float mx = 0, my = 0;
	REQUIRE(r.logic().terrain() != nullptr);
	REQUIRE(r.logic().terrain()->getExtent(0, mx, my));
	return r.freeSpot(mx * 0.5f, my * 0.5f, clear);
}
} // namespace

TEST_CASE("transport retail: a mumak carries its Haradrim archer horde (InitialPayload) on its cargo bones, out of the world as a horde, its members held on the bones")
{
	if (!haveWorld("transport retail"))
	{
		return;
	}
	SharedWorld &s = shared();
	Rig r(s);
	const Coord3D c0 = centre(r, 400.0f);
	Object *mumak = r.make("MordorMumakil", c0.x, c0.y);
	r.frame(2);
	HordeTransportContain *t = dynamic_cast<HordeTransportContain *>(mumak->getContain());
	REQUIRE(t != nullptr);
	CHECK(t->payloadCreated());
	REQUIRE(t->getContainCount() == 1u);
	Object *horde = ridersOf(*mumak).front();
	CHECK(horde->getTemplate()->getName() == "MordorHaradrimArcherHordeOnMumakil");
	CHECK(horde->getContainedBy() == mumak);
	CHECK_FALSE(horde->isInWorld()); // RW 0x990E5F
	HordeContainInterface *h = horde->getContain()->getHordeContainInterface();
	REQUIRE(h != nullptr);
	CHECK(h->isGarrisoned()); // horde slot 0x124
	const std::vector<Object *> members = ridersOf(*horde);
	REQUIRE(!members.empty());
	std::set<std::string> bones;
	for (Object *m : members)
	{
		CHECK(m->isInWorld());
		CHECK((m->getDisabledMask() & (1u << 3)) == 0); // the horde came in whole: TransportContain::onContaining ran for the horde object, not its members (RW 0x87A677)
		CHECK(m->getPosition()->z > mumak->getPosition()->z + 10.0f); // on the howdah, above the beast's feet
		CHECK(dist2D(*m, *mumak) < 60.0f);
		bones.insert(t->riderBone(m->getID()));
	}
	CHECK(bones.size() == members.size()); // each member on its own bone
	CHECK(bones.count("B_CARGO001") == 1u); // the prefix is "B_CARGO0", the bones B_CARGO001 ..
	std::printf("  info: mumak payload %s: %zu members on bones %s .. %s, the first at height %.1f over the mumak\n", horde->getTemplate()->getName().c_str(), members.size(),
		bones.begin()->c_str(), bones.rbegin()->c_str(), members.front()->getPosition()->z - mumak->getPosition()->z);
	// the members ride along: after a move they keep their places on the beast
	std::map<ObjectID, Coord3D> rel;
	for (Object *m : members)
	{
		rel[m->getID()] = Coord3D{ m->getPosition()->x - mumak->getPosition()->x, m->getPosition()->y - mumak->getPosition()->y, 0.0f };
	}
	mumak->getAIUpdateInterface()->aiMoveToPosition(Coord3D{ c0.x + 300.0f, c0.y, 0.0f }, CMD_FROM_PLAYER);
	r.frame(40);
	const float moved = std::fabs(mumak->getPosition()->x - c0.x);
	CHECK(moved > 50.0f);
	for (Object *m : members)
	{
		CHECK(dist2D(*m, *mumak) < 60.0f);
	}
	CHECK(horde->getPosition()->x == mumak->getPosition()->x); // RW 0x87A51F: the horde object stands at the mumak
	std::printf("  info: the mumak moved %.0f, its riders kept within 60 of it\n", moved);
	CHECK(reportHas(r.logic(), "[S-1104]")); // the transports' stop reaches the game's report
}

TEST_CASE("transport retail: the mumak's archers shoot an enemy horde from its back; a dead mumak throws them off and they die (RW 0x87AA2C, GameLogicRandomValue(5, 10))")
{
	if (!haveWorld("transport retail"))
	{
		return;
	}
	SharedWorld &s = shared();
	Rig r(s);
	const Coord3D c0 = centre(r, 600.0f);
	// the mumak is the computer player's (a human player's units do not attack what its shroud fogs, S-565; the rig's map starts shrouded)
	Player *enemy = r.game->players().findPlayerWithName("Player_2");
	REQUIRE(enemy != nullptr);
	Object *mumak = r.make("MordorMumakil", c0.x, c0.y, enemy);
	Object *orcs = r.make("GondorFighterHorde", c0.x + 150.0f, c0.y);
	const ObjectID orcsId = orcs->getID();
	r.frame(5);
	Object *horde = ridersOf(*mumak).front();
	const std::vector<Object *> members = ridersOf(*horde);
	std::vector<ObjectID> memberIds;
	for (Object *m : members)
	{
		memberIds.push_back(m->getID());
	}
	const float before = healthOfMembers(*orcs);
	r.frame(150);
	orcs = r.logic().findObjectByID(orcsId);
	const float after = orcs && !orcs->isDestroyed() ? healthOfMembers(*orcs) : 0.0f;
	std::printf("  info: Gondor fighters beside the mumak: health %.0f -> %.0f\n", before, after);
	CHECK(after < before);
	// the mumak dies: EjectPassengersOnDeath (Yes) throws every member off its bone and kills it
	r.logic().random().clearCallLog();
	r.logic().random().enableCallLog(true);
	kill(*mumak);
	r.frame(1);
	size_t draws = 0;
	for (const auto &c : r.logic().random().callLog())
	{
		draws += (c.file == "HordeTransportContain.cpp" && c.line == 0x234) ? 1u : 0u;
	}
	r.logic().random().enableCallLog(false);
	size_t dead = 0;
	for (ObjectID id : memberIds) // the members may be gone: only their ids are read
	{
		Object *m = r.logic().findObjectByID(id);
		dead += (!m || m->isEffectivelyDead()) ? 1u : 0u;
	}
	std::printf("  info: the mumak died: %zu of %zu riders dead, %zu fling draws (5, 10)\n", dead, members.size(), draws);
	CHECK(dead == members.size());
	CHECK(draws == members.size()); // every member has a PhysicsBehavior
}

TEST_CASE("transport retail: the Dwarven battle wagon: an upgrade makes its passengers (UpgradeCreationTrigger RW 0x86B169), LOADED, and its death kills them")
{
	if (!haveWorld("transport retail"))
	{
		return;
	}
	SharedWorld &s = shared();
	Rig r(s, "map mp fall back 4p", "FactionDwarves");
	const Coord3D c0 = centre(r, 400.0f);
	Object *wagon = r.make("DwarvenBattleWagon", c0.x, c0.y);
	r.frame(2);
	TransportContain *t = dynamic_cast<TransportContain *>(wagon->getContain());
	REQUIRE(t != nullptr);
	CHECK(t->getContainCount() == 0u);
	static const int loaded = CombatNames::modelCondition("LOADED");
	CHECK_FALSE(wagon->testModelCondition(loaded));
	wagon->giveUpgrade("Upgrade_BattleWagonAxeThrowers");
	r.frame(2);
	const std::vector<Object *> riders = ridersOf(*wagon);
	REQUIRE(riders.size() == 2u);
	CHECK(riders[0]->getTemplate()->getName() == "DwarvenBattleWagonAxeThrower");
	CHECK(wagon->testModelCondition(loaded)); // RW 0x86A9AB
	CHECK(t->riderBone(riders[0]->getID()) == "PASS01");
	CHECK(t->riderBone(riders[1]->getID()) == "PASS02");
	CHECK(riders[0]->testStatus((unsigned)CombatNames::status("UNSELECTABLE")));
	// a second upgrade finds the wagon full (Slots 2): nothing more is made
	wagon->giveUpgrade("Upgrade_BattleWagonMenOfDale");
	r.frame(2);
	CHECK(t->getContainCount() == 2u);
	const ObjectID a = riders[0]->getID(), b = riders[1]->getID();
	kill(*wagon); // EjectPassengersOnDeath No: OpenContain RW 0x86718A destroys them
	r.frame(3);
	CHECK(r.logic().findObjectByID(a) == nullptr);
	CHECK(r.logic().findObjectByID(b) == nullptr);
}

// EGH_SlowTransport (HordeTransportContain, Slots 2, a SHIP; placed on dry ground, so RW's shore-spot branches (RW 0x6EFBB8) answer the ship's position): a horde
// boards it (MSG_ENTER's AI path: AIMoveToPositionAndEnterState to the entry point RW 0x8657B6, the horde enter state), comes out on the evacuate order (AI command 0x1B -> contain slot 0x80) and a second time dies with it. The units are the computer
// player's and the orders CMD_FROM_SCRIPT: the rig's shroud (S-565) refuses an AI order to a container the player has not seen (RW 0x82CBD5's shroud test).
TEST_CASE("transport retail: a horde boards a transport ship on dry ground (EGH_SlowTransport), comes out on the evacuate order; boarded again, it dies with the transport")
{
	if (!haveWorld("transport retail"))
	{
		return;
	}
	SharedWorld &s = shared();
	Rig r(s);
	const Coord3D c0 = gridCentre(r, 350.0f);
	Player *enemy = r.game->players().findPlayerWithName("Player_2");
	REQUIRE(enemy != nullptr);
	Object *boat = r.make("EGH_SlowTransport", c0.x, c0.y, enemy);
	Object *horde = r.make("MordorFighterHorde", c0.x + 200.0f, c0.y, enemy);
	r.frame(3);
	HordeTransportContain *t = dynamic_cast<HordeTransportContain *>(boat->getContain());
	REQUIRE(t != nullptr);
	const std::vector<Object *> members = ridersOf(*horde);
	REQUIRE(!members.empty());
	Coord3D entry;
	REQUIRE(t->getEntryOffset(entry));
	CHECK(entry.x == boat->getPosition()->x); // RW 0x8657B6: not a SHIP: the position
	REQUIRE(horde->getAIUpdateInterface()->aiEnter(boat, CMD_FROM_SCRIPT));
	int boarded = -1;
	for (int f = 0; f < 200 && boarded < 0; ++f)
	{
		r.frame(1);
		if (horde->getContainedBy() == boat)
		{
			boarded = f;
		}
	}
	REQUIRE_MESSAGE(boarded >= 0, "the horde never boarded");
	// the members walk in after the horde object (the horde enter state pushes them in)
	r.frame(60);
	size_t inside = 0;
	for (Object *m : members)
	{
		inside += !m->isInWorld() ? 1u : 0u;
	}
	std::printf("  info: boarded at frame %d; %zu of %zu members aboard, count %u\n", boarded, inside, members.size(), t->getContainCount());
	CHECK(inside == members.size());
	CHECK(t->getContainCount() == 1u);
	CHECK(horde->getContain()->getHordeContainInterface()->isGarrisoned());
	CHECK_FALSE(horde->isInWorld());
	// the transport is a SHIP (a water locomotor): on this land map it stays put; the riding along is the mumak's test above
	CHECK(dist2D(*horde, *boat) < 1.0f);
	// the evacuate order (AI command 0x1B -> contain slot 0x80): the horde comes out and is back in the world with its members
	t->orderAllPassengersToExit((int)CMD_FROM_SCRIPT);
	int out = -1;
	for (int f = 0; f < 200 && out < 0; ++f)
	{
		r.frame(1);
		if (!horde->getContainedBy())
		{
			out = f;
		}
	}
	REQUIRE_MESSAGE(out >= 0, "the horde never came out");
	r.frame(30);
	CHECK(horde->isInWorld());
	CHECK(t->getContainCount() == 0u);
	CHECK_FALSE(horde->getContain()->getHordeContainInterface()->isGarrisoned());
	size_t outside = 0;
	for (Object *m : members)
	{
		outside += m->isInWorld() ? 1u : 0u;
	}
	std::printf("  info: the horde came out after %d frames, %zu members in the world, %.0f from the transport\n", out, outside,
	            dist2D(*horde, *boat));
	CHECK(outside == members.size());
	// boarded again, the transport dies
	REQUIRE(horde->getAIUpdateInterface()->aiEnter(boat, CMD_FROM_SCRIPT));
	for (int f = 0; f < 300 && horde->getContainedBy() != boat; ++f)
	{
		r.frame(1);
	}
	REQUIRE(horde->getContainedBy() == boat);
	r.frame(60);
	const ObjectID hordeId = horde->getID(), boatId = boat->getID();
	std::vector<ObjectID> memberIds;
	for (Object *m : members)
	{
		memberIds.push_back(m->getID());
	}
	// EGH_SlowTransport has neither EjectPassengersOnDeath nor KillPassengersOnDeath: its death (RW 0x87AE0A) leaves the riders aboard and the deletion of the
	// wreck (OpenContain RW 0x86708F) destroys them
	CHECK_FALSE(t->openData().m_ejectPassengersOnDeath);
	CHECK_FALSE(t->openData().m_killPassengersOnDeath);
	kill(*boat);
	r.frame(30);
	CHECK(boat->isEffectivelyDead());
	CHECK(horde->getContainedBy() == boat); // still aboard the dying ship (its slow death on dry ground does not end: the wreck is deleted by hand below)
	r.logic().destroyObject(boat);
	r.frame(1);
	CHECK(r.logic().findObjectByID(boatId) == nullptr);
	r.frame(2);
	size_t dead = 0, inWorld = 0;
	for (ObjectID id : memberIds) // the members may be gone: only their ids are read
	{
		Object *m = r.logic().findObjectByID(id);
		dead += (!m || m->isEffectivelyDead()) ? 1u : 0u;
		inWorld += (m && m->isInWorld()) ? 1u : 0u;
	}
	std::printf("  info: the transport died with the horde aboard, its wreck deleted: %zu of %zu members gone, %zu in the world, horde object %s\n", dead,
	            members.size(), inWorld, r.logic().findObjectByID(hordeId) ? "left" : "gone");
	CHECK(inWorld == 0u);
	CHECK(r.logic().findObjectByID(hordeId) == nullptr);
	CHECK(dead == members.size());
}

TEST_CASE("siege retail: a battering ram runs with its crew (InitialCrew 6 on CREWBONE, ObjectStatusOfCrew, LOCOMOTORSET_CONTAINED, the crew's speed); a crew member lost")
{
	if (!haveWorld("siege retail"))
	{
		return;
	}
	SharedWorld &s = shared();
	Rig r(s, "map mp fall back 4p", "FactionIsengard");
	const Coord3D c0 = centre(r, 400.0f);
	Object *ram = r.make("IsengardBatteringRam", c0.x, c0.y);
	r.frame(2);
	SiegeEngineContain *se = dynamic_cast<SiegeEngineContain *>(ram->getContain());
	REQUIRE(se != nullptr);
	CHECK(se->crewCount() == 6);
	CHECK(ram->getContain()->getContainCount() == 0u); // the crew is not a rider
	REQUIRE(se->crewList()->size() == 6u);
	std::set<std::string> bones;
	for (Object *c : *se->crewList())
	{
		CHECK(c->getTemplate()->getName() == "IsengardRamCrew");
		CHECK(c->getContainedBy() == ram);
		CHECK(c->testStatus((unsigned)CombatNames::status("UNATTACKABLE"))); // ObjectStatusOfCrew
		bones.insert(se->riderBone(c->getID()));
		CHECK(dist2D(*c, *ram) < 60.0f);
	}
	CHECK(bones.size() == 6u);
	CHECK(bones.count("CREWBONE01") == 1u);
	CHECK(se->getCrewPowerMultiplier() == 6.0f); // RW 0x87ED45: 6 x 100%
	CHECK(reportHas(r.logic(), "[S-1105]"));
	// the ram moves; the crew keeps its bones
	ram->getAIUpdateInterface()->aiMoveToPosition(Coord3D{ c0.x + 300.0f, c0.y, 0.0f }, CMD_FROM_PLAYER);
	r.frame(30);
	const float moved = std::fabs(ram->getPosition()->x - c0.x);
	std::printf("  info: the ram moved %.0f in 30 frames with 6 crew\n", moved);
	CHECK(moved > 10.0f);
	for (Object *c : *se->crewList())
	{
		CHECK(dist2D(*c, *ram) < 60.0f);
	}
	// a crew member dies: it leaves the crew, the speed follows the count
	Object *first = se->crewList()->front();
	const ObjectID firstId = first->getID();
	kill(*first);
	for (int f = 0; f < 600 && r.logic().findObjectByID(firstId); ++f)
	{
		r.frame(1); // the slow death runs; the destroyed object leaves its container (Object::onDestroy)
	}
	CHECK(se->crewCount() == 5);
	CHECK(se->getCrewPowerMultiplier() == 5.0f);
	CHECK(r.logic().findObjectByID(firstId) == nullptr);
	// the ram dies: the crew is killed (KillPassengersOnDeath, DamagePercentToUnits 100%)
	std::vector<ObjectID> crew;
	for (Object *c : *se->crewList())
	{
		crew.push_back(c->getID());
	}
	kill(*ram);
	r.frame(3);
	for (ObjectID id : crew)
	{
		Object *c = r.logic().findObjectByID(id);
		CHECK((c == nullptr || c->isEffectivelyDead()));
	}
}

TEST_CASE("siege retail: Grond's crew speed is 50% per troll (SpeedPercentPerCrew); the siege tower (Slots 0) refuses an enter order; its crew steps off (HordeSiegeEngineContain)")
{
	if (!haveWorld("siege retail"))
	{
		return;
	}
	SharedWorld &s = shared();
	Rig r(s, "map mp fall back 4p", "FactionMordor");
	const Coord3D c0 = centre(r, 500.0f);
	Object *grond = r.make("MordorGrond", c0.x, c0.y);
	r.frame(2);
	SiegeEngineContain *g = dynamic_cast<SiegeEngineContain *>(grond->getContain());
	REQUIRE(g != nullptr);
	CHECK(g->crewCount() == 6);
	CHECK(g->getCrewPowerMultiplier() == 3.0f);
	const Coord3D c1 = r.freeSpot(c0.x + 600.0f, c0.y, 300.0f);
	Object *tower = r.make("MordorSiegeTower", c1.x, c1.y);
	Object *orcs = r.make("MordorFighterHorde", c1.x - 200.0f, c1.y);
	r.frame(10);
	HordeSiegeEngineContain *st = dynamic_cast<HordeSiegeEngineContain *>(tower->getContain());
	REQUIRE(st != nullptr);
	CHECK(st->crewCount() == 2);
	CHECK(tower->testModelCondition(CombatNames::modelCondition("SIEGE_CONTAIN"))); // RW 0x8804FF
	// Slots 0: a player's enter order is refused (canEnterObject RW 0x82CE99 asks the capacity; HordeTransportContain RW 0x87A4D0: 0 < count + 1). The infantry
	// use a docked tower through DynamicPortalBehaviour, not through the contain (S-1103: not ported)
	CHECK(st->isValidContainerFor(*orcs, false, false));
	CHECK_FALSE(st->isValidContainerFor(*orcs, true, false));
	CHECK_FALSE(GarrisonRules::canEnterObject(*orcs, tower, CMD_FROM_PLAYER, 0, false));
	r.game->dispatch().dispatch([&] {
		r.local->selection() = { orcs->getID() };
		GameMessage m(MSG_ENTER, r.index());
		m.appendObjectIDArgument(orcs->getID());
		m.appendObjectIDArgument(tower->getID());
		return m;
	}());
	r.frame(100);
	CHECK(orcs->getContainedBy() == nullptr);
	// the crew walks with the tower and steps off when ordered out (RW 0x8807CA: 50 away from the tower)
	REQUIRE(st->crewList()->size() == 2u);
	Object *troll = st->crewList()->front();
	CHECK(troll->testStatus((unsigned)CombatNames::status("UNATTACKABLE"))); // ObjectStatusOfCrew
	st->exitObjectViaDoor(troll, 0);
	CHECK(troll->getContainedBy() == nullptr);
	CHECK(st->crewCount() == 1);
	CHECK_FALSE(troll->testStatus((unsigned)CombatNames::status("UNATTACKABLE")));
	CHECK(st->crewCount() == 1);
}
