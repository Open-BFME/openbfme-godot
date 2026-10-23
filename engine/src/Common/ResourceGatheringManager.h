// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (Common/RTS/ResourceGatheringManager.cpp).
//
// ResourceGatheringManager (lane LUMBER1): each player's list of the supply centers (the lumber mills) and supply warehouses its workers can use, and the
// three searches a worker makes: where to bring its lumber, where to fetch boxes, which tree to cut.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; lane LUMBER1 research, the bodies read in the disassembly):
//   * Player + 0x304 (made by the Player constructor, RW 0x6B105F; ctor RW 0x8FA930): + 4 the warehouses, + 8 the centers (std::list of object ids).
//   * addSupplyCenter RW 0x8FAA11 / addSupplyWarehouse RW 0x8FAA31: push_back of the object's id (a null object nothing). The remove functions (RW 0x8FA8BA /
//     0x8FA8F5) have no caller: a captured or dead center stays listed; ownership is checked at query time (canTransferSuppliesAt) and a missing object's
//     id is erased by the searches.
//   * computeRelativeCost RW 0x8FA7DA(worker, dock): FLT_MAX (RW 0xBD1910) for a null, for ActionManager::canTransferSuppliesAt (RW 0x82C5A6) false, or when
//     the dock's DockUpdateInterface (RW 0x68C383) refuses isClearToApproach(worker); else the worker's squared 3D distance to the dock (RW 0x5E3BE4: x87
//     PC24, (dz^2 + dy^2) + dx^2 of worker - dock, stored as a float).
//   * canTransferSuppliesAt RW 0x82C5A6(worker, dock): both set, the dock not effectively dead (+ 0x458 bit 0), neither the worker nor the dock UNDER_CONSTRUCTION
//     (status 2), the dock not SOLD (status 0x13), the worker has an AI with a supply interface (AI vslot 0x178); a SupplyWarehouseDockUpdate dock needs boxes
//     and a relationship (not ported: S-5202); a SupplyCenterDockUpdate dock needs the worker to carry boxes and the same controlling player; neither: false;
//     the worker must be available for supplying; a HUMAN worker's player (+ 0x5C == 0) must not see the dock as SHROUDED (Object RW 0x68D8F7 == 4).
//   * findBestSupplyCenter RW 0x8FABC8: the worker's preferred dock (supply slot 0x3C) when it has a SupplyCenterDockUpdate (RW 0x68BDA5) and a cost below
//     FLT_MAX; else the centers in list order, the strictly smallest cost (the first of equal costs). A missing object's node is erased, through the
//     WAREHOUSE list's object (RW 0x8FACB1 `lea ecx, [ebx + 4]`, ZH's bug kept): the node leaves the center list, the warehouse list's size counter is the
//     one decremented (S-5202: that counter is not modelled; no logic reads it, std::list iteration does not).
//   * findBestSupplyWarehouse RW 0x8FAA51: the preferred dock with a SupplyWarehouseDockUpdate and a cost below FLT_MAX; else the warehouses in order whose
//     cost is strictly below the best and strictly below scanDistance^2 (getWarehouseScanDistance twice, x87 fmul, stored float); start limit 100000.0
//     (RW 0xBDCF20, used only without a supply interface).
//   * findHarvestPosition RW 0x8FA834: the worker's supply interface must HarvestTrees; the centre is its PLAYER-given harvest position (slot 0x40) when set,
//     else the worker's position; the tree is TerrainLogic RW 0x67F55F(worker, centre, getWarehouseScanDistance): the query RW 0x67E323 (radius + 7, the
//     buckets, a record with a client id, toppled ones included, IsHarvestable, 3D distance below the radius) with the collector RW 0x67D5A0: a candidate
//     strictly closer in 2D (dy^2 + dx^2, SSE) than the best so far (or the first) is taken only when the pathfinder's quick path test from the worker
//     (RW 0x6F5BB0 with its locomotor) and the ground line test (RW 0x6EE5E3) pass; the answer is that tree's position.
//   * xfer RW 0x8FAD17: version 1, the warehouses then the centers (RW 0x76C8A7). No crc (RotWK has none; the OpenBFME state hash covers the lists).
// DONOR: ZH ResourceGatheringManager.cpp (ZH's cost adds the warehouse box weighting and uses the partition distance; RotWK's does not).

#pragma once

#include "Common/INIDataTypes.h"
#include "GameLogic/ObjectTypes.h"

#include <list>
#include <string>

class GameLogic;
class Object;
class StateHasher;
class Xfer;

class ResourceGatheringManager
{
public:
	void reset();

	void addSupplyCenter(Object *center);       // RW 0x8FAA11
	void addSupplyWarehouse(Object *warehouse); // RW 0x8FAA31

	Object *findBestSupplyCenter(Object *worker);    // RW 0x8FABC8
	Object *findBestSupplyWarehouse(Object *worker); // RW 0x8FAA51
	// RW 0x8FA834: false when the worker does not harvest trees or no tree qualifies
	bool findHarvestPosition(Object *worker, Coord3D &out);

	// RW 0x8FA7DA: FLT_MAX when the worker cannot use the dock
	static float computeRelativeCost(Object *worker, Object *dock);
	// RW 0x82C5A6
	static bool canTransferSuppliesAt(Object *worker, Object *dock);

	const std::list<ObjectID> &supplyCenters() const { return m_supplyCenters; }
	const std::list<ObjectID> &supplyWarehouses() const { return m_supplyWarehouses; }

	void crc(StateHasher &h) const;
	void xfer(Xfer &x); // RW 0x8FAD17
	static std::string stopLine(); ///< [S-5202]

private:
	std::list<ObjectID> m_supplyWarehouses; ///< + 4
	std::list<ObjectID> m_supplyCenters;    ///< + 8
};
