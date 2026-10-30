// OpenBFME retail tests (TUNNEL-1): the Goblins' tunnel network in a real skirmish, through the lockstep command path. A Goblin porter builds two WildMineShafts
// (MSG_DOZER_CONSTRUCT), a goblin horde is ordered into the first (MSG_ENTER) and let out at the second by the second tunnel's exit button (MSG_EXIT, RW 0x77AC92:
// the rider must be in the clicked container's network, contain slot 0xE8 = TunnelContain RW 0x8811DD, not in the tunnel it entered by). SKIP loudly without
// the retail install.  GPL-3.0.

#include "doctest.h"

#include "BuildTestUtil.h"

#include "Common/Player.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/AI/AIGarrisonStates.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/DozerAIUpdate.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Contain/TunnelContainRuntime.h"
#include "GameLogic/Object/Object.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <vector>

namespace
{
const char *const kTournament = "maps/map mp tournament mp1/map mp tournament mp1.map";

GameMessage selectMsg(int player, const std::vector<ObjectID> &ids)
{
	GameMessage m(MSG_CREATE_SELECTED_GROUP, player);
	m.appendBooleanArgument(true);
	for (ObjectID id : ids)
	{
		m.appendObjectIDArgument(id);
	}
	return m;
}

Player *humanOf(LiveGame &live)
{
	return live.players().findPlayerWithName(live.report().startSlotPlayers[0]);
}

Object *dozerOf(LiveGame &live, Player *player)
{
	for (Object *o = live.logic().getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() == player && o->isKindOfName("DOZER") && dynamic_cast<DozerAIUpdate *>(o->getAIUpdateInterface()))
		{
			return o;
		}
	}
	return nullptr;
}

Object *fortressOf(LiveGame &live, Player *player)
{
	for (const StartingBase::Placed &p : live.report().startingObjects)
	{
		Object *o = p.structure ? live.logic().findObjectByID(p.id) : nullptr;
		if (o && o->getControllingPlayer() == player)
		{
			return o;
		}
	}
	return nullptr;
}

float dist2D(const Object &a, const Object &b)
{
	const float dx = a.getPosition()->x - b.getPosition()->x, dy = a.getPosition()->y - b.getPosition()->y;
	return std::sqrt(dx * dx + dy * dy);
}

// the porter builds `tt` on the first legal site of a ring around `centre` at least `minRadius` out; the finished tunnel, or null
Object *buildTunnel(LiveGame &live, Player &player, Object &porter, const ThingTemplate &tt, const Coord3D &centre, float minRadius)
{
	GameLogic &logic = live.logic();
	DozerAIUpdate *dozer = dynamic_cast<DozerAIUpdate *>(porter.getAIUpdateInterface());
	REQUIRE(dozer != nullptr);
	Coord3D site{};
	bool found = false;
	for (float r = minRadius; r <= minRadius + 600.0f && !found; r += 40.0f)
	{
		for (int k = 0; k < 24 && !found; ++k)
		{
			const double a = (double)k * 0.2617993877991494;
			Coord3D c = centre;
			c.x += (float)(r * std::cos(a));
			c.y += (float)(r * std::sin(a));
			c.z = logic.getGroundHeight(c.x, c.y);
			if (dozer->siteIsLegal(tt, c, 0.0f, player))
			{
				site = c;
				found = true;
			}
		}
	}
	REQUIRE_MESSAGE(found, "no legal site for " << tt.getName());
	std::set<ObjectID> before;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		before.insert(o->getID());
	}
	const int pi = player.getPlayerIndex();
	live.commands().append(selectMsg(pi, { porter.getID() }));
	GameMessage m(MSG_DOZER_CONSTRUCT, pi);
	m.appendIntegerArgument((int)tt.getTemplateID());
	m.appendLocationArgument(site);
	m.appendRealArgument(0.0f);
	live.commands().append(m);
	logic.runLogicFrame();
	ObjectID id = INVALID_ID;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (!before.count(o->getID()) && o->getTemplate()->getFinalOverride() == tt.getFinalOverride() && o->getControllingPlayer() == &player)
		{
			id = o->getID();
		}
	}
	REQUIRE_MESSAGE(id != INVALID_ID, "the porter made no foundation");
	for (int guard = 0; guard < 6000; ++guard)
	{
		Object *b = logic.findObjectByID(id);
		REQUIRE(b != nullptr);
		static const int phantom = ObjectTemplateInfoBuilder::objectStatusIndex("PHANTOM_STRUCTURE");
		if (!b->isUnderConstruction() && !b->testStatus((unsigned)phantom)) // lane FBSIM-1: a phantom until the porter places it (RW 0x88D44F)
		{
			return b;
		}
		logic.runLogicFrame();
	}
	return nullptr;
}

bool allIn(Object &horde)
{
	HordeContainInterface *h = horde.getContain() ? horde.getContain()->getHordeContainInterface() : nullptr;
	return horde.getContainedBy() != nullptr && h && h->allMembersEntered();
}
// the player orders `horde` into `into` (MSG_ENTER: argument 0 the enterer, argument 1 the container) and runs until every member is in; false on the guard
bool orderEnter(LiveGame &live, int pi, Object &horde, Object &into)
{
	live.commands().append(selectMsg(pi, { horde.getID() }));
	GameMessage enter(MSG_ENTER, pi);
	enter.appendObjectIDArgument(horde.getID());
	enter.appendObjectIDArgument(into.getID());
	live.commands().append(enter);
	for (int f = 0; f < 600 && !allIn(horde); ++f)
	{
		live.logic().runLogicFrame();
	}
	return allIn(horde);
}

// the player presses `at`'s exit button for `horde` (MSG_EXIT { rider, container }: the control bar's press RW 0x940FEF) and runs until it is out; the frames
// it took, or -1
int orderExit(LiveGame &live, int pi, Object &horde, Object &at)
{
	live.commands().append(selectMsg(pi, { at.getID() }));
	GameMessage exitMsg(MSG_EXIT, pi);
	exitMsg.appendObjectIDArgument(horde.getID());
	exitMsg.appendObjectIDArgument(at.getID());
	live.commands().append(exitMsg);
	for (int f = 0; f < 600; ++f)
	{
		live.logic().runLogicFrame();
		if (!horde.getContainedBy())
		{
			return f;
		}
	}
	return -1;
}
} // namespace

TEST_CASE("tunnel1 flow: a Goblin porter builds two mine shafts; a horde ordered into one (MSG_ENTER) comes out of the other by its exit button (MSG_EXIT)")
{
	OPENBFME_REQUIRE_START(s);
	buildtest::Game g;
	std::string error;
	REQUIRE_MESSAGE(buildtest::startGame(*s, "FactionWild", "FactionMen", 5150, g, &error, kTournament, 1, 0), error);
	LiveGame &live = *g.live;
	GameLogic &logic = live.logic();
	Player *human = humanOf(live);
	REQUIRE(human != nullptr);
	human->depositMoney(20000, false);
	Object *porter = dozerOf(live, human);
	Object *fortress = fortressOf(live, human);
	REQUIRE(porter != nullptr);
	REQUIRE(fortress != nullptr);
	const ThingTemplate *shaft = logic.things().findTemplate("WildMineShaft");
	REQUIRE(shaft != nullptr);
	shaft = shaft->getFinalOverride();
	const unsigned tunnels0 = human->tunnelTracker().tunnelCount();
	Object *a = buildTunnel(live, *human, *porter, *shaft, *fortress->getPosition(), 260.0f);
	REQUIRE_MESSAGE(a != nullptr, "the first shaft never completed");
	Object *b = buildTunnel(live, *human, *porter, *shaft, *fortress->getPosition(), 700.0f);
	REQUIRE_MESSAGE(b != nullptr, "the second shaft never completed");
	TunnelContain *ta = dynamic_cast<TunnelContain *>(a->getContain());
	TunnelContain *tb = dynamic_cast<TunnelContain *>(b->getContain());
	REQUIRE(ta != nullptr);
	REQUIRE(tb != nullptr);
	// a finished, porter-built shaft joins its player's network (onBuildComplete RW 0x8810C9)
	CHECK(ta->isRegistered());
	CHECK(tb->isRegistered());
	CHECK(human->tunnelTracker().tunnelCount() == tunnels0 + 2u);
	const float apart = dist2D(*a, *b);
	std::printf("  info: two shafts %.0f apart\n", apart);
	REQUIRE(apart > 300.0f); // tolerance: the two sites only need to be told apart (the second ring starts 700 out)

	// a goblin horde next to the first shaft, ordered in by the player (MSG_ENTER: argument 0 the enterer, argument 1 the container)
	std::string err;
	Coord3D near = *a->getPosition();
	near.x += 150.0f;
	Object *horde = live.createObject("GoblinFighterHorde", human->getPlayerIndex(), near, 0.0f, &err);
	REQUIRE_MESSAGE(horde != nullptr, err);
	logic.runLogicFrame();
	const int pi = human->getPlayerIndex();
	std::vector<ObjectID> memberIds;
	for (Object *m : *horde->getContain()->getContainedItemsList())
	{
		memberIds.push_back(m->getID());
	}
	REQUIRE(memberIds.size() == 20u); // GoblinFighterHorde: 20 goblins
	REQUIRE_MESSAGE(orderEnter(live, pi, *horde, *a), "the horde never got into the first shaft");
	CHECK(horde->getContainedBy() == a);
	CHECK(tb->getContainCount() == 1u); // the network: the second shaft holds it too
	REQUIRE(tb->getContainedItemsList() != nullptr);
	CHECK(tb->getContainedItemsList()->front() == horde);

	// 2.01's tunnels have no HealObjects (TunnelContain blocks of wildmineshaft.ini / wildburrowsexpansion.ini / dwarvenmineshaft.ini): a hurt horde inside
	// stays hurt (RW 0x8812B0 heals only with data + 0x98 set)
	{
		float before = 0.0f, after = 0.0f;
		Object *member = horde->getContain()->getContainedItemsList()->front();
		member->getBodyModule()->setInitialHealth(50);
		before = member->getBodyModule()->getHealth();
		for (int i = 0; i < 50; ++i)
		{
			logic.runLogicFrame();
		}
		after = member->getBodyModule()->getHealth();
		CHECK(after == before);
		CHECK(human->tunnelTracker().healHits() == 0u);
	}

	// the other player's MSG_EXIT for this horde does nothing (RW 0x77ACDE: the rider's controlling player must be the issuer)
	{
		Player *other = live.players().findPlayerWithName(live.report().startSlotPlayers[1]);
		REQUIRE(other != nullptr);
		GameMessage foreign(MSG_EXIT, other->getPlayerIndex());
		foreign.appendObjectIDArgument(horde->getID());
		foreign.appendObjectIDArgument(b->getID());
		live.commands().append(foreign);
		for (int i = 0; i < 20; ++i)
		{
			logic.runLogicFrame();
		}
		CHECK(horde->getContainedBy() == a);
	}

	// the second shaft's exit button for the horde
	const int out = orderExit(live, pi, *horde, *b);
	REQUIRE_MESSAGE(out >= 0, "the horde never came out of the second shaft"); // tolerance: -1 is the guard ran out; the frame count is printed
	logic.runLogicFrame();
	for (int i = 0; i < 30; ++i)
	{
		logic.runLogicFrame();
	}
	std::printf("  info: out after %d frames, %.0f from the second shaft, %.0f from the first\n", out, dist2D(*horde, *b), dist2D(*horde, *a));
	CHECK(dist2D(*horde, *b) < 250.0f); // tolerance: out at the second shaft (its ExitOffset is 80); the horde re-forms after the exit
	CHECK(dist2D(*horde, *a) > 300.0f); // tolerance: not at the first shaft (the shafts stand > 300 apart, checked above)
	CHECK(horde->isInWorld());
	CHECK(ta->getContainCount() == 0u);
	// the same 20 goblins, each back in its horde, in the world, shown, no longer held (DISABLED_HELD off, RW 0x880FC6), at the second shaft
	std::vector<ObjectID> outIds;
	for (Object *m : *horde->getContain()->getContainedItemsList())
	{
		outIds.push_back(m->getID());
		CHECK(m->getContainedBy() == horde);
		CHECK(m->isInWorld());
		CHECK_FALSE(m->isDrawableHidden());
		CHECK((m->getDisabledMask() & (1u << 3)) == 0u);
		CHECK(dist2D(*m, *b) < 300.0f); // tolerance: every member out at the second shaft, in its formation slot around the horde
	}
	std::sort(outIds.begin(), outIds.end());
	std::vector<ObjectID> sortedIds = memberIds;
	std::sort(sortedIds.begin(), sortedIds.end());
	CHECK(outIds == sortedIds);
	CHECK(human->tunnelTracker().containCount() == 0);

	// the entrance dies while the horde is inside: it stays in the network as entered by the second shaft (RW 0x8FA4E8), whose exit button still lets it out
	REQUIRE_MESSAGE(orderEnter(live, pi, *horde, *a), "the horde never got back into the first shaft");
	a->kill(DAMAGE_UNRESISTABLE, 0);
	for (int i = 0; i < 5; ++i)
	{
		logic.runLogicFrame();
	}
	REQUIRE(horde->getContainedBy() == b);
	CHECK(human->tunnelTracker().tunnelCount() == tunnels0 + 1u);
	const int out2 = orderExit(live, pi, *horde, *b);
	REQUIRE_MESSAGE(out2 >= 0, "the horde never came out of the second shaft after the first died"); // tolerance: -1 is the guard ran out
	for (int i = 0; i < 30; ++i)
	{
		logic.runLogicFrame();
	}
	CHECK(dist2D(*horde, *b) < 250.0f); // tolerance: out at the second shaft (its ExitOffset is 80); the horde re-forms after the exit
	CHECK(horde->getContain()->getContainedItemsList()->size() == memberIds.size());
	CHECK(human->tunnelTracker().containCount() == 0);
}

TEST_CASE("tunnel1 flow: the Goblin fortress's Burrows expansion (MSG_FOUNDATION_CONSTRUCT on a pad) joins the mine shafts' network; a horde goes in at a shaft, out at the Burrows")
{
	OPENBFME_REQUIRE_START(s);
	buildtest::Game g;
	std::string error;
	REQUIRE_MESSAGE(buildtest::startGame(*s, "FactionWild", "FactionMen", 5150, g, &error, kTournament, 1, 0), error);
	LiveGame &live = *g.live;
	GameLogic &logic = live.logic();
	Player *human = humanOf(live);
	REQUIRE(human != nullptr);
	human->depositMoney(20000, false);
	const int pi = human->getPlayerIndex();
	Object *pad = nullptr;
	for (Object *o = logic.getFirstObject(); o && !pad; o = o->getNextObject())
	{
		const std::string &n = o->getTemplate()->getName();
		pad = (o->getControllingPlayer() == human && (n == "WildFortressExpansionPadCorner" || n == "WildFortressExpansionPadSide")) ? o : nullptr;
	}
	REQUIRE_MESSAGE(pad != nullptr, "the Goblin fortress has no expansion pad");
	const ThingTemplate *burrowsTt = logic.things().findTemplate("WildBurrowsExpansion");
	REQUIRE(burrowsTt != nullptr);
	burrowsTt = burrowsTt->getFinalOverride();
	live.commands().append(selectMsg(pi, { pad->getID() }));
	GameMessage m(MSG_FOUNDATION_CONSTRUCT, pi);
	m.appendIntegerArgument((int)burrowsTt->getTemplateID());
	m.appendLocationArgument(*pad->getPosition());
	m.appendRealArgument(0.0f);
	live.commands().append(m);
	logic.runLogicFrame();
	Object *burrows = nullptr;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		burrows = o->getTemplate()->getFinalOverride() == burrowsTt && o->getControllingPlayer() == human ? o : burrows;
	}
	REQUIRE_MESSAGE(burrows != nullptr, "the pad made no Burrows");
	const ObjectID burrowsId = burrows->getID();
	for (int f = 0; f < 6000 && burrows->isUnderConstruction(); ++f)
	{
		logic.runLogicFrame();
		burrows = logic.findObjectByID(burrowsId);
		REQUIRE(burrows != nullptr);
	}
	REQUIRE_MESSAGE(!burrows->isUnderConstruction(), "the Burrows never completed");
	TunnelContain *tBurrows = dynamic_cast<TunnelContain *>(burrows->getContain());
	REQUIRE(tBurrows != nullptr);
	CHECK(tBurrows->isRegistered()); // the expansion's completion (RW 0x8A2944 .. 0x8A29EC: every create module's onBuildComplete) joins the network

	Object *porter = dozerOf(live, human);
	REQUIRE(porter != nullptr);
	const ThingTemplate *shaftTt = logic.things().findTemplate("WildMineShaft");
	REQUIRE(shaftTt != nullptr);
	Object *shaft = buildTunnel(live, *human, *porter, *shaftTt->getFinalOverride(), *burrows->getPosition(), 700.0f);
	REQUIRE_MESSAGE(shaft != nullptr, "the shaft never completed");
	CHECK(human->tunnelTracker().tunnelCount() == 2u);

	std::string err;
	// the horde on the shaft's side towards the base (the ring's far side can be past a cliff)
	Coord3D near = *shaft->getPosition();
	{
		const float dx = burrows->getPosition()->x - near.x, dy = burrows->getPosition()->y - near.y;
		const float len = std::sqrt(dx * dx + dy * dy);
		near.x += 150.0f * dx / len;
		near.y += 150.0f * dy / len;
	}
	Object *horde = live.createObject("GoblinFighterHorde", pi, near, 0.0f, &err);
	REQUIRE_MESSAGE(horde != nullptr, err);
	logic.runLogicFrame();
	const bool entered = orderEnter(live, pi, *horde, *shaft);
	if (!entered)
	{
		AIUpdateInterface *ai = horde->getAIUpdateInterface();
		std::printf("  diag: shaft (%.0f, %.0f) burrows (%.0f, %.0f) horde (%.0f, %.0f) state %u goal %u containedBy %u count %u canEnter %d\n", shaft->getPosition()->x,
		            shaft->getPosition()->y, burrows->getPosition()->x, burrows->getPosition()->y, horde->getPosition()->x, horde->getPosition()->y,
		            ai ? ai->currentStateId() : 0u, ai && ai->stateMachine().goalObject() ? ai->stateMachine().goalObject()->getID() : 0u,
		            horde->getContainedBy() ? horde->getContainedBy()->getID() : 0u, tBurrows->getContainCount(),
		            (int)GarrisonRules::canEnterObject(*horde, shaft, CMD_FROM_PLAYER, 0, true));
		std::printf("  diag: canEnter nopath %d, mode1 %d, valid(cap, path) %d, valid(-, -) %d\n", (int)GarrisonRules::canEnterObject(*horde, shaft, CMD_FROM_PLAYER, 0, false),
		            (int)GarrisonRules::canEnterObject(*horde, shaft, CMD_FROM_SCRIPT, 1, true), (int)shaft->getContain()->isValidContainerFor(*horde, true, true),
		            (int)shaft->getContain()->isValidContainerFor(*horde, false, false));
	}
	REQUIRE_MESSAGE(entered, "the horde never got into the shaft");
	CHECK(tBurrows->getContainCount() == 1u);
	const int out = orderExit(live, pi, *horde, *burrows);
	REQUIRE_MESSAGE(out >= 0, "the horde never came out of the Burrows"); // tolerance: -1 is the guard ran out; the frame count is printed
	for (int i = 0; i < 30; ++i)
	{
		logic.runLogicFrame();
	}
	std::printf("  info: out of the Burrows after %d frames: %.0f from it, %.0f from the shaft\n", out, dist2D(*horde, *burrows), dist2D(*horde, *shaft));
	CHECK(dist2D(*horde, *burrows) < dist2D(*horde, *shaft)); // tolerance: which tunnel it left by (they stand ~700 apart)
	CHECK(horde->isInWorld());
}
