// OpenBFME unit tests, lanes WALL-1 / WALL-2 / SIEGE-1 with upstream's SAVE-4: a walled map's snapshot. GPL-3.0.
//
// Helm's Deep (wall tops and ramps in the ground grid's 7-bit layers, the raised walkway over the gate, its portal waypoints): the game saved while a
// horde walks on the Deeping Wall loads into itself (LiveGame::loadSnapshot) and every following frame hashes as the run that was not loaded; the
// pathfinder's wall pieces, wall heights and waypoints come back. The same with a siege ladder deployed on the wall (lane SIEGE-1): the ladder's
// portals and the siege modules come back too. SKIPs loudly without the installs.

#include "doctest.h"

#include "HudTestUtil.h"

#include "Common/SpecialPower.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GameLogicDispatch.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/PortalModules.h"
#include "GameLogic/Module/SiegeModules.h"
#include "GameLogic/Object/Object.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

using namespace hudtest;

TEST_CASE("walls snapshot retail: Helm's Deep with a horde on its wall top saves and loads into itself; the wall pieces, heights and waypoints come "
		  "back and the next 100 frames hash as before")
{
	if (!haveWorld("walls snapshot"))
	{
		return;
	}
	Rig r(shared(), "map wor helms deep", "FactionMen");
	const auto worldContext = shared().world->enterContext();
	r.frame(3);
	Pathfinder &pf = r.logic().aiWorld()->pathfinder();
	REQUIRE(pf.wallPieceCount() > 0); // tolerance: a precondition (Helm's Deep's wall pieces exist; their count is the map's, compared after the load)
	// a horde ordered onto the Deeping Wall's top (the highest walkable layer at the point, as a right click on the wall gives)
	const Coord3D onWall{ 1645.0f, 2938.0f, 0.0f };
	REQUIRE(isWallLayer(pf.getLayerAt(onWall.x, onWall.y)));
	const Coord3D from = r.freeSpot(1640.0f, 3150.0f, 60.0f);
	Object *horde = r.make("GondorFighterHorde", from.x, from.y);
	r.frame(2);
	REQUIRE(horde->getAIUpdateInterface());
	Coord3D goal = onWall;
	goal.z = r.logic().getHighestLayerHeight(goal.x, goal.y);
	horde->getAIUpdateInterface()->aiMoveToPosition(goal, CMD_FROM_PLAYER);
	r.frame(60);
	const size_t pieces = pf.wallPieceCount(), waypoints = pf.waypointCount();
	const std::vector<float> heights = pf.wallHeights();

	std::vector<std::uint8_t> snapshot;
	std::string error;
	REQUIRE_MESSAGE(r.logic().saveSnapshot(snapshot, &error), error);
	const UnsignedInt savedFrame = r.logic().getFrame();
	const std::uint32_t savedHash = r.logic().computeStateHash();
	std::vector<std::uint32_t> hashes;
	for (int i = 0; i < 100; ++i)
	{
		r.frame(1);
		hashes.push_back(r.logic().computeStateHash());
	}
	REQUIRE_MESSAGE(r.game->loadSnapshot(snapshot, &error), error);
	CHECK(r.logic().getFrame() == savedFrame);
	CHECK(r.logic().computeStateHash() == savedHash);
	Pathfinder &loaded = r.logic().aiWorld()->pathfinder();
	CHECK(loaded.wallPieceCount() == pieces);
	CHECK(loaded.wallHeights() == heights);
	CHECK(loaded.waypointCount() == waypoints);
	int same = 0;
	for (size_t i = 0; i < hashes.size(); ++i)
	{
		r.frame(1);
		same += r.logic().computeStateHash() == hashes[i] ? 1 : 0;
	}
	CHECK(same == (int)hashes.size());

	// review of #63: the wall heights are in the state hash. The same snapshot with its first wall height 1 higher loads with every cell as saved (the
	// heights only place units on the wall tops) and another state hash
	REQUIRE(!heights.empty());
	std::vector<std::uint8_t> run(4 + 4 * heights.size()); // the heights in the snapshot: their count, then each one (Pathfinder::xferWalls)
	const std::uint32_t count = (std::uint32_t)heights.size();
	std::memcpy(run.data(), &count, 4);
	std::memcpy(run.data() + 4, heights.data(), 4 * heights.size());
	std::vector<std::uint8_t> raised = snapshot;
	const auto at = std::search(raised.begin(), raised.end(), run.begin(), run.end());
	REQUIRE(at != raised.end());
	REQUIRE(std::search(at + 1, raised.end(), run.begin(), run.end()) == raised.end()); // the run is the heights' (it is found once)
	const float higher = heights[0] + 1.0f;
	std::memcpy(&*(at + 4), &higher, 4);
	REQUIRE_MESSAGE(r.game->loadSnapshot(raised, &error), error);
	CHECK(r.logic().aiWorld()->pathfinder().wallHeights()[0] == higher);
	CHECK(r.logic().computeStateHash() != savedHash);
}

TEST_CASE("walls snapshot retail: Helm's Deep with a siege ladder deployed on its wall saves and loads into itself; the walls, the raised walkway, the "
		  "ladder's portals and the siege modules come back and the next 100 frames hash as before")
{
	if (!haveWorld("walls snapshot"))
	{
		return;
	}
	Rig r(shared(), "map wor helms deep", "FactionIsengard");
	const auto worldContext = shared().world->enterContext();
	r.frame(3);
	r.game->shroud().revealMapForPlayerPermanently(r.index());
	Pathfinder &pf = r.logic().aiWorld()->pathfinder();
	REQUIRE(pf.wallPieceCount() > 0); // tolerance: a precondition (Helm's Deep's wall pieces exist; their count is the map's, compared after the load)
	// a ladder at the first wall with a SIEGELADDER dock point (as the SIEGE-1 deploy test), ordered to deploy there
	Object *wall = nullptr;
	SiegeDockingBehavior::Entry entry;
	for (Object *o = r.logic().getFirstObject(); o && !wall; o = o->getNextObject())
	{
		const SiegeDockingBehavior *d = SiegeDockingBehavior::of(*o);
		for (size_t i = 0; d && i < d->entries().size() && !wall; ++i)
		{
			if (d->entries()[i].type == 1)
			{
				wall = o;
				entry = d->entries()[i];
			}
		}
	}
	REQUIRE(wall);
	const Coord3D from = r.freeSpot(entry.position.x - entry.direction.x * 250.0f, entry.position.y - entry.direction.y * 250.0f, 60.0f);
	Object *ladder = r.make("IsengardSiegeLadder", from.x, from.y);
	r.frame(2);
	SiegeDeploySpecialPower *deploy = SiegeDeploySpecialPower::of(*ladder);
	REQUIRE(deploy);
	const SpecialPowerTemplate *t = TheSpecialPowerStore->findSpecialPowerTemplate("SpecialAbilitySiegeDeploy");
	REQUIRE(t);
	r.game->dispatch().dispatch([&] {
		r.local->selection() = { ladder->getID() };
		GameMessage m(MSG_DO_SPECIAL_POWER_AT_OBJECT, r.index());
		m.appendIntegerArgument((int)t->getID());
		m.appendObjectIDArgument(wall->getID());
		m.appendIntegerArgument(0);
		m.appendObjectIDArgument(INVALID_ID);
		return m;
	}());
	for (int f = 0; f < 300 && deploy->phase() != SiegeDeploySpecialPower::PHASE_DEPLOYED; ++f)
	{
		r.frame(1);
	}
	REQUIRE(deploy->phase() == SiegeDeploySpecialPower::PHASE_DEPLOYED);
	r.frame(10);
	const DynamicPortalBehaviour *portal = DynamicPortalBehaviour::of(*ladder);
	REQUIRE(portal);
	REQUIRE(portal->created());
	const size_t waypoints = pf.waypointCount(), pieces = pf.wallPieceCount();
	REQUIRE(waypoints > 0); // tolerance: a precondition (the deployed ladder made its climb's waypoints; their count is compared after the load)
	const std::vector<float> heights = pf.wallHeights();
	const ObjectID ladderId = ladder->getID(); // the load makes the objects again: the pointers go

	std::vector<std::uint8_t> snapshot;
	std::string error;
	REQUIRE_MESSAGE(r.logic().saveSnapshot(snapshot, &error), error);
	const UnsignedInt savedFrame = r.logic().getFrame();
	const std::uint32_t savedHash = r.logic().computeStateHash();
	std::vector<std::uint32_t> hashes;
	for (int i = 0; i < 100; ++i)
	{
		r.frame(1);
		hashes.push_back(r.logic().computeStateHash());
	}
	REQUIRE_MESSAGE(r.game->loadSnapshot(snapshot, &error), error);
	CHECK(r.logic().getFrame() == savedFrame);
	CHECK(r.logic().computeStateHash() == savedHash);
	Pathfinder &loaded = r.logic().aiWorld()->pathfinder();
	CHECK(loaded.wallPieceCount() == pieces);
	CHECK(loaded.wallHeights() == heights);
	CHECK(loaded.waypointCount() == waypoints);
	Object *again = r.logic().findObjectByID(ladderId);
	REQUIRE(again);
	REQUIRE(SiegeDeploySpecialPower::of(*again));
	CHECK(SiegeDeploySpecialPower::of(*again)->phase() == SiegeDeploySpecialPower::PHASE_DEPLOYED);
	REQUIRE(DynamicPortalBehaviour::of(*again));
	CHECK(DynamicPortalBehaviour::of(*again)->created());
	int same = 0;
	for (size_t i = 0; i < hashes.size(); ++i)
	{
		r.frame(1);
		same += r.logic().computeStateHash() == hashes[i] ? 1 : 0;
	}
	CHECK(same == (int)hashes.size());
}
