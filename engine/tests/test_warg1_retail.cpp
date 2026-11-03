// OpenBFME. GPL-3.0.
// Lane WARG-1 (owner bug 2026-10-29: "the wargs dont seem to have trample the default ones"). A cavalry horde TRAINED at its building in a loaded skirmish
// (map mp fall back 4p, the human player's MSG_QUEUE_UNIT_CREATE) and sent with the player's MSG_DO_ATTACK_OBJECT into an infantry horde of the other side
// tramples it (SquishCollide RW 0x8BFBAE: crushes, CrushWeapon shots, CrushKnockback flings, RamPower hits), as a horde placed in an empty arena does
// (test_hud_horde2_retail.cpp).
//
// The cause: the horde's crush attack (AIAttackMeleeSquishState RW 0x74F21A / 0x742271) paths to a point twice its bounding radius beyond the victim. Here a
// cliff (cells x 128 .. 139) stands behind the victims, the point is not a valid destination, and AIMover::computePath (RW 0x665C33) falls back to pathfinder
// slot + 4, FindClosestPath (RW 0x6FB869). The port stood in for it with findPath's search, whose closest-cell rule (RW 0x6FDA6C: a nearer cell that fails the
// footprint test shuts out every farther one) left a one-cell path: the squish state "arrived" at once and the horde fought in plain melee, no crush. Lane
// WARG-1 ports FindClosestPath. SKIPs without the game files.

#include "doctest.h"

#include "StartTestUtil.h"

#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include "Common/BuildAssistant.h"
#include "Common/Money.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/LiveGame.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/BuildPlacement.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Object/Object.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace
{
struct Trained
{
	bool produced = false;
	unsigned long long crushes = 0, shots = 0, knockbacks = 0, ramHits = 0;
	size_t victimsStart = 0, victimsEnd = 0;
	float closestPathEndX = 0.0f, startX = 0.0f, cliffGoalX = 0.0f;
	bool closestPathFound = false;
};

size_t aliveMembers(LiveGame &game, const std::vector<ObjectID> &ids)
{
	size_t n = 0;
	for (ObjectID id : ids)
	{
		const Object *o = game.logic().findObjectByID(id);
		n += (o && !o->isEffectivelyDead()) ? 1u : 0u;
	}
	return n;
}

void send(LiveGame &game, int player, ObjectID selected, GameMessage m)
{
	GameMessage sel(MSG_CREATE_SELECTED_GROUP, player);
	sel.appendBooleanArgument(true);
	sel.appendObjectIDArgument(selected);
	game.commands().append(sel);
	game.commands().append(m);
}

// `horde` trained at a level 2 `building` of the human player near its start, settled at its rally point, then ordered onto a `victim` horde of the AI side
// 300 east of it (inside its vision: a player's order cannot target a fogged object, RW 0x82C167); the counters over the next 15 s
Trained trainAndCharge(starttest::Shared *s, const char *faction, const char *building, const char *levelUpgrade, const char *horde, const char *enemyFaction,
	const char *victim)
{
	Trained out;
	ArchiveW3DFileSource source(*s->mount->fs);
	WW3DAssetManager assets(source);
	LiveGame game(*s->world, *s->mount->fs, assets, s->options);
	LiveGame::Options o;
	o.mapName = "map mp fall back 4p";
	o.seed = 1;
	o.slots.players.push_back({ "Player_1", faction, true, 0, 0, 0 });
	o.slots.players.push_back({ "Player_2", enemyFaction, false, 1, 0, 1 });
	std::string err;
	REQUIRE_MESSAGE(game.load(o, &err), err);
	Player *p1 = game.players().findPlayerWithName("Player_1");
	Player *p2 = game.players().findPlayerWithName("Player_2");
	REQUIRE(p1);
	REQUIRE(p2);
	const int pi = p1->getPlayerIndex();
	p1->getMoney()->deposit(20000, false);
	// a legal site 500 east of the human player's start (away from the slaved fire drake that guards the centre of this map)
	const ThingTemplate *bt = s->world->things().findTemplate(building);
	REQUIRE(bt);
	const Waypoint *w = game.logic().terrain()->findWaypointByName("Player_1_Start");
	REQUIRE(w);
	Coord3D base = w->location;
	base.x += 500.0f;
	Coord3D site{ 0, 0, 0 };
	bool found = false;
	for (int r = 0; r <= 600 && !found; r += 50)
	{
		for (int k = 0; k < 16 && !found; ++k)
		{
			Coord3D c{ base.x + (float)r * (float)std::cos(k * 0.3927), base.y + (float)r * (float)std::sin(k * 0.3927), 0.0f };
			c.z = game.logic().getGroundHeight(c.x, c.y);
			if (BuildPlacement::isLocationLegalToBuild(game.logic(), c, *bt->getFinalOverride(), 0.0f, LLF_TERRAIN_RESTRICTIONS | LLF_NO_OBJECT_OVERLAP, nullptr, nullptr) ==
				LBC_OK)
			{
				site = c;
				found = true;
			}
		}
	}
	REQUIRE(found);
	Object *b = game.createObject(building, pi, site, 0.0f, &err);
	REQUIRE_MESSAGE(b, err);
	game.advance(0.2);
	b->giveUpgrade(levelUpgrade); // the build button's NeededUpgrade (Options NEED_UPGRADE): the building's level 2
	game.advance(0.2);
	const ThingTemplate *ht = s->world->things().findTemplate(horde);
	REQUIRE(ht);
	REQUIRE(BuildAssistant::canMakeUnit(*b, ht->getFinalOverride(), -1) == CANMAKE_OK);
	GameMessage q(MSG_QUEUE_UNIT_CREATE, pi);
	q.appendBooleanArgument(false);
	q.appendIntegerArgument((int)ht->getTemplateID());
	q.appendIntegerArgument(-1);
	q.appendBooleanArgument(false);
	q.appendBooleanArgument(false);
	send(game, pi, b->getID(), q);
	GameMessage r(MSG_SET_RALLY_POINT, pi);
	r.appendObjectIDArgument(b->getID());
	Coord3D rp{ site.x, site.y - 300.0f, 0.0f };
	rp.z = game.logic().getGroundHeight(rp.x, rp.y);
	r.appendLocationArgument(rp);
	r.appendBooleanArgument(false);
	r.appendObjectIDArgument(INVALID_ID);
	game.commands().append(r);
	Object *riders = nullptr;
	for (int k = 0; k < 240 && !riders; ++k)
	{
		game.advance(0.5);
		for (Object *ob = game.logic().getFirstObject(); ob; ob = ob->getNextObject())
		{
			if (ob->getTemplate()->getName() == horde && ob->getControllingPlayer() == p1 && ob->getContain() && ob->getContain()->getContainCount() > 0)
			{
				riders = ob;
			}
		}
	}
	if (!riders)
	{
		return out;
	}
	out.produced = true;
	for (int k = 0; k < 40; ++k)
	{
		game.advance(0.5); // it reaches its rally point and settles
	}
	Coord3D vp = *riders->getPosition();
	vp.x += 300.0f;
	vp.z = game.logic().getGroundHeight(vp.x, vp.y);
	// the pathfinder's own answer to the crush attack's goal (the victim + 100 east, on the cliff): FindClosestPath reaches past the victims to the cliff's edge
	{
		Pathfinder &pf = game.ai().pathfinder();
		AIUpdateInterface *ai = riders->getAIUpdateInterface();
		REQUIRE(ai);
		const Coord3D from = *riders->getPosition();
		Coord3D goal{ vp.x + 100.0f, vp.y, vp.z };
		out.startX = from.x;
		out.cliffGoalX = goal.x;
		std::unique_ptr<Path> p(pf.findClosestPath(&ai->pathfindObject(), ai->locomotorInfo(), &from, &goal, false, 0.0f, false));
		out.closestPathFound = p && p->getLastNode();
		if (out.closestPathFound)
		{
			out.closestPathEndX = p->getLastNode()->getPosition()->x;
		}
	}
	Object *v = game.createObject(victim, p2->getPlayerIndex(), vp, 3.14159265f, &err);
	REQUIRE_MESSAGE(v, err);
	game.advance(0.2);
	std::vector<ObjectID> members;
	for (const Object *mo : *v->getContain()->getContainedItemsList())
	{
		members.push_back(mo->getID());
	}
	out.victimsStart = aliveMembers(game, members);
	const CombatState::Counters before = game.logic().combat().counters();
	GameMessage atk(MSG_DO_ATTACK_OBJECT, pi);
	atk.appendObjectIDArgument(v->getID());
	send(game, pi, riders->getID(), atk);
	for (int k = 0; k < 60; ++k)
	{
		game.advance(0.25);
	}
	const CombatState::Counters &c = game.logic().combat().counters();
	out.crushes = c.crushes - before.crushes;
	out.shots = c.crushWeaponShots - before.crushWeaponShots;
	out.knockbacks = c.crushKnockbacks - before.crushKnockbacks;
	out.ramHits = c.ramHits - before.ramHits;
	out.victimsEnd = aliveMembers(game, members);
	std::printf("  info: trained %s -> %s: %llu crushes, %llu crush weapon shots, %llu knockbacks, %llu ram hits, victims %zu -> %zu; closest path from x %.0f toward "
				"x %.0f ends at x %.0f\n",
		horde, victim, out.crushes, out.shots, out.knockbacks, out.ramHits, out.victimsStart, out.victimsEnd, out.startX, out.cliffGoalX, out.closestPathEndX);
	return out;
}
} // namespace

TEST_CASE("warg1 retail: Isengard warg riders trained at a level 2 warg pit and sent with a player's attack order trample a Gondor soldier horde")
{
	OPENBFME_REQUIRE_START(s);
	const Trained t = trainAndCharge(s, "FactionIsengard", "IsengardWargPit", "Upgrade_IsengardWargPitLevel2", "IsengardWargRiderHorde", "FactionMen", "GondorFighterHorde");
	REQUIRE(t.produced);
	// engine pins (measured on the lane's tree; before lane WARG-1: 0 crushes, the soldiers killed in melee)
	CHECK(t.victimsStart == 15);
	CHECK(t.crushes == 11);
	CHECK(t.shots == 22);      // WargCrush, and the soldiers' revenge weapon (they face the charge)
	CHECK(t.knockbacks == 11); // the horde's CrushKnockback 40 (parseVelocityReal: 8 per frame)
	CHECK(t.ramHits == 11);    // the member's RamPower 30
	CHECK(t.victimsEnd == 0);
	// FindClosestPath (RW 0x6FB869) toward the point on the cliff: the path ends at x 1255 (the stepped, corridor-adjusted destination), not in the cell next
	// to the horde (x 965 before)
	REQUIRE(t.closestPathFound);
	CHECK(t.closestPathEndX == 1255.0f);
}

TEST_CASE("warg1 retail: Rohirrim trained at a level 2 stable and sent with a player's attack order trample a Mordor orc horde")
{
	OPENBFME_REQUIRE_START(s);
	const Trained t = trainAndCharge(s, "FactionMen", "GondorStable", "Upgrade_GondorStableLevel2", "RohanRohirrimHorde", "FactionMordor", "MordorFighterHorde");
	REQUIRE(t.produced);
	// engine pins (measured on the lane's tree; 0 crushes before lane WARG-1)
	CHECK(t.victimsStart == 20);
	CHECK(t.crushes == 14);
	CHECK(t.shots == 28);
	CHECK(t.knockbacks == 14);
	CHECK(t.victimsEnd == 0);
}
