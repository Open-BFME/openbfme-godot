// OpenBFME. GPL-3.0.
//
// What SiegeDeploySpecialPower (lane SIEGE-1, SiegeModules.h) asks a siege ladder's / tower's DynamicPortalBehaviour (lane WALL-2, PortalModules.h, which
// implements it). Its own header so the two modules meet without including each other. TARGET FACTS (RotWK, BFME2 counterparts in Open-BFME-2
// Code/GameEngine/Source/GameLogic/Object/Behavior/DynamicPortalBehaviour*.cpp):
//   * RW 0x863983 (BFME2 0x4608E0): the object's DynamicPortalBehaviour module, null when it has none;
//   * RW 0x863EB3 (BFME2 0x460DF6, WorldBuilder name IsEndPortalAboveWall): the bone of the waypoint AboveWall (the BonePrefix bones, NumberOfBones, world
//     space) is on a wall (Pathfinder::IsPointOnWall(point, false)); `out` (may be null) receives the point; an AboveWall out of the waypoint list answers true
//     with the object's position;
//   * RW 0x86404D (BFME2 0x460F90): the deployed ladder's / tower's waypoints and portal links are made (the climb joins the pathfinder);
//   * RW 0x8639EF (BFME2 0x46094C): the waypoints made are removed again (on leaving the deployed phase and when the object dies);
//   * RW 0x863D77: the world point of TopAttackPos (module data + 0x164 / + 0x168 / + 0x16C: X, Y scaled by the length of the object's planar X axis
//     normalised by its larger component, Z as is, through the object's transform RW 0x70BFD1) and RW 0x863D30: TopAttackRadius (data + 0x170). The melee
//     reach of a unit against a deployed SIEGE_TOWER (RW 0x6EDB6F .. 0x6EDC74, ObjectWeapons) reads them.

#pragma once

#include "Common/INIDataTypes.h"

class SiegePortalInterface
{
public:
	virtual ~SiegePortalInterface() = default;
	virtual bool isEndPortalAboveWall(Coord3D *out) = 0; // RW 0x863EB3
	virtual void createPortalWaypoints() = 0;            // RW 0x86404D
	virtual void clearPortalWaypoints() = 0;             // RW 0x8639EF
	virtual Coord3D topAttackPosition() = 0;             // RW 0x863D77
	virtual float topAttackRadius() const = 0;           // RW 0x863D30
};
