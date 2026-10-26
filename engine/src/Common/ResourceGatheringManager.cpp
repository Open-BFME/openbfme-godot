// OpenBFME. GPL-3.0.
// See Common/ResourceGatheringManager.h for the target facts.

#include "Common/ResourceGatheringManager.h"

#include "Common/Player.h"
#include "Common/StateHash.h"
#include "Common/Xfer.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Map/TerrainTrees.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/DockUpdate.h"
#include "GameLogic/Module/SupplyTruckAIUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/SnapshotXfer.h"
#include "GameLogic/System/ShroudManager.h"

#include <cfloat>

namespace
{
const float kNoCost = FLT_MAX;                // RW 0xBD1910
const float kWarehouseStartLimit = 100000.0f; // RW 0xBDCF20

int statusIndex(const char *name)
{
	return ObjectTemplateInfoBuilder::objectStatusIndex(name);
}

SupplyTruckAIInterface *supplyOf(Object *o)
{
	AIUpdateInterface *ai = o ? o->getAIUpdateInterface() : nullptr;
	return ai ? ai->getSupplyTruckAIInterface() : nullptr;
}

// RW 0x5E3BE4 (this = the worker): x87 PC24 ((dz^2 + dy^2) + dx^2) of the worker's position minus `p`
double distSqTo(const Object &obj, const Coord3D &p)
{
	const Coord3D *o = obj.getPosition();
	const double dx = SimMath::pc24SubW((double)o->x, (double)p.x), dy = SimMath::pc24SubW((double)o->y, (double)p.y), dz = SimMath::pc24SubW((double)o->z, (double)p.z);
	return SimMath::pc24AddW(SimMath::pc24AddW(SimMath::pc24MulW(dz, dz), SimMath::pc24MulW(dy, dy)), SimMath::pc24MulW(dx, dx));
}

bool hasModule(const Object &o, const char *className)
{
	return o.findModule(className) != nullptr; // RW 0x68BDA5 with the class's name key
}

void eraseFirst(std::list<ObjectID> &l, std::list<ObjectID>::iterator it)
{
	l.erase(it);
}
} // namespace

std::string ResourceGatheringManager::stopLine()
{
	return "[S-5202] ResourceGatheringManager (lane LUMBER1, Player + 0x304): SupplyWarehouseDockUpdate / SupplyWarehouseCreate are not ported (no RotWK retail "
		   "object uses them): a warehouse dock is refused by canTransferSuppliesAt (RW 0x82C5A6 reads its boxes); findBestSupplyCenter's erase of a missing "
		   "center through the warehouse list (RW 0x8FACB1, ZH's bug) removes the node, the warehouse list's size counter it decrements is not modelled";
}

void ResourceGatheringManager::reset()
{
	m_supplyWarehouses.clear();
	m_supplyCenters.clear();
}

// RW 0x8FAA11
void ResourceGatheringManager::addSupplyCenter(Object *center)
{
	if (center)
	{
		m_supplyCenters.push_back(center->getID());
	}
}

// RW 0x8FAA31
void ResourceGatheringManager::addSupplyWarehouse(Object *warehouse)
{
	if (warehouse)
	{
		m_supplyWarehouses.push_back(warehouse->getID());
	}
}

// RW 0x82C5A6
bool ResourceGatheringManager::canTransferSuppliesAt(Object *worker, Object *dock)
{
	if (!worker || !dock || dock->isEffectivelyDead())
	{
		return false;
	}
	static const int underConstruction = statusIndex("UNDER_CONSTRUCTION");
	static const int sold = statusIndex("SOLD");
	if (worker->testStatus((unsigned)underConstruction) || dock->testStatus((unsigned)underConstruction) || dock->testStatus((unsigned)sold))
	{
		return false;
	}
	SupplyTruckAIInterface *sup = supplyOf(worker); // AI vslot 0x178
	if (!sup)
	{
		return false;
	}
	const bool warehouse = hasModule(*dock, "SupplyWarehouseDockUpdate");
	if (warehouse)
	{
		// RW 0x82C66C: the warehouse's boxes (+ 0x88) and the relationship (RW 0x68D7AB): SupplyWarehouseDockUpdate is not ported (S-5202)
		dock->logic().noteStop(stopLine());
		return false;
	}
	const bool center = hasModule(*dock, "SupplyCenterDockUpdate");
	if (center)
	{
		if (sup->getNumberBoxes() == 0 || worker->getControllingPlayer() != dock->getControllingPlayer())
		{
			return false; // RW 0x82C6CC .. 0x82C6EE
		}
	}
	if (!center)
	{
		return false; // RW 0x82C6F3
	}
	if (!sup->isAvailableForSupplying())
	{
		return false;
	}
	const Player *p = worker->getControllingPlayer();
	if (p && p->getPlayerType() == PLAYER_HUMAN)
	{
		// RW 0x82C713 .. 0x82C726: Object RW 0x68D8F7 with the player's index: SHROUDED (4) refuses
		if (ShroudManager *shroud = dock->logic().shroud())
		{
			if (shroud->getObjectStatus(*dock, p->getPlayerIndex()) == OBJECTSHROUD_SHROUDED)
			{
				return false;
			}
		}
	}
	return true;
}

// RW 0x8FA7DA
float ResourceGatheringManager::computeRelativeCost(Object *worker, Object *dock)
{
	if (!worker || !dock || !canTransferSuppliesAt(worker, dock))
	{
		return kNoCost;
	}
	DockUpdateInterface *d = dock->getDockUpdateInterface(); // RW 0x68C383 (a center has one: canTransferSuppliesAt found its module)
	if (!d || !d->isClearToApproach(*worker))
	{
		return kNoCost;
	}
	return SimMath::fstpDword(distSqTo(*worker, *dock->getPosition()));
}

// RW 0x8FABC8
Object *ResourceGatheringManager::findBestSupplyCenter(Object *worker)
{
	if (!worker || !worker->getAIUpdateInterface())
	{
		return nullptr;
	}
	GameLogic &logic = worker->logic();
	if (SupplyTruckAIInterface *sup = supplyOf(worker))
	{
		Object *preferred = logic.findObjectByID(sup->getPreferredDockID()); // RW 0x449681
		if (preferred && hasModule(*preferred, "SupplyCenterDockUpdate") && computeRelativeCost(worker, preferred) != kNoCost)
		{
			return preferred;
		}
	}
	Object *best = nullptr;
	float bestCost = kNoCost;
	for (auto it = m_supplyCenters.begin(); it != m_supplyCenters.end();)
	{
		Object *c = logic.findObjectByID(*it);
		if (!c)
		{
			auto dead = it++;
			eraseFirst(m_supplyCenters, dead); // RW 0x8FACB1 (through the warehouse list's object: S-5202)
			logic.noteStop(stopLine());
			continue;
		}
		const float cost = computeRelativeCost(worker, c);
		if (cost < bestCost)
		{
			best = c;
			bestCost = cost;
		}
		++it;
	}
	return best;
}

// RW 0x8FAA51
Object *ResourceGatheringManager::findBestSupplyWarehouse(Object *worker)
{
	if (!worker || !worker->getAIUpdateInterface())
	{
		return nullptr;
	}
	GameLogic &logic = worker->logic();
	float maxDistSq = kWarehouseStartLimit;
	if (SupplyTruckAIInterface *sup = supplyOf(worker))
	{
		Object *preferred = logic.findObjectByID(sup->getPreferredDockID());
		if (preferred && hasModule(*preferred, "SupplyWarehouseDockUpdate") && computeRelativeCost(worker, preferred) != kNoCost)
		{
			return preferred;
		}
		const float scan = sup->getWarehouseScanDistance();
		maxDistSq = SimMath::fstpDword(SimMath::pc24MulW((double)sup->getWarehouseScanDistance(), (double)scan)); // RW 0x8FAB22 .. 0x8FAB31
	}
	Object *best = nullptr;
	float bestCost = kNoCost;
	for (auto it = m_supplyWarehouses.begin(); it != m_supplyWarehouses.end();)
	{
		Object *w = logic.findObjectByID(*it);
		if (!w)
		{
			auto dead = it++;
			eraseFirst(m_supplyWarehouses, dead); // RW 0x702055
			continue;
		}
		const float cost = computeRelativeCost(worker, w); // the pure squared distance is the cost when the dock can be used
		if (cost < bestCost && maxDistSq > cost)
		{
			best = w;
			bestCost = cost;
		}
		++it;
	}
	return best;
}

// RW 0x8FA834
bool ResourceGatheringManager::findHarvestPosition(Object *worker, Coord3D &out)
{
	AIUpdateInterface *ai = worker ? worker->getAIUpdateInterface() : nullptr;
	SupplyTruckAIInterface *sup = ai ? ai->getSupplyTruckAIInterface() : nullptr;
	if (!sup || !sup->isHarvestTrees())
	{
		return false;
	}
	GameLogic &logic = worker->logic();
	Coord3D centre;
	if (!sup->getHarvestPosition(centre)) // slot 0x40: the PLAYER-given position when set
	{
		centre = *worker->getPosition();
	}
	const float radius = sup->getWarehouseScanDistance();
	AIWorld *world = logic.aiWorld();
	// the collector RW 0x67D5A0: the worker's quick path (RW 0x6F5BB0 with its locomotor) and ground line (RW 0x6EE5E3) to the tree
	auto reachable = [&](const TerrainTrees::Record &t) {
		if (!world || !world->mapReady())
		{
			return true; // a logic without a pathfinder map (unit tests): nothing to test against
		}
		const PathfindLocomotorInfo loco = ai->locomotorInfo();
		return world->pathfinder().clientSafeQuickDoesPathExist(loco, worker->getPosition(), &t.position) &&
		       world->pathfinder().isGroundLineClear(*worker->getPosition(), t.position);
	};
	const TerrainTrees::Record *tree = logic.trees().findClosestTree(centre, radius, false, 2, reachable); // RW 0x67F55F -> 0x67E323(.., 0, 2)
	if (!tree)
	{
		return false;
	}
	out = tree->position;
	return true;
}

void ResourceGatheringManager::crc(StateHasher &h) const
{
	h.addU32((std::uint32_t)m_supplyWarehouses.size());
	for (ObjectID id : m_supplyWarehouses)
	{
		h.addU32(id);
	}
	h.addU32((std::uint32_t)m_supplyCenters.size());
	for (ObjectID id : m_supplyCenters)
	{
		h.addU32(id);
	}
}

// RW 0x8FAD17: version 1, the warehouses, the centers (RW 0x76C8A7: the count, then each id)
void ResourceGatheringManager::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	for (std::list<ObjectID> *l : { &m_supplyWarehouses, &m_supplyCenters })
	{
		std::uint32_t n = (std::uint32_t)l->size();
		x.xferCount(&n, 4, sizeof(ObjectID) + 2 * sizeof(void *));
		if (x.isLoading())
		{
			l->clear();
			for (std::uint32_t i = 0; i < n; ++i)
			{
				ObjectID id = INVALID_ID;
				x.xferObjectID(&id);
				l->push_back(id);
			}
		}
		else
		{
			for (ObjectID id : *l)
			{
				x.xferObjectID(&id);
			}
		}
	}
}
