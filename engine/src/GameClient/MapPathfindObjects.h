// OpenBFME. GPL-3.0.
//
// The structures of a map as pathfinder objects (lane PATH-1): for every full object of the object loop whose template is a
// STRUCTURE (and the fences), a PathfindObject carrying the position, orientation, geometry and the KindOf bits the footprint
// rules read. Mobile units are not built: the live object layer (lane LOGIC-1) owns them and implements PathfindObject itself.
//
// STOP S-164 (docs/STOPS.md): the geometry of a template is read from its Geometry / GeometryMajorRadius / GeometryMinorRadius /
// GeometryHeight / GeometryIsSmall field slots (the last line of each wins); AdditionalGeometry shapes and the status tests of the
// retail footprint routine are not modelled.

#pragma once

#include "GameClient/MapObjectDrawables.h"
#include "GameLogic/AI/AIPathfindHost.h"

#include <memory>
#include <string>
#include <vector>

class MapPathfindObject : public PathfindObject
{
public:
	PathfindObjectID id = 0;
	std::string templateName;
	Coord3D position;
	float orientation = 0.0f;
	PathfindGeometry geometry;
	bool kinds[PK_COUNT] = {};
	float fenceWidth = 0.0f, fenceXOffset = 0.0f, pathfindDiameter = -1.0f;
	bool mobile = false;

	PathfindObjectID getID() const override { return id; }
	const Coord3D &getPosition() const override { return position; }
	float getOrientation() const override { return orientation; }
	PathfindLayerEnum getLayer() const override { return LAYER_GROUND; }
	float getHeightAboveTerrain() const override { return 0.0f; }
	bool isMobile() const override { return mobile; }
	bool isKindOf(PathfindKind k) const override { return kinds[(int)k]; }
	const PathfindGeometry &getGeometry() const override { return geometry; }
	float getFenceWidth() const override { return fenceWidth; }
	float getFenceXOffset() const override { return fenceXOffset; }
	float getPathfindDiameter() const override { return pathfindDiameter; }
	bool isRubble() const override { return false; }
	bool isComputerControlled() const override { return false; }
	unsigned getCrusherLevel() const override { return 0; }
	PathfindRelationship getRelationship(const PathfindObject &) const override { return PATHFIND_NEUTRAL; }
	bool canCrushOrSquish(const PathfindObject &) const override { return false; }
	PathfindObjectID getIgnoredObstacleID() const override { return PATHFIND_INVALID_ID; }
	bool canPathThroughUnits() const override { return false; }
	bool isAircraftThatAdjustsDestination() const override { return false; }
	bool isDoingGroundMovement() const override { return true; }
};

struct MapPathfindObjectSet
{
	std::vector<std::unique_ptr<MapPathfindObject>> objects;
	std::vector<PathfindObject *> pointers() const;
	size_t structures = 0, fences = 0; ///< counts by kind of footprint
	std::vector<std::string> errors;   ///< a template geometry that does not parse is an error, never a default
};

namespace MapPathfindObjects
{
// The footprint objects of the object loop's output (full objects only: fates MAPOBJ_OBJECT and MAPOBJ_OBJECT_BRIDGE).
void build(const MapObjectDrawables &drawables, MapPathfindObjectSet &out);
// The KindOf-derived flags of a template info (exposed for tests).
void kindsOf(const MapTemplateInfo &info, bool (&kinds)[PK_COUNT]);
} // namespace MapPathfindObjects
