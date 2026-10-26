// OpenBFME. GPL-3.0.
// See GameLogic/BeaconCommands.h (lane CHAT-1).

#include "GameLogic/BeaconCommands.h"

#include "Common/BuildAssistant.h"
#include "Common/Player.h"
#include "Common/PlayerTemplate.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GameLogicDispatch.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/VictoryConditions.h"


namespace
{
const char *const kLane = "CHAT-1";
}

const ThingTemplate *BeaconCommands::beaconTemplate(const GameLogic &logic, const Player &player)
{
	const PlayerTemplate *pt = player.getPlayerTemplate();
	return pt && !pt->m_beaconName.empty() ? logic.things().findTemplate(pt->m_beaconName) : nullptr; // RW 0x6D1305 (PlayerTemplate + 0x17C)
}

int BeaconCommands::countBeacons(const GameLogic &logic, const Player &player, const ThingTemplate &beacon)
{
	int count = 0;
	for (const Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() == &player && !o->isDestroyed() && BuildAssistant::isEquivalentTo(&beacon, o->getTemplate()))
		{
			++count;
		}
	}
	return count;
}

bool BeaconCommands::clampBeaconLocation(const GameLogic &logic, Coord3D &pos)
{
	// RW 0x67DFCE (TheTerrainLogic null: false)
	const TerrainLogic *terrain = logic.terrain();
	AIWorld *ai = logic.aiWorld();
	const PathfindTerrain *view = ai ? ai->pathfinder().terrainView() : nullptr;
	if (!terrain || !view)
	{
		return false;
	}
	float hiX = 0.0f, hiY = 0.0f;
	if (!terrain->getExtent(0, hiX, hiY))
	{
		hiX = hiY = 0.0f; // RW 0x462637 without boundaries: hi (0, 0)
	}
	const float loX = 0.0f, loY = 0.0f;
	if (pos.x <= loX || hiX <= pos.x || pos.y <= loY || hiY <= pos.y)
	{
		// RW 0x67D949: the nearest side, in the order low y, high x, high y, low x (a strictly smaller distance wins)
		// RW 0x67D961 ..: `fld a; fsub b` (x87, PC24), `fstp qword`, MSVCR71 fabs (RW 0xA3CF8A), `fstp dword`
		auto dist = [](float a, float b) { return SimMath::fstpDword(SimMath::absD(SimMath::pc24SubW((double)a, (double)b))); };
		const float d[4] = { dist(pos.y, loY), dist(pos.x, hiX), dist(pos.y, hiY), dist(pos.x, loX) };
		int best = 0;
		float bestD = d[0];
		for (int i = 1; i < 4; ++i)
		{
			if (d[i] < bestD)
			{
				bestD = d[i];
				best = i;
			}
		}
		float x = pos.x, y = pos.y;
		switch (best)
		{
		case 0:
			y = loY;
			break;
		case 1:
			x = hiX;
			break;
		case 2:
			y = hiY;
			break;
		default:
			x = loX;
			break;
		}
		pos.x = x;
		pos.y = y;
		pos.z = terrain->getGroundHeight(x, y); // vslot 0x18 (x, y, no normal)
	}
	return !view->isUnderwater(pos.x, pos.y, nullptr, nullptr); // vslot 0x4C (RW 0x67DAE6)
}

bool BeaconCommands::placeBeacon(GameLogic &logic, const GameMessage &m)
{
	// RW 0x77C634
	const GameMessageArgument *arg = m.getArgument(0);
	if (!arg || arg->type != ARGUMENTDATATYPE_LOCATION)
	{
		return false; // a malformed message (retail reads the argument unchecked)
	}
	Coord3D pos = arg->location;
	if (!logic.terrain() || !logic.aiWorld() || !logic.aiWorld()->pathfinder().terrainView())
	{
		logic.reportError("MSG_PLACE_BEACON: the game has no map terrain to place a beacon on"); // RW 0x67DFCE answers false without TheTerrainLogic
		return false;
	}
	if (!clampBeaconLocation(logic, pos))
	{
		return true; // under water: dropped without a word (RW 0x77C651)
	}
	Player *player = logic.players().getNthPlayer(m.getPlayerIndex());
	if (!player)
	{
		return true;
	}
	const ThingTemplate *beacon = beaconTemplate(logic, *player);
	if (!beacon || logic.victory().hasSinglePlayerBeenDefeated(player)) // TheVictoryConditions vslot 0x40
	{
		logic.noteBeacon(GameLogic::BeaconNotice::FAILED, player->getPlayerIndex(), INVALID_ID, pos);
		return true;
	}
	if (countBeacons(logic, *player, *beacon) >= logic.settings().maxBeaconsPerPlayer) // RW 0x77C6C6
	{
		logic.noteBeacon(GameLogic::BeaconNotice::TOO_MANY, player->getPlayerIndex(), INVALID_ID, pos);
		return true;
	}
	Object *obj = logic.newObject(beacon, player->getDefaultTeam(), ObjectStatusMaskType{}); // RW 0x6D165E
	if (!obj)
	{
		logic.reportError("MSG_PLACE_BEACON: newObject made nothing for " + beacon->getName());
		return false;
	}
	obj->setPosition(&pos); // RW 0x70C201
	obj->setProducer(nullptr); // RW 0x68B6A1
	logic.noteBeacon(GameLogic::BeaconNotice::PLACED, player->getPlayerIndex(), obj->getID(), *obj->getPosition());
	return true;
}

void BeaconCommands::registerHandlers(GameLogicDispatch &d)
{
	d.registerHandler(MSG_PLACE_BEACON, kLane, [](GameLogic &l, const GameMessage &m) { return placeBeacon(l, m); });
}

std::vector<std::string> BeaconCommands::stopLines()
{
	return { "[S-5151] beacons (CHAT-1, RW 0x77C634 / 0x8CDCE9): placement, the limit, Delete, the minimap pulse and the allies' notice are ported; not ported: "
		"MSG_REMOVE_BEACON / MSG_SET_BEACON_TEXT (captions, never sent by retail), the smoke (no retail system matches), the hidden beacon's z -100, a loaded "
		"save's beacon pulses" };
}
