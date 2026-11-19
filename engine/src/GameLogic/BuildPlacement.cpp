// OpenBFME. GPL-3.0.
// See GameLogic/BuildPlacement.h for the sources of every rule.

#include "GameLogic/BuildPlacement.h"

#include "Common/Player.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/SimMath.h"
#include "Common/NumericState.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/ConstructionSite.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/CastleModules.h"
#include "GameLogic/Module/DozerAIUpdate.h"
#include "GameLogic/Module/ExitInterface.h"
#include "GameLogic/Module/GarrisonContain.h"
#include "GameLogic/Module/NotifyCrushModules.h"
#include "GameLogic/Module/QueueProductionExitUpdate.h"
#include "GameLogic/Object/ObjectGeometry.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/System/ShroudManager.h"
#include "GameLogic/WallSpan.h"
#include "Libraries/WWVegas/WWMath/quat.h"

#include <memory>
#include <stdexcept>

namespace
{
const float kCell = 10.0f; // MAP_XY_FACTOR (ZH: the terrain's heightmap cell, the sample resolution of checkSampleBuildLocation; RotWK RW 0xBD83D8)
const float kMaxWaterLandSampleRatio = 6.0f; // lane BUILD-5: RW 0xDA59A0 (a PORT's samples, either way)
const float kPortWalkStep = 5.0f;            // lane BUILD-5: RW 0xBDAE58 (getPortTransform's walk from the land centre to the water)
const float kPortAngleOffset = 1.5707963705062866f; // lane BUILD-5: RW 0xDA599C (pi / 2, added to the shore direction)
const float kSupplyBuildBorderSearchFactor = 2.0f; // RotWK RW 0xBD889C: closest-supply search radius = 2 * border + build bounding-circle radius

struct PlayableExtent
{
	float minX = 0.0f, minY = 0.0f, maxX = 0.0f, maxY = 0.0f;
};

bool getActivePlayableExtent(const GameLogic &logic, PlayableExtent &extent)
{
	const TerrainLogic *terrain = logic.terrain();
	return terrain && terrain->getExtent(0, extent.maxX, extent.maxY); // active-boundary selector is not yet ported (S-921)
}

bool outsidePlayableMargin(const PlayableExtent &extent, float x, float y, float margin)
{
	if (!(margin > 0.0f))
	{
		return false;
	}
	// RotWK's comiss/ja tests (RW 0x794C08..0x794C58) reject strict outside values; equality at the margin is legal.
	const float minX = SimMath::addf32(extent.minX, margin), minY = SimMath::addf32(extent.minY, margin);
	const float maxX = SimMath::subf32(extent.maxX, margin), maxY = SimMath::subf32(extent.maxY, margin);
	return x < minX || x > maxX || y < minY || y > maxY;
}

// RotWK's checkSampleBuildLocation (0x794A2B -> 0x6EAE05) rejects no cell, bit 18, and a type-4 obstacle whose object is missing or INERT.
// At RW 0x794B14 the template byte +0x113 is tested with mask 2 (KindOf INERT, index 89); HERO is mask 4 (index 90) and does not trigger this branch.
// The layer (review fix, lane PRTRLW): RW 0x794A5C .. 0x794A93 reads the cell on the ground layer (push 1) unless the template is WALL_SEGMENT (+0x11C
// bit 29, KindOf 189), which asks RW 0x680B70; the type-4 obstacle id (RW 0x794AF4 -> 0x5E3CC9) always asks RW 0x680B70. The port answers RW 0x680B70
// with the ground everywhere (S-161), so both lookups are the ground layer; the pathfinder's getLayerAt (wall tops, ramps, bridges) is not retail here.
bool sampleCellRequiresRefusal(GameLogic &logic, AIWorld &ai, float x, float y, float z)
{
	Coord3D sample{ x, y, z };
	Pathfinder &pathfinder = ai.pathfinder();
	const PathfindCell *cell = pathfinder.getCell(LAYER_GROUND, &sample);
	if (!cell || cell->getImpassableToPlayers())
	{
		return true;
	}
	if (cell->getType() != PathfindCell::CELL_OBSTACLE)
	{
		return false;
	}
	const PathfindObjectID obstacleId = cell->getObstacleID();
	Object *obstacle = obstacleId == PATHFIND_INVALID_ID ? nullptr : logic.findObjectByID((ObjectID)obstacleId);
	return !obstacle || !obstacle->getTemplate() || obstacle->isKindOfName("INERT"); // RW template +0x113 & 2, KindOf INERT 89 (HERO is & 4 / 90)
}

bool templateKindOf(GameLogic &logic, const ThingTemplate &tt, const char *name)
{
	const int bit = ObjectTemplateInfoBuilder::kindOfIndex(name);
	if (bit < 0)
	{
		return false;
	}
	return MaskTest(logic.templateInfo(tt.getFinalOverride()).kindOf, (unsigned)bit);
}

// ZH BuildAssistant::iterateFootprint as BuildPlacement samples it: every active shape's footprint (lane PATH-2, S-341) on a regular grid of `res` that always
// includes the centre line (the grid starts at the negative extent); a template without geometry samples its position. `visit` answers false to stop.
template <class F>
void sampleFootprints(std::vector<BuildPlacement::Footprint> &fps, const Coord3D &pos, float angle, float res, F &&visit)
{
	if (fps.empty())
	{
		BuildPlacement::Footprint fp;
		fp.type = 0;
		fp.major = fp.minor = 0.0f;
		fp.x = pos.x;
		fp.y = pos.y;
		fp.z = pos.z;
		fp.angle = angle;
		fps.assign(1, fp);
	}
	for (const BuildPlacement::Footprint &fp : fps)
	{
		const float c = SimMath::cosDet(fp.angle), s = SimMath::sinDet(fp.angle);
		for (float u = SimMath::subf32(0.0f, fp.major);; u = SimMath::addf32(u, res))
		{
			const float uu = u > fp.major ? fp.major : u;
			for (float v = SimMath::subf32(0.0f, fp.minor);; v = SimMath::addf32(v, res))
			{
				const float vv = v > fp.minor ? fp.minor : v;
				if (fp.type == 0 || SimMath::sumSquares2(uu, vv) <= SimMath::mulf32(fp.major, fp.major))
				{
					const float x = SimMath::addf32(fp.x, SimMath::subf32(SimMath::mulf32(uu, c), SimMath::mulf32(vv, s)));
					const float y = SimMath::addf32(fp.y, SimMath::addf32(SimMath::mulf32(uu, s), SimMath::mulf32(vv, c)));
					if (!visit(x, y, fp.z))
					{
						return;
					}
				}
				if (v >= fp.minor)
				{
					break;
				}
			}
			if (u >= fp.major)
			{
				break;
			}
		}
	}
}

template <class F>
void sampleFootprint(GameLogic &logic, const ThingTemplate &build, const Coord3D &pos, float angle, float res, F &&visit)
{
	std::vector<BuildPlacement::Footprint> fps;
	if (!BuildPlacement::footprintsOf(logic, build, pos, angle, fps))
	{
		fps.clear();
	}
	sampleFootprints(fps, pos, angle, res, visit);
}

// the footprints of geometry shapes at a place (footprintsOf's body)
void footprintsOfShapes(const std::vector<PathfindShape> &shapes, const Coord3D &pos, float angle, std::vector<BuildPlacement::Footprint> &out)
{
	out.clear();
	for (const PathfindShape &s : shapes)
	{
		if (!s.active)
		{
			continue;
		}
		Coord3D at = pos;
		PathfindGeometry::applyShapeOffset(s, angle, at);
		BuildPlacement::Footprint f;
		// Footprint numbers its kinds 0 box, 1 sphere, 2 cylinder; PathfindGeometryType is SPHERE 0, CYLINDER 1, BOX 2 (lane PATH-2 fix: the type was copied
		// unconverted, so a box collided as a disc and a sphere as a box)
		f.type = s.type == PATHFIND_GEOMETRY_BOX ? 0 : (s.type == PATHFIND_GEOMETRY_SPHERE ? 1 : 2);
		f.major = s.majorRadius;
		f.minor = s.type == PATHFIND_GEOMETRY_BOX ? s.minorRadius : s.majorRadius;
		f.x = at.x;
		f.y = at.y;
		f.z = at.z;
		f.angle = angle;
		out.push_back(f);
	}
}

// the corners of an oriented rectangle (half sizes a along the angle, b across), counter-clockwise
void corners(const BuildPlacement::Footprint &f, float cx[4], float cy[4])
{
	const float c = SimMath::cosDet(f.angle), s = SimMath::sinDet(f.angle);
	const float ax = SimMath::mulf32(c, f.major), ay = SimMath::mulf32(s, f.major);
	const float bx = SimMath::mulf32(SimMath::subf32(0.0f, s), f.minor), by = SimMath::mulf32(c, f.minor);
	cx[0] = SimMath::addf32(SimMath::addf32(f.x, ax), bx);
	cy[0] = SimMath::addf32(SimMath::addf32(f.y, ay), by);
	cx[1] = SimMath::subf32(SimMath::addf32(f.x, ax), bx);
	cy[1] = SimMath::subf32(SimMath::addf32(f.y, ay), by);
	cx[2] = SimMath::subf32(SimMath::subf32(f.x, ax), bx);
	cy[2] = SimMath::subf32(SimMath::subf32(f.y, ay), by);
	cx[3] = SimMath::addf32(SimMath::subf32(f.x, ax), bx);
	cy[3] = SimMath::addf32(SimMath::subf32(f.y, ay), by);
}

// the projection interval of a footprint's corner set on the axis (ux, uy)
void project(const float cx[4], const float cy[4], float ux, float uy, float &lo, float &hi)
{
	for (int i = 0; i < 4; ++i)
	{
		const float d = SimMath::addf32(SimMath::mulf32(cx[i], ux), SimMath::mulf32(cy[i], uy));
		if (i == 0 || d < lo)
		{
			lo = d;
		}
		if (i == 0 || d > hi)
		{
			hi = d;
		}
	}
}

// the point of the rectangle `r` nearest to (px, py), in the rectangle's own frame: the distance test of a disc against an oriented rectangle
bool discHitsRect(const BuildPlacement::Footprint &rect, float px, float py, float radius)
{
	const float c = SimMath::cosDet(rect.angle), s = SimMath::sinDet(rect.angle);
	const float dx = SimMath::subf32(px, rect.x), dy = SimMath::subf32(py, rect.y);
	const float lx = SimMath::addf32(SimMath::mulf32(dx, c), SimMath::mulf32(dy, s));
	const float ly = SimMath::subf32(SimMath::mulf32(dy, c), SimMath::mulf32(dx, s));
	const float qx = lx > rect.major ? rect.major : (lx < SimMath::subf32(0.0f, rect.major) ? SimMath::subf32(0.0f, rect.major) : lx);
	const float qy = ly > rect.minor ? rect.minor : (ly < SimMath::subf32(0.0f, rect.minor) ? SimMath::subf32(0.0f, rect.minor) : ly);
	const float ex = SimMath::subf32(lx, qx), ey = SimMath::subf32(ly, qy);
	return SimMath::sumSquares2(ex, ey) <= SimMath::mulf32(radius, radius);
}

bool isBox(const BuildPlacement::Footprint &f) { return f.type == 0; }
} // namespace

bool BuildPlacement::footprintsCollide(const Footprint &a, const Footprint &b)
{
	if (isBox(a) && isBox(b))
	{
		// separating axis test on the four edge normals
		float ax[4], ay[4], bx[4], by[4];
		corners(a, ax, ay);
		corners(b, bx, by);
		const float axes[4][2] = { { SimMath::cosDet(a.angle), SimMath::sinDet(a.angle) },
			{ SimMath::subf32(0.0f, SimMath::sinDet(a.angle)), SimMath::cosDet(a.angle) },
			{ SimMath::cosDet(b.angle), SimMath::sinDet(b.angle) },
			{ SimMath::subf32(0.0f, SimMath::sinDet(b.angle)), SimMath::cosDet(b.angle) } };
		for (const auto &axis : axes)
		{
			float alo = 0, ahi = 0, blo = 0, bhi = 0;
			project(ax, ay, axis[0], axis[1], alo, ahi);
			project(bx, by, axis[0], axis[1], blo, bhi);
			if (ahi < blo || bhi < alo)
			{
				return false;
			}
		}
		return true;
	}
	if (!isBox(a) && !isBox(b))
	{
		const float dx = SimMath::subf32(a.x, b.x), dy = SimMath::subf32(a.y, b.y);
		const float r = SimMath::addf32(a.major, b.major);
		return SimMath::sumSquares2(dx, dy) <= SimMath::mulf32(r, r);
	}
	if (isBox(a))
	{
		return discHitsRect(a, b.x, b.y, b.major);
	}
	return discHitsRect(b, a.x, a.y, a.major);
}

bool BuildPlacement::footprintsOf(GameLogic &logic, const ThingTemplate &tt, const Coord3D &pos, float angle, std::vector<Footprint> &out)
{
	out.clear();
	AIWorld *ai = logic.aiWorld();
	if (!ai)
	{
		return false;
	}
	const ObjectMovementInfo &info = ai->movementInfo(*tt.getFinalOverride());
	if (!info.hasGeometry)
	{
		return false;
	}
	std::vector<PathfindShape> shapes;
	for (size_t i = 0; i < info.geometry.shapeCount(); ++i)
	{
		shapes.push_back(info.geometry.shape(i));
	}
	footprintsOfShapes(shapes, pos, angle, out);
	return true;
}

bool BuildPlacement::footprintsOf(GameLogic &logic, const Object &obj, std::vector<Footprint> &out)
{
	out.clear();
	AIWorld *ai = logic.aiWorld();
	if (!ai)
	{
		return false;
	}
	const ObjectMovementInfo &info = ai->movementInfo(*static_cast<const ThingTemplate *>(obj.getTemplate())->getFinalOverride());
	if (!info.hasGeometry)
	{
		return false;
	}
	const PathfindGeometry &g = obj.ownPathfindGeometry(info.geometry); // lane MODCOMPAT-MOD1: the object's own GeometryInfo
	std::vector<PathfindShape> shapes;
	for (size_t i = 0; i < g.shapeCount(); ++i)
	{
		shapes.push_back(g.shape(i));
	}
	footprintsOfShapes(shapes, *obj.getPosition(), obj.getOrientation(), out);
	return true;
}

namespace
{
// lane BUILDCLEAR-1: the KindOf bits RW 0x796D37 and its filters test (names from RW 0xDA0E68; the indices are asserted by test_buildclear1.cpp)
int clearKindBit(const char *name)
{
	const int bit = ObjectTemplateInfoBuilder::kindOfIndex(name);
	if (bit < 0)
	{
		throw std::logic_error(std::string("BuildPlacement: the KindOf registry has no ") + name);
	}
	return bit;
}

struct ClearKinds
{
	int immobile = clearKindBit("IMMOBILE");          // 2: template + 0x108 & 4
	int structure = clearKindBit("STRUCTURE");        // 7: + 0x108 & 0x80
	int aircraft = clearKindBit("AIRCRAFT");          // 12: + 0x109 & 0x10
	int dozer = clearKindBit("DOZER");                // 14: + 0x109 & 0x40
	int inert = clearKindBit("INERT");                // 89: + 0x113 & 2
	int baseFoundation = clearKindBit("BASE_FOUNDATION"); // 104: + 0x115 & 1
	int baseSite = clearKindBit("BASE_SITE");         // 120: + 0x117 & 1
	int blockingGate = clearKindBit("BLOCKING_GATE"); // 137: + 0x119 & 2
	int taint = clearKindBit("TAINT");                // 152: + 0x11B & 1
	int wallHub = clearKindBit("WALL_HUB");           // 156: + 0x11B & 0x10
	int wallSegment = clearKindBit("WALL_SEGMENT");   // 189: + 0x11F & 0x20
};
const ClearKinds &clearKinds()
{
	static const ClearKinds k;
	return k;
}

const unsigned kStatusAttached = 98; // RW 0x796F0F: Object::testStatus(0x62), TheObjectStatusNames[98] = ATTACHED

bool templateHas(GameLogic &logic, const ThingTemplate &tt, int bit)
{
	return MaskTest(logic.templateInfo(tt.getFinalOverride()).kindOf, (unsigned)bit);
}

bool objectHas(const Object &o, int bit)
{
	return o.isKindOf((unsigned)bit);
}

// ThingTemplate RW 0x73CD88 (BFME2 decomp ThingTemplate::rva0033B3D7): the first behavior module data whose vslot 5 answers true. In RotWK only
// CastleBehaviorModuleData (vtable RW 0xC30FB0, ctor RW 0x79C543) overrides it.
const CastleBehaviorModuleData *castleDataOf(const ThingTemplate &tt)
{
	for (const ThingTemplate::Nugget &n : tt.getFinalOverride()->behaviorModules().nuggets())
	{
		if (const CastleBehaviorModuleData *c = dynamic_cast<const CastleBehaviorModuleData *>(n.data.get()))
		{
			return c;
		}
	}
	return nullptr;
}

// Object RW 0x68BDA5 with the CastleBehavior name key (RW 0x797AC1, string RW 0xC0BE9C)
const CastleBehavior *castleBehaviorOf(const Object &obj)
{
	for (const std::unique_ptr<BehaviorModule> &m : obj.modules())
	{
		if (const CastleBehavior *c = dynamic_cast<const CastleBehavior *>(m.get()))
		{
			return c;
		}
	}
	return nullptr;
}

const std::vector<ObjectGeometry::Shape> &templateShapes(const ThingTemplate &tt)
{
	return ObjectGeometry::cachedShapesOf(*tt.getFinalOverride()); // template + 0xA0
}

// RW 0x450429(type, small, height, major, minor): one shape at no offset
std::vector<ObjectGeometry::Shape> oneShape(int type, float height, float major, float minor)
{
	ObjectGeometry::Shape s;
	s.type = type;
	s.height = height;
	s.majorRadius = major;
	s.minorRadius = minor;
	return { s };
}

// RW 0x66137C: the x87 squared 2D distance of the object from `p`: (dy * dy) + (dx * dx), each difference and product at 24 bits (left in ST0)
double distSq2DX87(const Object &obj, const Coord3D &p)
{
	const double dx = NumericState::pc24SubW(obj.getPosition()->x, p.x);
	const double dy = NumericState::pc24SubW(obj.getPosition()->y, p.y);
	return NumericState::pc24AddW(NumericState::pc24MulW(dy, dy), NumericState::pc24MulW(dx, dx));
}

// RW 0x661C09 (vtable RW 0xC307B8, ctor RW 0x793A2A; BFME2 decomp Rva00391667): the castle build's range filter. An object with a CastleBehavior passes when
// (its MaxCastleRadius (RW 0x797ED7: module data + 0x2C) + m_range)^2 > its squared 2D distance (x87; the distance is stored as a float, the square stays in ST0);
// any other object passes when its geometry touches a CYLINDER (height 20 RW 0xBDBC6C, both radii m_range) at the site, angle 0 (RW 0xAD2CE0)
class CastleRangeFilter : public PartitionFilter
{
public:
	CastleRangeFilter(const Coord3D &pos, float range)
		: m_pos(pos)
		, m_range(range)
	{
	}
	bool allow(Object &obj) override
	{
		if (const CastleBehavior *castle = castleBehaviorOf(obj))
		{
			const float dist = NumericState::fstpDword(distSq2DX87(obj, m_pos));
			const double r = NumericState::pc24AddW(castle->data()->m_maxCastleRadius, m_range);
			return NumericState::pc24MulW(r, r) > (double)dist; // RW 0x661C52: fcompi, jbe refuses
		}
		const std::vector<ObjectGeometry::Shape> cyl = oneShape(ObjectGeometry::SHAPE_CYLINDER, 20.0f, m_range, m_range);
		return NotifyCrushModules::geometriesOverlap(obj.geometryShapes(), *obj.getPosition(), obj.getOrientation(), cyl, m_pos, 0.0f);
	}

private:
	Coord3D m_pos;
	float m_range;
};

// RW 0x405ED1: the per-axis slab test of a segment (origin, origin + dir) against a 2D box (lo.x, lo.y, hi.x, hi.y). Each axis on its own: |d| below 1e-4
// (RW 0xBD19E0 / 0xBD19E4) needs the origin inside the slab; else t = (lo - o) * (1 / d) and (hi - o) * (1 / d) (SSE) must give min <= 1 and max >= 0
bool segmentTouchesBox2D(const float box[4], float ox, float oy, float dx, float dy)
{
	const float o[2] = { ox, oy }, d[2] = { dx, dy };
	for (int axis = 0; axis < 2; ++axis)
	{
		const float lo = box[axis], hi = box[axis + 2];
		if (9.999999747378752e-05f > d[axis] && d[axis] > -9.999999747378752e-05f)
		{
			if (lo > o[axis] || o[axis] > hi)
			{
				return false;
			}
			continue;
		}
		const float inv = SimMath::divf32(1.0f, d[axis]);
		const float t1 = SimMath::mulf32(SimMath::subf32(lo, o[axis]), inv), t2 = SimMath::mulf32(SimMath::subf32(hi, o[axis]), inv);
		const float tMin = t2 > t1 ? t1 : t2, tMax = t1 > t2 ? t1 : t2;
		if (tMin > 1.0f || 0.0f > tMax)
		{
			return false;
		}
	}
	return true;
}

// RW 0x661A02 (vtable RW 0xC307AC, ctor RW 0x7939DF; BFME2 decomp Rva0039161C): the geometry filter of a non-castle build (`m_castles` is retail's + 0x1D,
// cleared for a WALL_SEGMENT build at RW 0x796E0E):
//   1. an object with castle data (RW 0x73CD88) passes, when m_castles, if a CYLINDER (height 20, both radii its castle data's MaxCastleRadius) at its position and
//      orientation touches the build's geometry at the site (RW 0xAD2CE0);
//   2. else the build's geometry touching the object's must equal m_desired (1);
//   3. failing that a BLOCKING_GATE passes when its template's circle (GeometryInfo + 0x10)^2 * 0.66 (RW 0xC0F1C4, x87) >= the squared 2D distance (RW 0x66137C);
//   4. failing that a STRUCTURE passes when one of its exits' natural rally points (ExitInterface slot 9, offset; the update exit, vslot 0x50) makes a segment
//      from its position that the 2D box of the build's geometry at the site (RW 0xAD1D60) meets (RW 0x405ED1). Every module is asked (no early stop).
class BuildGeometryFilter : public PartitionFilter
{
public:
	BuildGeometryFilter(GameLogic &logic, const Coord3D &pos, const std::vector<ObjectGeometry::Shape> &shapes, float angle, bool castles)
		: m_logic(logic)
		, m_pos(pos)
		, m_shapes(shapes)
		, m_angle(angle)
		, m_castles(castles)
	{
	}
	bool allow(Object &obj) override
	{
		const ThingTemplate *tt = static_cast<const ThingTemplate *>(obj.getTemplate());
		if (tt && m_castles)
		{
			if (const CastleBehaviorModuleData *data = castleDataOf(*tt))
			{
				const std::vector<ObjectGeometry::Shape> cyl = oneShape(ObjectGeometry::SHAPE_CYLINDER, 20.0f, data->m_maxCastleRadius, data->m_maxCastleRadius);
				if (NotifyCrushModules::geometriesOverlap(m_shapes, m_pos, m_angle, cyl, *obj.getPosition(), obj.getOrientation()))
				{
					return true;
				}
			}
		}
		if (NotifyCrushModules::geometriesOverlap(m_shapes, m_pos, m_angle, obj.geometryShapes(), *obj.getPosition(), obj.getOrientation()) == m_desired)
		{
			return true;
		}
		const ClearKinds &k = clearKinds();
		if (objectHas(obj, k.blockingGate) && tt)
		{
			const float r = ConstructionSite::geometryBounds(templateShapes(*tt)).circle;
			const double dist = distSq2DX87(obj, m_pos);
			if (!(NumericState::pc24MulW(NumericState::pc24MulW(r, r), 0.6600000262260437f) < dist)) // RW 0x661B11: jb skips
			{
				return true;
			}
		}
		if (!objectHas(obj, k.structure))
		{
			return false;
		}
		bool result = false;
		for (const std::unique_ptr<BehaviorModule> &m : obj.modules())
		{
			ExitInterface *exit = m->getExitInterface();
			if (!exit)
			{
				continue; // S-301: the contain's exit interface (vslot 8 -> 0x74) is not ported (Object RW 0x68BB37 likewise)
			}
			Coord3D pt{ 0.0f, 0.0f, 0.0f };
			if (!exit->getNaturalRallyPoint(&pt, true))
			{
				continue;
			}
			PathfindGeometry g;
			ObjectGeometry::fillPathfindGeometry(m_shapes, g);
			float box[4];
			g.boundingBox2D(m_pos, m_angle, box);
			const Coord3D &at = *obj.getPosition();
			if (segmentTouchesBox2D(box, at.x, at.y, SimMath::subf32(pt.x, at.x), SimMath::subf32(pt.y, at.y)))
			{
				result = true;
			}
		}
		return result;
	}

private:
	GameLogic &m_logic;
	Coord3D m_pos;
	const std::vector<ObjectGeometry::Shape> &m_shapes;
	float m_angle;
	bool m_desired = true;
	bool m_castles;
};

// RW 0x661027 (vtable RW 0xC1D63C; BFME2 decomp Rva00261353Filter): the object's shroud status for the player (Object RW 0x68D8F7) is CLEAR (1). Without a
// ShroudManager every object is clear (as RW 0x68D8F7 answers for an object without a record).
bool clearForPlayer(GameLogic &logic, Object &obj, int player)
{
	ShroudManager *shroud = logic.shroud();
	return !shroud || shroud->getObjectStatus(obj, player) == OBJECTSHROUD_CLEAR;
}

// RW 0x796E52: FOGGED (3) or SHROUDED (4) for the player
bool foggedForPlayer(GameLogic &logic, Object &obj, int player)
{
	ShroudManager *shroud = logic.shroud();
	if (!shroud)
	{
		return false;
	}
	const ObjectShroudStatus s = shroud->getObjectStatus(obj, player);
	return s == OBJECTSHROUD_FOGGED || s == OBJECTSHROUD_SHROUDED;
}

// RW 0x4054F5: x87 (z * z + y * y) + x * x, CRT sqrt, fst dword
float lengthX87(float x, float y, float z)
{
	const double sum = NumericState::pc24AddW(NumericState::pc24AddW(NumericState::pc24MulW(z, z), NumericState::pc24MulW(y, y)), NumericState::pc24MulW(x, x));
	return NumericState::fstpDword(NumericState::sqrtPC24(sum));
}

// RW 0x797040 .. 0x797160: the first behavior module data (template + 0x2E4, in order) that answers vslot 25 (RW 0x64: in RotWK only
// QueueProductionExitUpdateModuleData, vtable RW 0xC08068, + 0x14 = NaturalRallyPoint) or vslot 29 (RW 0x74: HordeGarrisonContainModuleData and every module
// data built on its ctor RW 0x87D4CF: HordeGarrisonContain / TunnelContain (RW 0xC5CBE8), SlaughterHordeContain (RW 0xC5F570), CitadelSlaughterHordeContain
// (RW 0xC5F5F0), ProductionQueueHordeContain (RW 0xC5F988); + 0xC8 = ExitOffset). The exit point moves 20 (RW 0xBDBC6C) along its own direction
// ((x * x + y * y) + z * z, SSE; Inv_Sqrt RW 0x441C56 with 24-bit products, skipped for a zero vector); the door offset is taken as it is.
Coord3D exitOrDoorPoint(const ThingTemplate &build)
{
	Coord3D pt{ 0.0f, 0.0f, 0.0f };
	for (const ThingTemplate::Nugget &n : build.getFinalOverride()->behaviorModules().nuggets())
	{
		if (const QueueProductionExitUpdateModuleData *exit = dynamic_cast<const QueueProductionExitUpdateModuleData *>(n.data.get()))
		{
			pt = exit->m_naturalRallyPoint;
			float nx = pt.x, ny = pt.y, nz = pt.z;
			const float lenSq = SimMath::addf32(SimMath::addf32(SimMath::mulf32(pt.x, pt.x), SimMath::mulf32(pt.y, pt.y)), SimMath::mulf32(pt.z, pt.z));
			if (lenSq != 0.0f)
			{
				const float inv = BFME2_Inverse_Sqrt(lenSq);
				nx = BFME2_Mul24(pt.x, inv);
				ny = BFME2_Mul24(pt.y, inv);
				nz = BFME2_Mul24(inv, pt.z);
			}
			pt.x = SimMath::addf32(SimMath::mulf32(nx, 20.0f), pt.x);
			pt.y = SimMath::addf32(SimMath::mulf32(ny, 20.0f), pt.y);
			pt.z = SimMath::addf32(SimMath::mulf32(nz, 20.0f), pt.z);
			return pt;
		}
		if (const HordeGarrisonContainBehaviorData *door = dynamic_cast<const HordeGarrisonContainBehaviorData *>(n.data.get()))
		{
			return door->horde.m_exitOffset;
		}
	}
	return pt;
}

// the unplaced dozer structures of the player's list (RW Player + 0x774 via RW 0x796631; BFME2 + 0x754): DozerAIUpdate::createPhantomStructure adds the
// phantom's id to the dozer's controlling player's list (RW 0x88D443 -> RW 0x6AEEED), its removal (RW 0x88D0B6) and its placement (RW 0x88D147) take it out
// (RW 0x6ADA08). INFERENCE (S-301): the port keeps no such list; it is derived from the dozers whose unplaced structure is set, in object order (the
// collision test that reads the list has no order dependence).
std::vector<Object *> pendingStructures(GameLogic &logic, const Player *player)
{
	std::vector<Object *> out;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		const DozerAIUpdate *dozer = o->isDestroyed() ? nullptr : dynamic_cast<const DozerAIUpdate *>(o->getAIUpdateInterface());
		if (!dozer || dozer->unplacedStructure() == INVALID_ID || o->getControllingPlayer() != player)
		{
			continue;
		}
		if (Object *phantom = logic.findObjectByID(dozer->unplacedStructure()))
		{
			out.push_back(phantom);
		}
	}
	return out;
}
} // namespace

bool BuildPlacement::isLocationClearOfObjects(GameLogic &logic, const Coord3D &pos, const ThingTemplate &build, float angle, Object *builder, unsigned options, bool ignoreFogged)
{
	// RotWK RW 0x796D37 (tier A = BFME2 0x794879, decomp BuildAssistant.cpp:2634), lane BUILDCLEAR-1. Both editions.
	const bool b20 = (options & LLF_NO_ENEMY_OBJECT_OVERLAP) != 0, b40 = (options & LLF_NO_EXIT_TEST) != 0, b100 = (options & LLF_CASTLE_MEMBERS) != 0;
	const bool b200 = (options & LLF_ENEMY_STRUCTURES_ONLY) != 0, b400 = (options & LLF_PENDING_STRUCTURES) != 0;
	if (!logic.settings().maxCastleRadiusLoaded)
	{
		// BFME1 data (RotWK's rules on BFME1 data, S-5017) has no GameData MaxCastleRadius row; RotWK's GlobalData + 0x1220 default was not read
		logic.noteStop("[S-301] building placement: GameData MaxCastleRadius was not loaded (RW GlobalData + 0x1220's default not read): the clear test's range adds 0");
	}
	const ClearKinds &k = clearKinds();
	const std::vector<ObjectGeometry::Shape> &buildShapes = templateShapes(build);
	const ConstructionSite::GeometryBounds bounds = ConstructionSite::geometryBounds(buildShapes);
	const float gdCastleRadius = logic.settings().maxCastleRadius; // GlobalData + 0x1220
	float range = 0.0f;
	std::unique_ptr<PartitionFilter> filter;
	if (const CastleBehaviorModuleData *data = castleDataOf(build))
	{
		range = SimMath::mulf32(gdCastleRadius, 2.0f); // RW 0x796DC3: mulss RW 0xBD889C
		filter = std::make_unique<CastleRangeFilter>(pos, data->m_maxCastleRadius);
	}
	else
	{
		range = SimMath::addf32(gdCastleRadius, bounds.sphere); // RW 0x796DF3: template + 0xB4 (GeometryInfo + 0x14)
		filter = std::make_unique<BuildGeometryFilter>(logic, pos, buildShapes, angle, !templateHas(logic, build, k.wallSegment));
	}
	const PartitionHits hits = logic.partition().iterateObjectsInRange(pos, range, FROM_BOUNDINGSPHERE_3D, { filter.get() }, ITER_FASTEST);
	const int builderPlayer = builder && builder->getControllingPlayer() ? builder->getControllingPlayer()->getPlayerIndex() : -1;
	for (const PartitionHit &h : hits)
	{
		Object *them = h.object;
		if (builder && ignoreFogged)
		{
			if (builderPlayer < 0)
			{
				logic.reportError("BuildPlacement: the builder of a fog-ignoring placement test has no controlling player (retail reads its + 0x54) [S-301]");
				return false;
			}
			if (foggedForPlayer(logic, *them, builderPlayer))
			{
				continue; // RW 0x796E57 / 0x796E60
			}
		}
		if (ConstructionSite::isRemovableForConstruction(logic, them))
		{
			continue; // RW 0x796E70: RW 0x7940A6
		}
		if (objectHas(*them, k.aircraft) || objectHas(*them, k.inert))
		{
			continue; // RW 0x796E80 / 0x796E8D
		}
		if (b100 && builder && objectHas(*builder, k.baseSite))
		{
			// RW 0x796EB4: the member's CastleMemberBehavior (RW 0x797BEF) + 0x18 names the builder (Object + 0x74)
			const CastleMemberBehavior *member = dynamic_cast<const CastleMemberBehavior *>(them->findModule("CastleMemberBehavior"));
			if (member && member->castleId() == builder->getID())
			{
				continue;
			}
			if (them == builder || objectHas(*them, k.dozer))
			{
				continue; // RW 0x796ED0 / 0x796EDC
			}
		}
		if (!templateHas(logic, build, k.baseFoundation) && templateHas(logic, build, k.wallSegment) && objectHas(*them, k.wallHub) && them == builder)
		{
			continue; // RW 0x796EE9 .. 0x796F0A: a wall segment from its own hub
		}
		if (them->testStatus(kStatusAttached) || objectHas(*them, k.taint))
		{
			continue; // RW 0x796F11 / 0x796F1A
		}
		if (objectHas(*them, k.immobile))
		{
			if (b20 && builder && builder->getRelationship(*them) != ENEMIES)
			{
				continue; // RW 0x796F3B: RW 0x68D7AB
			}
			return false;
		}
		if (builder && builder->getRelationship(*them) == ENEMIES && !b200)
		{
			return false; // RW 0x796F55
		}
		if (b200 && builder && builder->getRelationship(*them) == ENEMIES && objectHas(*them, k.structure))
		{
			return false; // RW 0x796F76 .. 0x796F89
		}
	}
	if (b400 && builder)
	{
		// RW 0x796F9E .. 0x797017: the builder's player's list; an object whose own geometry touches the build's at the site refuses (RW 0xAD2CE0)
		if (Player *owner = builder->getControllingPlayer())
		{
			for (Object *obj : pendingStructures(logic, owner))
			{
				if (NotifyCrushModules::geometriesOverlap(buildShapes, pos, angle, obj->geometryShapes(), *obj->getPosition(), obj->getOrientation()))
				{
					return false;
				}
			}
		}
	}
	if (b20 || b40 || b100)
	{
		return true; // RW 0x79701C .. 0x797034
	}
	const Coord3D pt = exitOrDoorPoint(build);
	if (lengthX87(pt.x, pt.y, pt.z) == 0.0f)
	{
		return true; // RW 0x797174 .. 0x797183
	}
	// RW 0x797189 .. 0x797242: Matrix3D(true).Rotate_Z(angle) on the point, MSVCR71 cos / sin (RW 0xA3CF84 / 0xA3CF90, stored as floats), SSE with the matrix's
	// zero terms; x and y move to the site, z stays the offset's
	const float c = NumericState::fstpDword(SimMath::cosd(angle)), s = NumericState::fstpDword(SimMath::sind(angle));
	const float cz = SimMath::mulf32(c, 0.0f), sz = SimMath::mulf32(s, 0.0f), pz0 = SimMath::mulf32(pt.z, 0.0f);
	Coord3D out;
	out.x = SimMath::addf32(SimMath::addf32(SimMath::addf32(SimMath::mulf32(SimMath::subf32(cz, s), pt.y), SimMath::mulf32(SimMath::addf32(c, sz), pt.x)), pz0), pos.x);
	out.y = SimMath::addf32(SimMath::addf32(SimMath::addf32(SimMath::mulf32(SimMath::subf32(c, sz), pt.y), SimMath::mulf32(SimMath::addf32(s, cz), pt.x)), pz0), pos.y);
	out.z = SimMath::addf32(SimMath::addf32(SimMath::mulf32(SimMath::subf32(cz, sz), pt.y), SimMath::mulf32(SimMath::addf32(cz, sz), pt.x)), pt.z);
	if (builder && objectHas(*builder, k.dozer))
	{
		// RW 0x797255 .. 0x79726B: TheAI's pathfinder QuickDoesPathExist(builder, the site, the point, 0) (lane QPATH-1)
		AIWorld *ai = logic.aiWorld();
		AIUpdateInterface *bai = builder->getAIUpdateInterface();
		if (!ai || !bai)
		{
			logic.reportError("BuildPlacement: a DOZER builder's exit path test needs the AIWorld and the builder's AI [S-301]");
			return false;
		}
		if (!ai->pathfinder().quickDoesPathExist(bai->adapter(), &pos, &out, nullptr))
		{
			return false;
		}
	}
	// RW 0x79727B .. 0x797345: the strip from the site to the point: a BOX (RW 0x450429: type 2, not small, height 1, major len * 0.5, minor 0.5) at the
	// midpoint (z the site's), turned by atan2(dx / len, dy / len) (RW 0x441BF4 with x then y, as retail passes them)
	const float dx = SimMath::subf32(out.x, pos.x), dy = SimMath::subf32(out.y, pos.y);
	const float len = SimMath::length2d(dx, dy); // RW 0x4032DD
	const std::vector<ObjectGeometry::Shape> strip = oneShape(ObjectGeometry::SHAPE_BOX, 1.0f, SimMath::mulf32(len, 0.5f), 0.5f);
	const float inv = SimMath::divf32(1.0f, len);
	const float stripAngle = NumericState::fstpDword(SimMath::atan2d(SimMath::mulf32(inv, dx), SimMath::mulf32(inv, dy)));
	const Coord3D centre{ SimMath::mulf32(SimMath::addf32(out.x, pos.x), 0.5f), SimMath::mulf32(SimMath::addf32(out.y, pos.y), 0.5f), pos.z };
	if (builderPlayer < 0)
	{
		logic.reportError("BuildPlacement: the exit strip test needs the builder's player (retail reads builder->getControllingPlayer() + 0x54) [S-301]");
		return false;
	}
	// RW 0x79734F .. 0x7973F7: ThePartitionManager within the build's circle (template + 0xB0) * 2 from the site (FROM_CENTER_3D, unsorted), KindOf STRUCTURE
	// (RW 0x444D39 / 0x445139) and CLEAR for the builder's player (RW 0xC1D63C); a hit whose geometry touches the strip refuses
	PartitionFilterFn structures([&k](Object &o) { return objectHas(o, k.structure); });
	PartitionFilterFn visible([&logic, builderPlayer](Object &o) { return clearForPlayer(logic, o, builderPlayer); });
	const float stripRange = SimMath::mulf32(bounds.circle, 2.0f);
	for (const PartitionHit &h : logic.partition().iterateObjectsInRange(pos, stripRange, FROM_CENTER_3D, { &structures, &visible }, ITER_FASTEST))
	{
		Object *them = h.object;
		if (NotifyCrushModules::geometriesOverlap(them->geometryShapes(), *them->getPosition(), them->getOrientation(), strip, centre, stripAngle))
		{
			return false;
		}
	}
	return true;
}

LegalBuildCode BuildPlacement::isLocationLegalToBuild(GameLogic &logic, const Coord3D &pos, const ThingTemplate &build, float angle, unsigned options, Object *builder, Player *player)
{
	(void)player;
	AIWorld *ai = logic.aiWorld();
	const PathfindTerrain *terrain = ai ? ai->pathfinder().terrainView() : nullptr;
	// ZH: "You just can't never build off the map, regardless of options"
	if (terrain)
	{
		float loX, loY, hiX, hiY;
		terrain->getMaximumPathfindExtent(loX, loY, hiX, hiY);
		if (pos.x < loX || pos.x > hiX || pos.y < loY || pos.y > hiY)
		{
			return LBC_RESTRICTED_TERRAIN;
		}
	}
	// RW 0x796861 .. 0x796942 (lane BUILDCLEAR-1, DECOMPINV-2 item 2): bit 0x04 asks isLocationClearOfObjects (vslot 0x48 in RotWK, 0x44 in BFME2) with 0x04 and the
	// caller's 0x40 / 0x100 / 0x200 / 0x400, bit 0x20 a second time with 0x20 and the caller's 0x40 / 0x200; both pass the caller's bit 0x80 as the fog flag
	const bool ignoreFogged = (options & LLF_IGNORE_FOGGED) != 0;
	if (options & LLF_NO_OBJECT_OVERLAP)
	{
		const unsigned clear = LLF_NO_OBJECT_OVERLAP | (options & (LLF_NO_EXIT_TEST | LLF_CASTLE_MEMBERS | LLF_ENEMY_STRUCTURES_ONLY | LLF_PENDING_STRUCTURES));
		if (!isLocationClearOfObjects(logic, pos, build, angle, builder, clear, ignoreFogged))
		{
			return LBC_OBJECTS_IN_THE_WAY;
		}
	}
	if (options & LLF_NO_ENEMY_OBJECT_OVERLAP)
	{
		const unsigned clear = LLF_NO_ENEMY_OBJECT_OVERLAP | (options & (LLF_NO_EXIT_TEST | LLF_ENEMY_STRUCTURES_ONLY));
		if (!isLocationClearOfObjects(logic, pos, build, angle, builder, clear, ignoreFogged))
		{
			return LBC_OBJECTS_IN_THE_WAY;
		}
	}
	if (templateKindOf(logic, build, "CANNOT_BUILD_NEAR_SUPPLIES"))
	{
		if (!logic.settings().buildRulesLoaded)
		{
			logic.reportError("BuildPlacement: GameData's SupplyBuildBorder was not loaded for a CANNOT_BUILD_NEAR_SUPPLIES template");
			return LBC_RESTRICTED_TERRAIN;
		}
		const float border = logic.settings().supplyBuildBorder;
		if (border > 0.0f)
		{
			if (!ai)
			{
				logic.reportError("BuildPlacement: CANNOT_BUILD_NEAR_SUPPLIES requires AIWorld geometry [S-301]");
				return LBC_RESTRICTED_TERRAIN;
			}
			const ObjectMovementInfo &buildInfo = ai->movementInfo(*build.getFinalOverride());
			const float searchRadius = SimMath::addf32(SimMath::mulf32(border, kSupplyBuildBorderSearchFactor), buildInfo.geometry.boundingCircleRadius());
			PartitionFilterFn supplySource([](Object &obj) { return obj.isKindOfName("SUPPLY_SOURCE"); });
			Object *source = logic.partition().getClosestObject(pos, searchRadius, FROM_CENTER_3D, { &supplySource });
			if (source)
			{
				std::vector<Footprint> mine, supply;
				if (footprintsOf(logic, build, pos, angle, mine) && footprintsOf(logic, *source, supply))
				{
					// RotWK ObjectGeometry::expandGeometryBy (RW 0xAD2CA0) adds the raw border to each active radius with x87 PC24 rounding.
					for (Footprint &shape : supply)
					{
						shape.major = SimMath::pc24Add(shape.major, border);
						shape.minor = SimMath::pc24Add(shape.minor, border);
					}
					for (const Footprint &a : mine)
					{
						for (const Footprint &b : supply)
						{
							if (footprintsCollide(a, b))
							{
								// RW's overlap helper returns raw 3; use the port's named semantic code (6), not that incompatible integer.
								return LBC_TOO_CLOSE_TO_SUPPLIES;
							}
						}
					}
				}
			}
		}
	}
	// RW 0x796A3C ..: bit 0x02 with a DOZER builder on a cell CLEAR for its player (TheShroudManager; no shroud: clear) asks for a path
	const int playerIndex = builder && builder->getControllingPlayer() ? builder->getControllingPlayer()->getPlayerIndex() : -1;
	ShroudManager *shroud = logic.shroud();
	if ((options & LLF_CLEAR_PATH) && builder && builder->isKindOfName("DOZER") && (!shroud || shroud->getStatusAt(playerIndex, pos.x, pos.y) == CELLSHROUD_CLEAR))
	{
		// RotWK gates the clear-path query on a DOZER builder (template byte +0x109 bit 0x40, RW 0x796A46..0x796A98; ZH's gate is a non-IMMOBILE builder, BuildAssistant.cpp:997): a
		// wall hub or a plot skips the test (lane BUILD-2); a builder without an AI cannot reach it
		if (!builder->getAIUpdateInterface())
		{
			return LBC_NO_CLEAR_PATH;
		}
		// the path query itself is not ported (stop S-301)
	}
	if (options & LLF_TERRAIN_RESTRICTIONS)
	{
		if (!terrain)
		{
			logic.reportError("BuildPlacement: TERRAIN_RESTRICTIONS asked without a pathfinder terrain (the cell classification lives there) [S-301]");
			return LBC_RESTRICTED_TERRAIN;
		}
		if (!logic.settings().buildRulesLoaded)
		{
			logic.reportError("BuildPlacement: GameData's build rules (including MinDistFromEdgeOfMapForBuild) were not loaded");
			return LBC_RESTRICTED_TERRAIN;
		}
		const float edgeMargin = logic.settings().minDistFromEdgeOfMapForBuild;
		PlayableExtent playableExtent;
		if (edgeMargin > 0.0f && !getActivePlayableExtent(logic, playableExtent))
		{
			logic.reportError("BuildPlacement: MinDistFromEdgeOfMapForBuild needs playable boundary 0 [S-921]");
			return LBC_RESTRICTED_TERRAIN;
		}
		// lane BUILD-5 r2: a PORT (KindOf 188, template + 0x11F & 0x10) samples water too: RotWK's checkSampleBuildLocation (RW 0x794A2B, BFME2 decomp
		// BuildAssistant.cpp:1843, tier A) counts a K188 build's samples as water (TerrainLogic::isUnderwater) or land, and only land samples enter the height
		// range; isLocationLegalToBuild (RW 0x796C0B .. 0x796C60, BFME2 decomp BuildAssistant.cpp:2101) then wants both kinds after the full resolution pass and
		// refuses a ratio max / min above 6 (RW 0xDA59A0). A non-port's underwater sample stays restricted (the port's cell classification by the pathfinder's
		// cell types is S-301's inference: cliffs restrict, water restricts unless PORT)
		const bool port = templateKindOf(logic, build, "PORT");
		const float step[2] = { SimMath::mulf32(3.0f, kCell), kCell };
		float lo = 0.0f, hi = 0.0f; // the height range spans both passes (RW: hiZ / loZ are set once; only the water / land counts restart)
		bool first = true;
		for (int pass = 0; pass < 2; ++pass)
		{
			float water = 0.0f, land = 0.0f;
			bool restricted = false;
			sampleFootprint(logic, build, pos, angle, step[pass], [&](float x, float y, float sampleZ) {
				if (sampleCellRequiresRefusal(logic, *ai, x, y, sampleZ))
				{
					restricted = true;
					return false;
				}
				float waterZ = 0.0f, terrainZ = 0.0f;
				if (terrain->isCliffCell(x, y))
				{
					restricted = true;
					return false;
				}
				if (terrain->isUnderwater(x, y, &waterZ, &terrainZ))
				{
					if (!port)
					{
					restricted = true;
						return false;
					}
					water = SimMath::addf32(water, 1.0f);
					if (outsidePlayableMargin(playableExtent, x, y, edgeMargin))
					{
						restricted = true;
						return false;
					}
					return true;
				}
				land = SimMath::addf32(land, 1.0f);
				const float z = terrain->getGroundHeight(x, y);
				if (first || z < lo)
				{
					lo = z;
				}
				if (first || z > hi)
				{
					hi = z;
				}
				first = false;
				if (outsidePlayableMargin(playableExtent, x, y, edgeMargin))
				{
					restricted = true;
					return false;
				}
				return true;
			});
			if (restricted)
			{
				return LBC_RESTRICTED_TERRAIN;
			}
			if (SimMath::subf32(hi, lo) > logic.settings().allowedHeightVariationForBuilding)
			{
				return LBC_NOT_FLAT_ENOUGH;
			}
			if (pass == 1 && port)
			{
				if (land == 0.0f || water == 0.0f)
				{
					return LBC_RESTRICTED_TERRAIN;
				}
				const float big = water > land ? water : land, small = land < water ? land : water;
				if (SimMath::divf32(big, small) > kMaxWaterLandSampleRatio)
				{
					return LBC_RESTRICTED_TERRAIN;
				}
			}
		}
	}
	// RW 0x796C66 .. 0x796D24 (lane BUILDCLEAR-1; BFME2 decomp BuildAssistant.cpp:2137): a WALL_HUB build whose template has the module data answering vslot 6
	// (ThingTemplate RW 0x73CDD8; in RotWK only WallHubBehaviorModuleData, vtable RW 0xC567C0) is legal within its MaxBuildoutDistance (+ 0x30) of one of the
	// builder's player's COMMANDCENTERs (RW 0x6B00F3, object order: Coord3D::length RW 0x403111 kept in ST0, `jae`), else code 9. Retail reads the builder's
	// player without a null test.
	if (templateKindOf(logic, build, "WALL_HUB"))
	{
		const WallHubBehaviorModuleData *hub = nullptr;
		for (const ThingTemplate::Nugget &n : build.getFinalOverride()->behaviorModules().nuggets())
		{
			if ((hub = dynamic_cast<const WallHubBehaviorModuleData *>(n.data.get())) != nullptr)
			{
				break;
			}
		}
		if (hub)
		{
			if (!builder)
			{
				logic.reportError("BuildPlacement: a WALL_HUB build's buildout test needs a builder (retail reads its controlling player) [S-301]");
				return LBC_BEYOND_BUILDOUT;
			}
			if (const Player *owner = builder->getControllingPlayer())
			{
				for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
				{
					if (o->getControllingPlayer() != owner || o->isDestroyed() || !o->isKindOfName("COMMANDCENTER"))
					{
						continue;
					}
					const Coord3D *c = o->getPosition();
					const double length = SimMath::sqrtPC24((double)SimMath::sumSquares3(SimMath::subf32(pos.x, c->x), SimMath::subf32(pos.y, c->y), SimMath::subf32(pos.z, c->z)));
					if ((double)hub->m_maxBuildoutDistance >= length)
					{
						return LBC_OK;
					}
				}
			}
			return LBC_BEYOND_BUILDOUT;
		}
	}
	return LBC_OK;
}

bool BuildPlacement::getPortTransform(GameLogic &logic, const Coord3D &pos, const ThingTemplate &build, float angle, float &outAngle, Coord3D &outPos)
{
	// lane BUILD-5: RotWK BuildAssistant::getPortTransform (RW 0x794C63, tier A with BFME2 0x792802; the decomp's recovered, not yet byte-matching attempt
	// BuildAssistantGetPortTransform.cpp, matched since; was reverse/attempts/0x00392802.cpp): the footprint sampled at MAP_XY_FACTOR (RW 0x794CFB -> RW 0x793A6E with checkSampleBuildLocation RW 0x794A2B); no answer
	// when a sample is restricted or either kind is missing; else the land samples' centre walks towards the water samples' centre in steps of 5 (RW 0xBDAE58)
	// until it is under water, steps back 10 (the direction normalized, times RW 0xBD83D8) and takes the ground height there; the angle is atan2 of that
	// direction (RW 0x441BD2) plus pi / 2 (RW 0xDA599C)
	PortSnapView view;
	return portSnapView(logic, build, view) && getPortTransform(view, pos, angle, outAngle, outPos);
}

bool BuildPlacement::portSnapView(GameLogic &logic, const ThingTemplate &build, PortSnapView &out)
{
	out = PortSnapView();
	AIWorld *ai = logic.aiWorld();
	out.terrain = ai ? ai->pathfinder().terrainView() : nullptr;
	if (!out.terrain)
	{
		return false;
	}
	out.buildRulesLoaded = logic.settings().buildRulesLoaded;
	out.minDistFromEdgeOfMapForBuild = logic.settings().minDistFromEdgeOfMapForBuild;
	PlayableExtent extent;
	if (out.minDistFromEdgeOfMapForBuild > 0.0f && getActivePlayableExtent(logic, extent))
	{
		out.hasPlayableExtent = true;
		out.playableMaxX = extent.maxX;
		out.playableMaxY = extent.maxY;
	}
	const ObjectMovementInfo &info = ai->movementInfo(*build.getFinalOverride());
	out.hasGeometry = info.hasGeometry;
	for (size_t i = 0; info.hasGeometry && i < info.geometry.shapeCount(); ++i)
	{
		out.shapes.push_back(info.geometry.shape(i));
	}
	return true;
}

bool BuildPlacement::getPortTransform(const PortSnapView &view, const Coord3D &pos, float angle, float &outAngle, Coord3D &outPos)
{
	const PathfindTerrain *terrain = view.terrain;
	if (!terrain || !view.buildRulesLoaded || (view.minDistFromEdgeOfMapForBuild > 0.0f && !view.hasPlayableExtent))
	{
		return false;
	}
	PlayableExtent playableExtent;
	playableExtent.maxX = view.playableMaxX;
	playableExtent.maxY = view.playableMaxY;
	std::vector<Footprint> fps;
	if (view.hasGeometry)
	{
		footprintsOfShapes(view.shapes, pos, angle, fps);
	}
	float water = 0.0f, land = 0.0f;
	float wx = 0.0f, wy = 0.0f, wz = 0.0f, lx = 0.0f, ly = 0.0f, lz = 0.0f;
	bool restricted = false;
	sampleFootprints(fps, pos, angle, kCell, [&](float x, float y, float) {
		if (outsidePlayableMargin(playableExtent, x, y, view.minDistFromEdgeOfMapForBuild))
		{
			restricted = true;
			return false;
		}
		if (terrain->isCliffCell(x, y))
		{
			restricted = true; // INFERENCE (S-301): the pathfinder's cell types stand for retail's GetCellType classification
		}
		float waterZ = 0.0f, terrainZ = 0.0f;
		const float z = terrain->getGroundHeight(x, y);
		if (terrain->isUnderwater(x, y, &waterZ, &terrainZ))
		{
			water = SimMath::addf32(water, 1.0f);
			wx = SimMath::addf32(wx, x);
			wy = SimMath::addf32(wy, y);
			wz = SimMath::addf32(wz, z);
		}
		else
		{
			land = SimMath::addf32(land, 1.0f);
			lx = SimMath::addf32(lx, x);
			ly = SimMath::addf32(ly, y);
			lz = SimMath::addf32(lz, z);
		}
		return true;
	});
	if (restricted || !(land > 0.0f) || !(water > 0.0f))
	{
		return false;
	}
	const float invL = SimMath::divf32(1.0f, land), invW = SimMath::divf32(1.0f, water);
	lx = SimMath::mulf32(lx, invL);
	ly = SimMath::mulf32(ly, invL);
	lz = SimMath::mulf32(lz, invL);
	wx = SimMath::mulf32(wx, invW);
	wy = SimMath::mulf32(wy, invW);
	wz = SimMath::mulf32(wz, invW);
	float dx = SimMath::subf32(wx, lx), dy = SimMath::subf32(wy, ly), dz = SimMath::subf32(wz, lz);
	const float dist = (float)SimMath::length3d(dx, dy, dz);
	auto normalize = [&](float scale) {
		const float len = (float)SimMath::length3d(dx, dy, dz);
		if (len != 0.0f)
		{
			const float inv = SimMath::divf32(1.0f, len);
			dx = SimMath::mulf32(dx, inv);
			dy = SimMath::mulf32(dy, inv);
			dz = SimMath::mulf32(dz, inv);
		}
		dx = SimMath::mulf32(dx, scale);
		dy = SimMath::mulf32(dy, scale);
		dz = SimMath::mulf32(dz, scale);
	};
	normalize(kPortWalkStep);
	outPos.x = lx;
	outPos.y = ly;
	outPos.z = lz;
	for (float t = 0.0f; t < dist; t = SimMath::addf32(t, kPortWalkStep))
	{
		float waterZ = 0.0f, terrainZ = 0.0f;
		if (terrain->isUnderwater(outPos.x, outPos.y, &waterZ, &terrainZ))
		{
			normalize(kCell);
			outPos.x = SimMath::subf32(outPos.x, dx);
			outPos.y = SimMath::subf32(outPos.y, dy);
			outPos.z = terrain->getGroundHeight(outPos.x, outPos.y);
			break;
		}
		outPos.x = SimMath::addf32(outPos.x, dx);
		outPos.y = SimMath::addf32(outPos.y, dy);
		outPos.z = SimMath::addf32(outPos.z, dz);
	}
	outAngle = SimMath::addf32((float)SimMath::atan2d(dy, dx), kPortAngleOffset);
	return true;
}

const char *BuildPlacement::codeName(LegalBuildCode code)
{
	switch (code)
	{
		case LBC_OK: return "OK";
		case LBC_RESTRICTED_TERRAIN: return "RESTRICTED_TERRAIN";
		case LBC_NOT_FLAT_ENOUGH: return "NOT_FLAT_ENOUGH";
		case LBC_OBJECTS_IN_THE_WAY: return "OBJECTS_IN_THE_WAY";
		case LBC_NO_CLEAR_PATH: return "NO_CLEAR_PATH";
		case LBC_SHROUD: return "SHROUD";
		case LBC_TOO_CLOSE_TO_SUPPLIES: return "TOO_CLOSE_TO_SUPPLIES";
		case LBC_BEYOND_BUILDOUT: return "BEYOND_BUILDOUT";
	}
	return "?";
}

std::vector<std::string> BuildPlacement::stopLines()
{
	return {
		"[S-301] building placement: isLocationLegalToBuild follows RotWK RW 0x796810 (BFME2 0x794352 decomp; options and codes in RotWK / BFME2 numbering, lane BUILDCLEAR-1) and "
		"isLocationClearOfObjects is RW 0x796D37 (tier A: the castle range filter RW 0x661C09, the geometry filter RW 0x661A02 with BLOCKING_GATE and structure exit "
		"lines, the shroud / removable / AIRCRAFT / INERT / castle member / wall hub / ATTACHED / TAINT skips, the IMMOBILE and enemy rules, the pending dozer structures, "
		"the exit / door strip and the dozer's quick path); cliff/water/height checks, positive MinDistFromEdgeOfMapForBuild (boundary 0; equality allowed), the "
		"disassembled missing-cell / bit-18 / type-4 INERT rule, CANNOT_BUILD_NEAR_SUPPLIES and the WALL_HUB buildout code 9 are ported. Still not ported: active-boundary "
		"script selection (S-921), the WALL_SEGMENT / obstacle sample layer and getLayerForDestination (RW 0x680B70, taken as the ground: S-161), remaining cell-type "
		"cases, the builder's clear-path query (isPathAvailable / isQuickPathAvailable), the contain's exit interface in the structure exit lines (vslot 8 -> 0x74), "
		"and INFERENCE: the player's pending-structure list (Player + 0x774) is derived from its dozers' unplaced structures",
	};
}
