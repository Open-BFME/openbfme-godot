// OpenBFME. PLAY-4: the Discord tester's "Aragorn doesn't attack anyone" (v0.3.0-preview.6), with the retail data: every multiplayer hero of every faction is
// recruited from its fortress (MSG_QUEUE_UNIT_CREATE with the hero record's index, the ring heroes after the spell book's Upgrade_RingHero), walks to a spot
// between the bases (a player move order) and then, in two separate games:
//   * on its own: the Battle stance's guard (AIGuard.h) attacks an enemy horde made 100 away, no order;
//   * on an order: with the auto-acquire switched off (no unit picks a target by itself, the horde does not fight back), the hero is ordered onto the horde.
// Each mode counts the hero's strikes and the health the horde's members lost.
// The cause of the report (lane PLAY-4): a lone melee unit whose engagement search (RW 0x6F37B3) finds no free spot next to a horde member falls back to the
// contact back-off (RW 0x7463E8), whose CanApproachToTarget (RW 0x6FB754) the port had replaced by a line test that other units' goal reservations close: next
// to a horde no line was ever open, so the hero waited ten seconds at a time and never struck. Needs the retail install (SKIPs loudly without it).

#include "doctest.h"

#include "BuildTestUtil.h"

#include "Common/Player.h"
#include "Common/PlayerHeroList.h"
#include "Common/PlayerTemplate.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Object/Object.h"

#include <cmath>
#include <set>

namespace
{
// a horde goes with its last member: looked up by id (0 health when gone)
float membersHealth(GameLogic &logic, ObjectID id)
{
	float h = 0.0f;
	Object *horde = logic.findObjectByID(id);
	if (horde && horde->getContain() && horde->getContain()->getContainedItemsList())
	{
		for (Object *m : *horde->getContain()->getContainedItemsList())
		{
			h += m->isEffectivelyDead() ? 0.0f : m->getBodyModule()->getHealth();
		}
	}
	return h;
}

std::vector<std::string> heroesOf(const PlayerTemplate &pt)
{
	std::vector<std::string> out;
	std::set<std::string> seen;
	for (const auto *list : { &pt.m_buildableHeroesMP, &pt.m_buildableRingHeroesMP })
	{
		for (const std::string &h : *list)
		{
			if (seen.insert(h).second)
			{
				out.push_back(h);
			}
		}
	}
	return out;
}

struct Fight
{
	bool recruited = false;
	int recruitFrames = -1;
	bool giantBird = false; // GIANT_BIRD heroes (the fell beasts, Drogoth) run GiantBirdAIUpdate
	unsigned long long shots = 0;
	float lost = 0.0f;
	unsigned hordeMembers = 0;
	unsigned membersKilled = 0;
	bool heroAlive = false;
	float walked = 0.0f;     // how far the move order to the spot took it
	bool noted3804 = false;  // the runtime stops the game reported
	bool noted3807 = false;
};

bool noted(GameLogic &logic, const char *id)
{
	for (const auto &kv : logic.notedStopHits())
	{
		if (kv.first.rfind(id, 0) == 0)
		{
			return true;
		}
	}
	return false;
}

GameMessage selectMsg(int player, ObjectID id)
{
	GameMessage m(MSG_CREATE_SELECTED_GROUP, player);
	m.appendBooleanArgument(true);
	m.appendObjectIDArgument(id);
	return m;
}

GameMessage queueHeroMsg(int player, int index)
{
	// MSG_QUEUE_UNIT_CREATE with the build-index flag (RW 0x77A764): the index of the player's hero record
	GameMessage m(MSG_QUEUE_UNIT_CREATE, player);
	m.appendBooleanArgument(true);
	m.appendIntegerArgument(index);
	m.appendIntegerArgument(-1);
	m.appendBooleanArgument(false);
	m.appendBooleanArgument(false);
	return m;
}

// a fresh game; the hero recruited from the fortress and walked to the spot between the bases; then the fight of the mode
Fight fight(starttest::Shared &s, const char *faction, const std::string &hero, bool ordered)
{
	Fight out;
	std::string error;
	buildtest::Game g;
	REQUIRE_MESSAGE(buildtest::startGame(s, faction, "FactionMordor", 4242, g, &error), error);
	LiveGame &live = *g.live;
	GameLogic &logic = live.logic();
	Player *player = live.players().findPlayerWithName(live.report().startSlotPlayers[0]);
	Player *enemy = live.players().findPlayerWithName(live.report().startSlotPlayers[1]);
	REQUIRE(player);
	REQUIRE(enemy);
	player->getMoney()->deposit(1000000, false);
	player->commandPoints().setFromScript(100000, 100000);
	const ThingTemplate *tt = logic.things().findTemplate(hero);
	REQUIRE(tt);
	tt = tt->getFinalOverride();
	int index = player->heroes().findIndex(*tt, 0xFFFFFFFFu, 0);
	if (index < 0)
	{
		// a ring hero: the spell book's BuildableHeroListUpgrade (Upgrade_RingHero, RW 0x8BC4C6) lists it
		Object *book = logic.findObjectByID(player->science().spellBookId());
		REQUIRE(book);
		book->giveUpgrade("Upgrade_RingHero");
		index = player->heroes().findIndex(*tt, 0xFFFFFFFFu, 0);
	}
	REQUIRE_MESSAGE(index >= 0, hero << " has no hero record");
	Object *fortress = nullptr;
	for (const StartingBase::Placed &p : live.report().startingObjects)
	{
		Object *o = p.structure ? logic.findObjectByID(p.id) : nullptr;
		fortress = (o && o->getControllingPlayer() == player) ? o : fortress;
	}
	REQUIRE(fortress);
	// the producer: the player's structure whose queue takes the hero (the fortress's citadel)
	const int pi = player->getPlayerIndex();
	Object *producer = nullptr;
	for (Object *o = logic.getFirstObject(); o && !producer; o = o->getNextObject())
	{
		ProductionUpdateInterface *pu = o->getControllingPlayer() == player && o->isKindOfName("STRUCTURE") ? o->getProductionUpdate() : nullptr;
		if (!pu)
		{
			continue;
		}
		live.commands().append(selectMsg(pi, o->getID()));
		live.commands().append(queueHeroMsg(pi, index));
		logic.runLogicFrame();
		producer = pu->firstProduction() != nullptr ? o : nullptr;
	}
	REQUIRE_MESSAGE(producer, hero << ": no structure of " << faction << " recruits it");
	const ObjectID producerId = producer->getID();
	Object *h = nullptr;
	for (int f = 1; f < 6000 && !h; ++f)
	{
		logic.runLogicFrame();
		for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
		{
			if (o->getTemplate()->getFinalOverride() == tt && o->getControllingPlayer() == player && !o->isEffectivelyDead())
			{
				h = o;
				out.recruitFrames = f;
			}
		}
	}
	REQUIRE_MESSAGE(h, hero << " was never made");
	out.recruited = h->getProducerID() == producerId;
	out.giantBird = h->isKindOfName("GIANT_BIRD");
	const ObjectID heroId = h->getID();
	const Coord3D made = *h->getPosition();
	// the spot between the bases, out of both fortresses' reach (Evendim: start 0 at the west, start 4 at the east)
	Coord3D spot = *fortress->getPosition();
	spot.x += 900.0f;
	spot.z = logic.getGroundHeight(spot.x, spot.y);
	h->getAIUpdateInterface()->aiMoveToPosition(spot, CMD_FROM_PLAYER);
	for (int f = 0; f < 1500; ++f)
	{
		logic.runLogicFrame();
		h = logic.findObjectByID(heroId);
		REQUIRE(h);
		const float dx = h->getPosition()->x - spot.x, dy = h->getPosition()->y - spot.y;
		if (f > 10 && std::sqrt(dx * dx + dy * dy) < 60.0f && !h->getAIUpdateInterface()->isMoving())
		{
			break;
		}
	}
	for (int f = 0; f < 15; ++f)
	{
		logic.runLogicFrame(); // the move's end (StancesBehavior onMoveEnd -> the guard, RW 0x86228E)
	}
	h = logic.findObjectByID(heroId);
	REQUIRE(h);
	out.walked = std::sqrt((h->getPosition()->x - made.x) * (h->getPosition()->x - made.x) + (h->getPosition()->y - made.y) * (h->getPosition()->y - made.y));
	if (ordered)
	{
		logic.combat().setAutoAcquireEnabled(false);
	}
	const Coord3D at = *h->getPosition();
	Object *horde = live.createObject("MordorFighterHorde", enemy->getPlayerIndex(), Coord3D{ at.x + 100.0f, at.y, 0.0f }, 0.0f, &error);
	REQUIRE_MESSAGE(horde, error);
	const ObjectID hordeId = horde->getID();
	logic.runLogicFrame();
	horde = logic.findObjectByID(hordeId);
	REQUIRE(horde);
	REQUIRE(horde->getContain()->getContainedItemsList());
	REQUIRE_FALSE(horde->getContain()->getContainedItemsList()->empty());
	std::vector<ObjectID> members;
	for (Object *m : *horde->getContain()->getContainedItemsList())
	{
		members.push_back(m->getID());
	}
	out.hordeMembers = (unsigned)members.size();
	const float before = membersHealth(logic, hordeId);
	h = logic.findObjectByID(heroId);
	const unsigned long long shots0 = h && h->getWeapons() ? h->getWeapons()->stats().shotsFired : 0;
	if (ordered && h)
	{
		// the player's order: the cursor's pick of a horde member passes to the horde (HudObjects.cpp, ZH addDrawableToList), whose nearest member the AI attacks
		// (aiAttackObject's horde victim, then the next member while the horde lives)
		h->getAIUpdateInterface()->aiAttackObject(logic.findObjectByID(hordeId), CMD_FROM_PLAYER);
	}
	for (int f = 0; f < 150; ++f)
	{
		logic.runLogicFrame();
	}
	h = logic.findObjectByID(heroId);
	out.heroAlive = h && !h->isEffectivelyDead();
	out.shots = (h && h->getWeapons() ? h->getWeapons()->stats().shotsFired : shots0) - shots0;
	out.lost = before - membersHealth(logic, hordeId);
	out.noted3804 = noted(logic, "[S-3804]");
	out.noted3807 = noted(logic, "[S-3807]");
	for (ObjectID id : members)
	{
		Object *m = logic.findObjectByID(id);
		out.membersKilled += (!m || m->isEffectivelyDead()) ? 1u : 0u;
	}
	return out;
}
} // namespace

TEST_CASE("play4 heroes retail: every faction's heroes, recruited from the fortress, fight an enemy horde on their own and, auto-acquire off, on an order")
{
	OPENBFME_REQUIRE_START(s);
	for (const char *faction : { "FactionMen", "FactionElves", "FactionDwarves", "FactionIsengard", "FactionMordor", "FactionWild", "FactionAngmar" })
	{
		const PlayerTemplate *pt = s->world->playerTemplates().findPlayerTemplate(faction);
		REQUIRE(pt);
		const std::vector<std::string> heroes = heroesOf(*pt);
		CHECK_FALSE(heroes.empty());
		for (const std::string &hero : heroes)
		{
			if (hero == "CreateAHero")
			{
				continue; // the Create-a-Hero placeholder: a player's own hero is built from its saved definition, not this template (S-852)
			}
			INFO(std::string(faction) << " " << hero);
			const Fight own = fight(*s, faction, hero, false);
			const Fight ord = fight(*s, faction, hero, true);
			MESSAGE(std::string(faction) << " " << hero << std::string(own.giantBird ? " (GIANT_BIRD)" : "") << ": recruited in " << own.recruitFrames << " frames; on its own " << own.shots
							<< " strikes, " << own.lost << " health taken, " << own.membersKilled << "/" << own.hordeMembers << " killed, walked " << own.walked << "; ordered " << ord.shots
							<< " strikes, " << ord.lost << " health taken, " << ord.membersKilled << "/" << ord.hordeMembers << " killed");
			CHECK(own.recruited);
			CHECK(ord.recruited);
			if (own.giantBird)
			{
				// lane FLY-1 closed S-3804 (the GIANT_BIRD mover and GiantBirdAIUpdate are ported, S-4040): the birds fly to the horde. Measured on the merged tree
				// (merge queue q7): on their own they strike nothing and take nothing; ordered, Khamul,
				// Morgomir and the Witch-king on his fell beast kill 5 members (450 health taken) and Drogoth 4 strikes, 1500 health, all 20
				CHECK_FALSE(own.noted3804);
				CHECK_FALSE(ord.noted3804);
				CHECK(own.shots == 0u);
				CHECK(own.lost == 0.0f);
				CHECK(own.membersKilled == 0u);
				const bool drogoth = hero == "Drogoth";
				CHECK(ord.shots == (drogoth ? 4u : 0u));
				CHECK(ord.lost == (drogoth ? 1500.0f : 450.0f));
				CHECK(ord.membersKilled == (drogoth ? 20u : 5u));
				continue;
			}
			CHECK(own.walked > 600.0f);
			// on its own: the Battle stance's guard attacks the horde (with the reach of RW 0x6ED843 every walking hero kills several members)
			CHECK(own.shots > 0u);
			CHECK(own.lost > 0.0f);
			CHECK_FALSE(own.noted3804);
			// lane PATHPERF-1 (measured on the merge of PLAY-4 r3): MordorMouthOfSauron, ordered onto the horde, chases a member that moves (46 then 96 away);
			// findPath gives every goal the attack machine asks (the corridor adjusts none, each path ends at its goal), its corridor's routes differ from the
			// search without it, and the machine ends at frame 30 with the nearest member 24 - 35 away, S-3807 noted; with the corridor switched off (a test
			// switch) he strikes twice. A second named case of S-3807, not a search that fails
			const bool unfired3807 = (std::string(faction) == "FactionAngmar" && hero == "AngmarWitchking") || (std::string(faction) == "FactionMordor" && hero == "MordorMouthOfSauron");
			if (unfired3807)
			{
				// the named exceptions, stop S-3807 (measured): the Witch-king, ordered onto the horde, runs his crush (0xE9) through its members, which deals no crush
				// damage, and RW's blocked test (RW 0x747207) with the re-acquisition throttle (RW 0x74D46D) ends the order before a strike: no strike, no
				// damage, the stop reported
				CHECK(ord.shots == 0u);
				CHECK(ord.lost == 0.0f);
				CHECK(ord.membersKilled == 0u);
				CHECK(ord.noted3807);
				continue;
			}
			// on an order (auto-acquire off): the order alone strikes and kills (a later attack segment can still end unfired and report S-3807)
			CHECK(ord.shots > 0u);
			CHECK(ord.lost > 0.0f);
		}
	}
}

// lane PLAY-4 r1: RW 0x6CC07C's melee branch for a moving victim (an AI with a current locomotor, RW 0x6CC0CE) is RW 0x6ED843's footprint test. Aragorn (PathfindDiameter
// 40: 4 cells, not centred) against a lone MordorFighter (radius 8: diameter 16 -> 20 -> 2 cells), STANDARD_MELEE_ATTACK_RANGE 11.5 -> floor(11.5 * 0.1 + 0.1) = 1
// cell: the unit box [c - 2 - 1, c - 2 + 4 + 1] and the target box [t - 1, t - 1 + 2] overlap while |t - c| <= 4 cells. Both on cell centres of one row
// (x = 10 * cell: not centred, RW 0x6E8CE6 adds 0.5 before the floor): 40 apart is in reach, 50 is not (the S-320 edge distance it replaced reached 11.5 + 8 + 8 = 27.5).
// The reach is tested from the source position RW passes (the engagement search's candidate cell), not from where the unit stands.
TEST_CASE("play4 heroes retail: a melee weapon reaches a moving unit by the footprints (RW 0x6ED843), from the position it is asked about")
{
	OPENBFME_REQUIRE_START(s);
	std::string error;
	buildtest::Game g;
	REQUIRE_MESSAGE(buildtest::startGame(*s, "FactionMen", "FactionMordor", 4242, g, &error), error);
	LiveGame &live = *g.live;
	GameLogic &logic = live.logic();
	Player *player = live.players().findPlayerWithName(live.report().startSlotPlayers[0]);
	Player *enemy = live.players().findPlayerWithName(live.report().startSlotPlayers[1]);
	REQUIRE(player);
	REQUIRE(enemy);
	const float y = 4500.0f;
	Object *hero = live.createObject("GondorAragornMP", player->getPlayerIndex(), Coord3D{ 1500.0f, y, 0.0f }, 0.0f, &error);
	REQUIRE_MESSAGE(hero, error);
	Object *orc = live.createObject("MordorFighter", enemy->getPlayerIndex(), Coord3D{ 1540.0f, y, 0.0f }, 0.0f, &error);
	REQUIRE_MESSAGE(orc, error);
	auto put = [&](Object *o, float x) {
		Coord3D p{ x, y, logic.getGroundHeight(x, y) };
		o->setPosition(&p);
	};
	ObjectWeapons *w = hero->getWeapons();
	REQUIRE(w);
	REQUIRE(w->currentWeapon());
	REQUIRE(w->currentWeapon()->getTemplate()->m_meleeWeapon);
	REQUIRE(orc->getAIUpdateInterface());
	REQUIRE(orc->getAIUpdateInterface()->curLocomotor());
	put(hero, 1500.0f);
	put(orc, 1540.0f);
	CHECK(w->isWithinAttackRange(*orc, 0.0f, false)); // 4 cells: the boxes touch
	put(orc, 1550.0f);
	CHECK_FALSE(w->isWithinAttackRange(*orc, 0.0f, false)); // 5 cells
	put(orc, 1460.0f);
	CHECK(w->isWithinAttackRange(*orc, 0.0f, false)); // 4 cells on the other side
	// asked from another position (the engagement search's candidate): the reach is the candidate's
	put(orc, 1540.0f);
	put(hero, 2000.0f);
	CHECK_FALSE(w->isWithinAttackRange(*orc, 0.0f, false));
	CHECK(w->isWithinAttackRangeFrom(Coord3D{ 1500.0f, y, logic.getGroundHeight(1500.0f, y) }, orc, *orc->getPosition(), 0.0f, false));
	// a SIEGE_TOWER outside the boxes takes RW's ramp branch (RW 0x6EDB6F), not ported: false and stop S-3806 reported
	Object *tower = live.createObject("MordorSiegeTower", enemy->getPlayerIndex(), Coord3D{ 1700.0f, y, 0.0f }, 0.0f, &error);
	REQUIRE_MESSAGE(tower, error);
	put(hero, 1500.0f);
	put(tower, 1700.0f);
	CHECK_FALSE(w->isWithinAttackRange(*tower, 0.0f, false));
	bool noted = false;
	for (const auto &kv : logic.notedStopHits())
	{
		noted = noted || kv.first == meleeReachStopLine();
	}
	CHECK(noted);
}
