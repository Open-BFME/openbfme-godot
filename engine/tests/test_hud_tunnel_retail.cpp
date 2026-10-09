// OpenBFME retail tests for GARRISON-2's TunnelContain (the player's tunnel network, RW 0x880E29 .. 0x881404 and the TunnelTracker RW 0x8FA152 .. 0x8FA4E8): a Goblin
// horde enters one WildMineShaft and comes out of another, heals inside, and comes out when the last tunnel dies. They share the HUD tests' retail world and SKIP
// when ROTWK_INSTALL / BFME2_INSTALL are unset. The units are the computer player's and the orders CMD_FROM_SCRIPT (the rig's shroud refuses an AI order to a
// container the player has not seen: S-565).

#include "doctest.h"
#include "HudTestUtil.h"

#include "Common/Player.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/Contain/TunnelContainRuntime.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace hudtest;

namespace
{
std::vector<Object *> membersOf(Object &o)
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

Coord3D gridSpot(Rig &r, float fx, float fy, float clear)
{
	const ICoord2D *hi = r.logic().aiWorld()->pathfinder().getExtent();
	return r.freeSpot((float)hi->x * 10.0f * fx, (float)hi->y * 10.0f * fy, clear);
}

TunnelContain *tunnelOf(Object &o)
{
	return dynamic_cast<TunnelContain *>(o.getContain());
}

bool enter(Rig &r, Object &horde, Object &tunnel, int frames)
{
	REQUIRE(horde.getAIUpdateInterface()->aiEnter(&tunnel, CMD_FROM_SCRIPT));
	for (int f = 0; f < frames; ++f)
	{
		r.frame(1);
		HordeContainInterface *h = horde.getContain() ? horde.getContain()->getHordeContainInterface() : nullptr;
		// the horde inside, every member back in it and held at the tunnel (GarrisonContain::onContaining: DISABLED_HELD)
		bool hidden = horde.getContainedBy() != nullptr && h && h->allMembersEntered();
		for (Object *m : membersOf(horde))
		{
			hidden = hidden && (m->getDisabledMask() & (1u << 3)) != 0;
		}
		if (hidden)
		{
			return true;
		}


	}
	return false;
}
} // namespace

TEST_CASE("tunnel retail: the 2.01 tunnels are TunnelContain (a HordeGarrisonContain), MaxTunnelCapacity from gamedata.ini, a finished tunnel joins its player's network")
{
	if (!haveWorld("tunnel retail"))
	{
		return;
	}
	SharedWorld &s = shared();
	Rig r(s);
	const int capacity = r.logic().settings().maxTunnelCapacity;
	std::printf("  info: MaxTunnelCapacity %d\n", capacity);
	CHECK(capacity > 0);
	Player *enemy = r.game->players().findPlayerWithName("Player_2");
	REQUIRE(enemy != nullptr);
	const Coord3D c0 = gridSpot(r, 0.3f, 0.5f, 300.0f);
	Object *a = r.make("WildMineShaft", c0.x, c0.y, enemy);
	r.frame(2);
	TunnelContain *ta = tunnelOf(*a);
	REQUIRE(ta != nullptr);
	CHECK(ta->isHordeGarrison());
	const unsigned before = enemy->tunnelTracker().tunnelCount();
	a->friend_onBuildComplete(); // a finished structure (the construction's last step, RW 0x68D252)
	CHECK(ta->isRegistered());
	CHECK(enemy->tunnelTracker().tunnelCount() == before + 1u);
	a->friend_onBuildComplete(); // only once (+ 0x9E4)
	CHECK(enemy->tunnelTracker().tunnelCount() == before + 1u);
	CHECK(ta->getContainMax() == capacity);
	r.logic().destroyObject(a);
	r.frame(2);
	CHECK(enemy->tunnelTracker().tunnelCount() == before);
	bool reported = false;
	for (const std::string &line : r.logic().report().stops)
	{
		reported = reported || line.rfind("[S-1107]", 0) == 0;
	}
	CHECK(reported);
}

TEST_CASE("tunnel retail: a Goblin horde enters one mine shaft and comes out of another; it heals inside; the last tunnel's death lets it out")
{
	if (!haveWorld("tunnel retail"))
	{
		return;
	}
	SharedWorld &s = shared();
	Rig r(s);
	Player *enemy = r.game->players().findPlayerWithName("Player_2");
	REQUIRE(enemy != nullptr);
	const Coord3D c0 = gridSpot(r, 0.3f, 0.5f, 350.0f);
	const Coord3D c1 = gridSpot(r, 0.6f, 0.5f, 350.0f);
	Object *a = r.make("WildMineShaft", c0.x, c0.y, enemy);
	Object *b = r.make("WildMineShaft", c1.x, c1.y, enemy);
	Object *horde = r.make("GoblinFighterHorde", c0.x + 200.0f, c0.y, enemy);
	r.frame(3);
	a->friend_onBuildComplete();
	b->friend_onBuildComplete();
	TunnelContain *ta = tunnelOf(*a), *tb = tunnelOf(*b);
	REQUIRE(ta != nullptr);
	REQUIRE(tb != nullptr);
	const std::vector<Object *> members = membersOf(*horde);
	std::vector<ObjectID> memberIds;
	for (Object *m : members)
	{
		memberIds.push_back(m->getID());
	}
	REQUIRE(!members.empty());
	// hurt the goblins: the network heals them (HealObjects / TimeForFullHeal)
	for (Object *m : members)
	{
		m->getBodyModule()->setInitialHealth(50);
	}
	float hurt = 0.0f, full = 0.0f;
	for (Object *m : members)
	{
		hurt += m->getBodyModule()->getHealth();
		full += m->getBodyModule()->getMaxHealth();
	}
	REQUIRE(enter(r, *horde, *a, 400));
	CHECK(horde->getContainedBy() == a);
	CHECK(ta->getContainCount() == 1u);
	CHECK(tb->getContainCount() == 1u); // the same network
	CHECK(tb->getContainedItemsList()->front() == horde);
	r.frame(150);
	float healed = 0.0f;
	for (Object *m : members)
	{
		healed += m->getBodyModule()->getHealth();
	}
	std::printf("  info: goblins' health %.0f -> %.0f of %.0f inside the network; tracker heal hits %llu\n", hurt, healed, full, enemy->tunnelTracker().healHits());
	if (enemy->tunnelTracker().healHits() > 0)
	{
		CHECK(healed > hurt);
	}
	// the evacuate order at the other tunnel: the horde comes out there
	tb->orderAllPassengersToExit((int)CMD_FROM_SCRIPT);
	int out = -1;
	for (int f = 0; f < 300 && out < 0; ++f)
	{
		r.frame(1);
		if (!horde->getContainedBy())
		{
			out = f;
		}
	}
	REQUIRE_MESSAGE(out >= 0, "the horde never came out");
	r.frame(30);
	std::printf("  info: out of the second shaft after %d frames: %.0f from it, %.0f from the first\n", out, dist2D(*horde, *b), dist2D(*horde, *a));
	CHECK(dist2D(*horde, *b) < dist2D(*horde, *a));
	CHECK(horde->isInWorld());
	CHECK(ta->getContainCount() == 0u);
	// in again by the first; both tunnels die: the last one lets the horde out (RotWK: no cave-in kill)
	REQUIRE(enter(r, *horde, *a, 400));
	a->kill(0);
	r.frame(5);
	CHECK(horde->getContainedBy() == b); // b still stands: the horde stays in the network, now as entered by b (RW 0x8FA4E8)
	CHECK(enemy->tunnelTracker().tunnelCount() >= 1u);
	b->kill(0);
	r.frame(30);
	size_t alive = 0;
	for (ObjectID id : memberIds) // a member may be gone: only its id is read
	{
		const Object *m = r.logic().findObjectByID(id);
		alive += (m && !m->isEffectivelyDead()) ? 1u : 0u;
	}
	std::printf("  info: both shafts dead: the horde %s, %zu of %zu goblins alive\n", horde->getContainedBy() ? "still inside" : "out", alive, members.size());
	CHECK(horde->getContainedBy() == nullptr);
	CHECK(alive > 0u);
}
