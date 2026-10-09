// OpenBFME unit tests. GPL-3.0.
// Synthetic terrain, objects and a world for the pathfinder tests (lane PATH-1): no retail data.

#pragma once

#include "GameLogic/AI/AIPathfind.h"

#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace pathtest
{

// A rectangular terrain: cells 0..w-1 x 0..h-1 of 10 units, flat unless a height function is given.
class SyntheticTerrain : public PathfindTerrain
{
public:
	SyntheticTerrain(int w, int h) : m_w(w), m_h(h) {}

	int m_w, m_h;
	std::set<std::pair<int, int>> cliff;     // cells (cell index) the cliff plane marks
	std::set<std::pair<int, int>> bit18, bit21;
	struct Water
	{
		float x0, y0, x1, y1, z; // rectangle, standing water at z
	};
	std::vector<Water> water;
	float (*heightFn)(float x, float y) = nullptr;

	void getMaximumPathfindExtent(float &loX, float &loY, float &hiX, float &hiY) const override
	{
		loX = 0.0f;
		loY = 0.0f;
		hiX = (float)m_w * 10.0f;
		hiY = (float)m_h * 10.0f;
	}
	void getExtent(float &loX, float &loY, float &hiX, float &hiY) const override { getMaximumPathfindExtent(loX, loY, hiX, hiY); }
	bool isCliffCell(float x, float y) const override { return cliff.count({ (int)(x / 10.0f), (int)(y / 10.0f) }) != 0; }
	bool impassableToPlayers(float x, float y) const override { return bit18.count({ (int)(x / 10.0f), (int)(y / 10.0f) }) != 0; }
	bool extraPass(float x, float y) const override { return bit21.count({ (int)(x / 10.0f), (int)(y / 10.0f) }) != 0; }
	float getGroundHeight(float x, float y) const override { return heightFn ? heightFn(x, y) : 0.0f; }
	bool isUnderwater(float x, float y, float *waterZ, float *terrainZ) const override
	{
		for (const Water &w : water)
		{
			if (x >= w.x0 && x < w.x1 && y >= w.y0 && y < w.y1)
			{
				if (waterZ) *waterZ = w.z;
				if (terrainZ) *terrainZ = getGroundHeight(x, y);
				return true;
			}
		}
		return false;
	}
};

// A configurable object.
class TestObject : public PathfindObject
{
public:
	PathfindObjectID id = 1;
	Coord3D pos;
	float angle = 0.0f;
	PathfindGeometry geometry;
	bool structure = false, mobile = false, mine = false, computer = false;
	std::set<PathfindKind> kinds;
	float fenceWidth = 0.0f, fenceX = 0.0f, diameter = -1.0f;
	unsigned crusher = 0;
	bool rubble = false, aircraftAdjust = false, throughUnits = false;
	PathfindObjectID ignored = PATHFIND_INVALID_ID;
	int team = 0;
	float height = 0.0f;
	std::set<PathfindObjectID> crushable;
	bool stationary = true;

	PathfindObjectID getID() const override { return id; }
	const Coord3D &getPosition() const override { return pos; }
	float getOrientation() const override { return angle; }
	PathfindLayerEnum getLayer() const override { return LAYER_GROUND; }
	float getHeightAboveTerrain() const override { return height; }
	bool isMobile() const override { return mobile; }
	bool isKindOf(PathfindKind k) const override { return kinds.count(k) != 0 || (k == PK_STRUCTURE && structure); }
	const PathfindGeometry &getGeometry() const override { return geometry; }
	float getFenceWidth() const override { return fenceWidth; }
	float getFenceXOffset() const override { return fenceX; }
	float getPathfindDiameter() const override { return diameter; }
	bool isRubble() const override { return rubble; }
	bool isComputerControlled() const override { return computer; }
	unsigned getCrusherLevel() const override { return crusher; }
	PathfindRelationship getRelationship(const PathfindObject &other) const override
	{
		const TestObject *o = dynamic_cast<const TestObject *>(&other);
		return (o && o->team == team) ? PATHFIND_ALLIES : PATHFIND_ENEMIES;
	}
	bool canCrushOrSquish(const PathfindObject &other) const override { return crushable.count(other.getID()) != 0; }
	PathfindObjectID getIgnoredObstacleID() const override { return ignored; }
	bool canPathThroughUnits() const override { return throughUnits; }
	bool isAircraftThatAdjustsDestination() const override { return aircraftAdjust; }
	bool isDoingGroundMovement() const override { return !kinds.count(PK_AIRCRAFT); }
	bool hasAI() const override { return mobile; }
	bool isStationary() const override { return stationary; }
};

class TestWorld : public PathfindWorld
{
public:
	std::map<PathfindObjectID, TestObject *> objects;
	unsigned frame = 100;
	PathfindObject *findObjectByID(PathfindObjectID id) const override
	{
		auto it = objects.find(id);
		return it == objects.end() ? nullptr : it->second;
	}
	unsigned getFrame() const override { return frame; }
};

// the values the retail INI files give (default/aidata.ini DeepWaterDepth 6.0; gamedata.ini MaxPathfindCellsPerFrame),
// stated in each test that depends on them
inline PathfindConfig testConfig()
{
	PathfindConfig c;
	c.wadeWaterDepth = 5.0f;
	c.deepWaterDepth = 6.0f;
	c.slopeLimits[0] = c.slopeLimits[1] = 0.0f;
	c.cellsPerFrame = 4000;
	c.adjustDestinationLimit = 400;
	c.adjustHordeMeleeLimit = 200;
	c.patchPathLimit = 2000;
	c.findPathLimit = 15000;
	c.findAttackPathLimit = 2500;
	c.adjustToMeleeLimit = 400;         // gamedata.ini MaxCellsAdjustToMeleeDestination (lane PHYS-1)
	c.findMeleeEngagementLimit = 50;    // gamedata.ini MaxCellsFindMeleeEngagementLocation (lane PHYS-1) // gamedata.ini MaxCellsFindAttackPath (lane PHYS-1)
	c.meleeApproachDist = 48.0f;       // default/aidata.ini MeleeApproachDist (lane PHYS-1)
	c.castleSiegeStandBackDistance = 500.0f; // default/aidata.ini CastleSiegeStandBackDistance (lane PHYS-1 round 6)
	c.meleeApproachTolerance = 20.0f;  // default/aidata.ini MeleeApproachTolerance (lane PHYS-1)
	c.hordesWaitForHordes = true; // default/aidata.ini HordesWaitForHordes Yes (lane PHYS-1)
	c.adjustToPossibleLimit = 400;
	c.examineTowardsGoalLimit = 25000;
	c.cellInfoPoolSize = 30000;
	c.zoneBlockSize = 16;
	return c;
}

inline PathfindLocomotorInfo groundLoco()
{
	PathfindLocomotorInfo l;
	l.validSurfaces = LOCOMOTORSURFACE_GROUND;
	return l;
}

} // namespace pathtest
