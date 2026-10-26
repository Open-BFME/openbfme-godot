// OpenBFME retail tests for GARRISON-2's TunnelContain (the player's tunnel network, RW 0x880E29 .. 0x881404 and the TunnelTracker RW 0x8FA152 .. 0x8FA4E8): a Goblin
// horde enters one WildMineShaft and comes out of another, heals inside, and comes out when the last tunnel dies. They share the HUD tests' retail world and SKIP
// when ROTWK_INSTALL / BFME2_INSTALL are unset. The units are the computer player's and the orders CMD_FROM_SCRIPT (the rig's shroud refuses an AI order to a
// container the player has not seen: S-565).

#include "doctest.h"
#include "HudTestUtil.h"

#include "Common/Player.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/InGameHud.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/Contain/TunnelContainRuntime.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <set>
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

TEST_CASE("tunnel retail: a Goblin horde enters one mine shaft and comes out of another; nobody heals inside (no 2.01 HealObjects); the last tunnel's death lets it out")
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
	// hurt the goblins: the network would heal them only with HealObjects (TimeForFullHeal), which no 2.01 tunnel sets
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
	// TUNNEL-1: no 2.01 tunnel sets HealObjects (wildmineshaft.ini, wildburrowsexpansion.ini, dwarvenmineshaft.ini): RW 0x8812B0 heals nobody
	CHECK(enemy->tunnelTracker().healHits() == 0u);
	CHECK(healed == hurt);
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

// TUNNEL-1: the player's flow on the HUD. A Goblin player right-clicks a mine shaft with a horde selected (MSG_ENTER), selects the OTHER shaft: its
// WildMineShaftCommandSet's five Command_ExitGarrison (EXIT_CONTAINER) buttons show the network's rider (RW 0x94251F: the contain's list is the tracker's,
// RW 0x8810AB), and the press (MSG_EXIT RW 0x940FEF -> RW 0x77AC92, the rider in the pressed tunnel's network: contain slot 0xE8 = RW 0x8811DD) lets the
// horde out at the shaft whose button was pressed. Before TUNNEL-1 the press did nothing: MSG_EXIT compared the rider's own container with the pressed one.
TEST_CASE("tunnel retail: a Goblin player right-clicks a horde into one mine shaft and lets it out of another with that shaft's exit button")
{
	if (!haveWorld("tunnel retail hud"))
	{
		return;
	}
	SharedWorld &s = shared();
	Rig r(s, "map mp fall back 4p", "FactionWild");
	RecordingShellServices services;
	InGameHud::Config cfg{ *r.game, *s.world, *s.mount->fs, services, r.view, s.mouse, s.meta, nullptr };
	auto hud = std::make_unique<InGameHud>(cfg);
	std::string error;
	REQUIRE_MESSAGE(hud->boot(&error), error);
	hud->setWindowSize(1024, 768);
	r.view.setScreen(1024, 768);
	auto frames = [&](int n) {
		for (int i = 0; i < n; ++i)
		{
			hud->update(0.033);
			r.game->advance(0.033);
		}
	};
	auto exitButtons = [&]() {
		std::vector<ControlBarButton> out;
		std::set<int> seen;
		for (const auto *v : { &hud->controlBar().palantirButtons(), &hud->controlBar().sideButtons(), &hud->controlBar().offBarButtons() })
		{
			for (const ControlBarButton &b : *v)
			{
				if (b.button && b.button->m_command == GUI_COMMAND_EXIT_CONTAINER && seen.insert(b.slot).second)
				{
					out.push_back(b);
				}
			}
		}
		std::sort(out.begin(), out.end(), [](const ControlBarButton &a, const ControlBarButton &b) { return a.slot < b.slot; });
		return out;
	};
	const Coord3D c0 = gridSpot(r, 0.35f, 0.5f, 350.0f);
	const Coord3D c1 = gridSpot(r, 0.65f, 0.5f, 350.0f);
	Object *a = r.make("WildMineShaft", c0.x, c0.y);
	Object *b = r.make("WildMineShaft", c1.x, c1.y);
	Object *horde = r.make("GoblinFighterHorde", c0.x + 220.0f, c0.y);
	frames(3);
	a->friend_onBuildComplete(); // finished shafts (the porter's completion is test_tunnel1_flow.cpp's)
	b->friend_onBuildComplete();
	REQUIRE(tunnelOf(*a) != nullptr);
	REQUIRE(tunnelOf(*a)->isRegistered());
	REQUIRE(tunnelOf(*b)->isRegistered());

	// the player's mouse (the rig's input, as test_hud_combat_input.cpp): a left click on the horde selects it, then with the alternate mouse setup a right
	// click on the first shaft is MSG_ENTER (CommandXlat: the EnterFriendly order)
	r.lookAt(Coord3D{ (c0.x + horde->getPosition()->x) * 0.5f, c0.y, 0.0f }, 520.0f, 640.0f);
	r.frame(1);
	const ICoord2D ph = r.screenOf({ horde->getPosition()->x, horde->getPosition()->y, 6.0f });
	r.leftClick(ph.x, ph.y);
	r.frame(1);
	REQUIRE(r.local->selection().size() == 1u);
	REQUIRE(r.local->selection()[0] == horde->getID());
	r.input->commandTranslator().setUseAlternateMouse(true);
	const ICoord2D at = r.screenOf({ c0.x, c0.y, 10.0f });
	r.rightClick(at.x, at.y);
	HordeContainInterface *hc = horde->getContain()->getHordeContainInterface();
	REQUIRE(hc != nullptr);
	int n = 0;
	while (n < 900 && !(horde->getContainedBy() == a && hc->allMembersEntered()))
	{
		frames(1);
		++n;
	}
	REQUIRE_MESSAGE(horde->getContainedBy() == a, "the right-click did not put the horde into the first shaft");

	// the other shaft's control bar: the horde on its first exit button, enabled, with the horde's ButtonImage
	r.lookAt(*b->getPosition());
	hud->input().ui().selectObject(b->getID());
	frames(5);
	std::vector<ControlBarButton> exits = exitButtons();
	REQUIRE(exits.size() == 5u); // WildMineShaftCommandSet 1 .. 5
	CHECK(exits[0].rider == horde->getID());
	CHECK(exits[0].state == ButtonState::Enabled);
	for (std::size_t i = 1; i < exits.size(); ++i)
	{
		CHECK(exits[i].rider == INVALID_ID);
	}
	REQUIRE(hud->controlBar().pressButton(exits[0].slot, exits[0].inPalantir));
	n = 0;
	while (n < 900 && horde->getContainedBy())
	{
		frames(1);
		++n;
	}
	REQUIRE_MESSAGE(horde->getContainedBy() == nullptr, "the second shaft's exit button did not let the horde out");
	frames(30);
	std::printf("  info: the exit button: out after %d frames, %.0f from the pressed shaft, %.0f from the entered one\n", n, dist2D(*horde, *b), dist2D(*horde, *a));
	CHECK(dist2D(*horde, *b) < dist2D(*horde, *a)); // tolerance: which shaft it left by; the shafts stand ~1700 apart, the horde re-forms a few units after the exit
	CHECK(horde->isInWorld());
	frames(3);
	exits = exitButtons();
	REQUIRE(exits.size() == 5u);
	CHECK(exits[0].rider == INVALID_ID);
	CHECK(exits[0].state == ButtonState::Restricted);
	hud.reset();
}
