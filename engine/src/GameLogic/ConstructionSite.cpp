// OpenBFME. GPL-3.0.
// See GameLogic/ConstructionSite.h for the sources of every rule.

#include "GameLogic/ConstructionSite.h"

#include "Common/NumericState.h"
#include "Common/Player.h"
#include "Common/RandomValue.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/AI/AIMove.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIPathfindHost.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/NotifyCrushModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/SimMath.h"

#include <stdexcept>

namespace
{
// the source names the retail random draws carry (RW 0xC30858 / 0xC56E80; only the debug log reads them)
const char kBuildAssistantFile[] = "BuildAssistant.cpp";
const char kFoundationFile[] = "FoundationAIUpdate.cpp";

int kindBit(const char *name)
{
	const int bit = ObjectTemplateInfoBuilder::kindOfIndex(name);
	if (bit < 0)
	{
		throw std::logic_error(std::string("ConstructionSite: the KindOf registry has no ") + name);
	}
	return bit;
}

struct Kinds
{
	int selectable = kindBit("SELECTABLE"), immobile = kindBit("IMMOBILE"), shrubbery = kindBit("SHRUBBERY"), dozer = kindBit("DOZER"), noCollide = kindBit("NO_COLLIDE");
	int clearedByBuild = kindBit("CLEARED_BY_BUILD"), alwaysSelectable = kindBit("ALWAYS_SELECTABLE"), walkOnTopOfWall = kindBit("WALK_ON_TOP_OF_WALL");
	int inert = kindBit("INERT"), baseFoundation = kindBit("BASE_FOUNDATION"), horde = kindBit("HORDE"), baseSite = kindBit("BASE_SITE"), moveOnly = kindBit("MOVE_ONLY");
	int doNotClassify = kindBit("DO_NOT_CLASSIFY"), taint = kindBit("TAINT");
};
const Kinds &kinds()
{
	static const Kinds k;
	return k;
}

bool templateIs(GameLogic &logic, const ThingTemplate &tt, int bit)
{
	return MaskTest(logic.templateInfo(tt.getFinalOverride()).kindOf, (unsigned)bit);
}

bool objectIs(const Object &o, int bit)
{
	return o.isKindOf((unsigned)bit);
}

// a GeometryInfo: its shapes and the bounds RW 0xAD2860 computes for them
struct Geometry
{
	std::vector<ObjectGeometry::Shape> shapes;
	float circle = 0.01f; ///< + 0x10
	float sphere = 0.0f;  ///< + 0x14
	float majorExtent = 0.0f, minorExtent = 0.0f; ///< + 0x24 / + 0x28
};

PathfindShape pathfindShape(const ObjectGeometry::Shape &g)
{
	PathfindShape ps;
	ps.type = g.type == ObjectGeometry::SHAPE_BOX ? PATHFIND_GEOMETRY_BOX : (g.type == ObjectGeometry::SHAPE_CYLINDER ? PATHFIND_GEOMETRY_CYLINDER : PATHFIND_GEOMETRY_SPHERE);
	ps.height = g.height;
	ps.majorRadius = g.majorRadius;
	ps.minorRadius = g.minorRadius;
	ps.offsetX = g.offsetX;
	ps.offsetY = g.offsetY;
	ps.offsetZ = g.offsetZ;
	ps.active = g.active;
	return ps;
}

Geometry geometryOf(std::vector<ObjectGeometry::Shape> shapes)
{
	Geometry g;
	PathfindGeometry pg;
	for (const ObjectGeometry::Shape &s : shapes)
	{
		pg.shapes.push_back(pathfindShape(s));
	}
	if (!shapes.empty())
	{
		g.circle = pg.boundingCircleRadius();
		g.sphere = pg.boundingSphereRadius();
	}
	// RW 0xAD2040 / 0xAD2957 / 0xAD2974: + 0x24 / + 0x28 are the half sizes of the box around the active shapes (from 0, each shape's offset +- its extent: a box's
	// major radius in x and minor in y, a sphere's or cylinder's major radius in both)
	float minX = 0.0f, maxX = 0.0f, minY = 0.0f, maxY = 0.0f;
	for (const ObjectGeometry::Shape &s : shapes)
	{
		if (!s.active)
		{
			continue;
		}
		const float rx = s.majorRadius, ry = s.type == ObjectGeometry::SHAPE_BOX ? s.minorRadius : s.majorRadius;
		const float lx = SimMath::subf32(s.offsetX, rx), hx = SimMath::addf32(s.offsetX, rx), ly = SimMath::subf32(s.offsetY, ry), hy = SimMath::addf32(s.offsetY, ry);
		minX = lx < minX ? lx : minX;
		maxX = hx > maxX ? hx : maxX;
		minY = ly < minY ? ly : minY;
		maxY = hy > maxY ? hy : maxY;
	}
	const float nx = SimMath::subf32(0.0f, minX), ny = SimMath::subf32(0.0f, minY);
	g.majorExtent = nx > maxX ? nx : maxX;
	g.minorExtent = ny > maxY ? ny : maxY;
	g.shapes = std::move(shapes);
	return g;
}

// RW 0x450429(BOX, not small, height, major, minor): one shape at no offset
Geometry boxGeometry(float height, float major, float minor)
{
	ObjectGeometry::Shape s;
	s.type = ObjectGeometry::SHAPE_BOX;
	s.height = height;
	s.majorRadius = major;
	s.minorRadius = minor;
	return geometryOf({ s });
}

Geometry templateGeometry(const ThingTemplate &tt)
{
	return geometryOf(ObjectGeometry::shapesOf(*tt.getFinalOverride()));
}

// RW 0x693A1A(0) -> 0x68C866 -> HordeContainInterface vslot 0x1C8 (RW 0x86D830): see the header
bool hordeMemberPathHasWaypoint(const Object &obj)
{
	const Object *horde = objectIs(obj, kinds().horde) ? &obj : nullptr;
	if (!horde && obj.getContainedBy() && objectIs(*obj.getContainedBy(), kinds().horde))
	{
		horde = obj.getContainedBy();
	}
	ContainModuleInterface *contain = horde ? horde->getContain() : nullptr;
	const ContainModuleInterface::ContainedItemsList *items = contain && contain->getHordeContainInterface() ? contain->getContainedItemsList() : nullptr;
	if (!items)
	{
		return false;
	}
	for (const Object *member : *items)
	{
		const AIUpdateInterface *ai = member ? member->getAIUpdateInterface() : nullptr;
		const Path *path = ai ? ai->mover().path() : nullptr;
		// RW 0x765522: a path (+ 4 head) with a current node (+ 0x10) and a node whose waypoint id (+ 0x20) is set
		if (!path || !path->getFirstNode() || !path->currentNode())
		{
			continue;
		}
		for (const PathNode *n = path->getFirstNode(); n; n = n->getNextOptimized())
		{
			if (n->getWaypointID() != PathNode::NO_WAYPOINT)
			{
				return true;
			}
		}
	}
	return false;
}

// RW 0x4054F5: x87 (z * z + y * y) + x * x, CRT sqrt, fst dword
float lengthX87(float x, float y, float z)
{
	const double sum = NumericState::pc24AddW(NumericState::pc24AddW(NumericState::pc24MulW(z, z), NumericState::pc24MulW(y, y)), NumericState::pc24MulW(x, x));
	return NumericState::fstpDword(NumericState::sqrtPC24(sum));
}

// RW 0x7940E8 (vtable RW 0xC307C8): answers no; destroys the path of a unit whose line to its path's end passes within `radius` of the site
class PathCutFilter : public PartitionFilter
{
public:
	PathCutFilter(const Coord3D &site, float radius)
		: m_site(site)
		, m_radius(radius)
	{
	}
	int cut = 0;
	bool allow(Object &o) override
	{
		const Kinds &k = kinds();
		if (objectIs(o, k.inert) || objectIs(o, k.moveOnly) || objectIs(o, k.baseFoundation) || objectIs(o, k.walkOnTopOfWall) || objectIs(o, k.immobile) ||
			objectIs(o, k.shrubbery) || objectIs(o, k.clearedByBuild) || objectIs(o, k.alwaysSelectable))
		{
			return false; // RW 0x7940F6 .. 0x794148
		}
		if (o.isEffectivelyDead())
		{
			return false; // RW 0x79414E
		}
		AIUpdateInterface *ai = o.getAIUpdateInterface();
		Path *path = ai ? ai->mover().path() : nullptr;
		if (!path || !path->getLastNode())
		{
			return false; // RW 0x79415B .. 0x794170: AI + 0x260, its path + 0x140
		}
		if (hordeMemberPathHasWaypoint(o))
		{
			return false; // RW 0x794176 .. 0x79418D
		}
		// RW 0x794193 ..: start = the unit, end = the path's node at Path + 8 (its last), rel = site - start, dir = end - start (SSE)
		const Coord3D start = *o.getPosition();
		const Coord3D end = *path->getLastNode()->getPosition();
		const Coord3D rel = { SimMath::subf32(m_site.x, start.x), SimMath::subf32(m_site.y, start.y), SimMath::subf32(m_site.z, start.z) };
		Coord3D dir = { SimMath::subf32(end.x, start.x), SimMath::subf32(end.y, start.y), SimMath::subf32(end.z, start.z) };
		const float length = lengthX87(dir.x, dir.y, dir.z); // RW 0x794230: fstp dword
		// RW 0x405553: 1.0f / the length (divss), each component times it (no zero test)
		const float inv = SimMath::divf32(1.0f, lengthX87(dir.x, dir.y, dir.z));
		dir.x = SimMath::mulf32(dir.x, inv);
		dir.y = SimMath::mulf32(dir.y, inv);
		dir.z = SimMath::mulf32(dir.z, inv);
		// RW 0x4037F5: x87 (z * z' + y * y') + x * x', kept in the register for the 0 test, stored (fst dword) for the length test
		const double dotW = NumericState::pc24AddW(NumericState::pc24AddW(NumericState::pc24MulW(rel.z, dir.z), NumericState::pc24MulW(rel.y, dir.y)), NumericState::pc24MulW(rel.x, dir.x));
		const float proj = NumericState::fstpDword(dotW);
		Coord3D closest;
		if (0.0 > dotW)
		{
			closest = start; // RW 0x794256
		}
		else if (proj > length)
		{
			closest = end; // RW 0x794278 (comiss)
		}
		else
		{
			closest.x = SimMath::addf32(SimMath::mulf32(dir.x, proj), start.x);
			closest.y = SimMath::addf32(SimMath::mulf32(dir.y, proj), start.y);
			closest.z = SimMath::addf32(SimMath::mulf32(dir.z, proj), start.z);
		}
		const float cx = SimMath::subf32(closest.x, m_site.x), cy = SimMath::subf32(closest.y, m_site.y), cz = SimMath::subf32(closest.z, m_site.z);
		// RW 0x405520: x87 (z * z + y * y) + x * x against radius * radius (x87), `ja` skips
		const double lenSq = NumericState::pc24AddW(NumericState::pc24AddW(NumericState::pc24MulW(cz, cz), NumericState::pc24MulW(cy, cy)), NumericState::pc24MulW(cx, cx));
		if (lenSq > NumericState::pc24MulW(m_radius, m_radius))
		{
			return false;
		}
		ai->mover().destroyPath(); // RW 0x794303: RW 0x66276B
		++cut;
		return false;
	}

private:
	Coord3D m_site;
	float m_radius;
};
} // namespace

bool ConstructionSite::GeometryFilter::allow(Object &obj)
{
	// RW 0x66139C: (RW 0xAD2CE0(this geometry, position, angle, the object's geometry, its position, its orientation) == the flag)
	return NotifyCrushModules::geometriesOverlap(m_shapes, m_pos, m_angle, objectShapes(obj), *obj.getPosition(), obj.getOrientation()) == m_flag;
}

std::vector<ObjectGeometry::Shape> ConstructionSite::objectShapes(const Object &obj)
{
	std::vector<ObjectGeometry::Shape> shapes = ObjectGeometry::shapesOf(*static_cast<const ThingTemplate *>(obj.getTemplate())->getFinalOverride());
	const std::vector<std::uint8_t> &active = obj.geometryActive();
	if (active.size() == shapes.size())
	{
		for (size_t i = 0; i < shapes.size(); ++i)
		{
			shapes[i].active = active[i] != 0;
		}
	}
	return shapes;
}

bool ConstructionSite::isRemovableForConstruction(GameLogic &logic, const Object *obj)
{
	(void)logic;
	// RW 0x7940A6
	if (!obj)
	{
		return false;
	}
	const Kinds &k = kinds();
	if (objectIs(*obj, k.inert) || objectIs(*obj, k.taint))
	{
		return false; // RW 0x7940B5 / 0x7940BE (+ 0x113 bit 1, + 0x11B bit 0)
	}
	if (objectIs(*obj, k.shrubbery) || objectIs(*obj, k.clearedByBuild))
	{
		return true; // RW 0x7940C7 / 0x7940D4
	}
	return obj->isEffectivelyDead(); // RW 0x7940DD
}

bool ConstructionSite::isRemovableForFoundation(GameLogic &logic, const Object *obj)
{
	(void)logic;
	// RW 0x8582A5: as RW 0x7940A6 without the TAINT test
	if (!obj)
	{
		return false;
	}
	const Kinds &k = kinds();
	if (objectIs(*obj, k.inert))
	{
		return false;
	}
	if (objectIs(*obj, k.shrubbery) || objectIs(*obj, k.clearedByBuild))
	{
		return true;
	}
	return obj->isEffectivelyDead();
}

namespace
{
// RW 0x796576 / 0x858C41 (identical but for the removable test)
int clearSite(GameLogic &logic, const ThingTemplate &what, const Coord3D &pos, float angle, bool foundation)
{
	const Geometry g = templateGeometry(what);
	ConstructionSite::GeometryFilter overlap(pos, g.shapes, angle, true); // RW 0x7965A0: (pos, template + 0xA0, angle, 1)
	const float radius = SimMath::mulf32(g.sphere, 1.1f);               // RW 0x7965A5: template + 0xB4 * RW 0xBE4398 (SSE)
	std::vector<Object *> doomed;
	for (const PartitionHit &h : logic.partition().iterateObjectsInRange(pos, radius, FROM_BOUNDINGSPHERE_3D, { &overlap }, ITER_FASTEST))
	{
		Object *o = h.object;
		const bool removable = foundation ? ConstructionSite::isRemovableForFoundation(logic, o) : ConstructionSite::isRemovableForConstruction(logic, o);
		if (removable && !objectIs(*o, kinds().alwaysSelectable))
		{
			doomed.push_back(o);
		}
	}
	int n = 0;
	for (Object *o : doomed)
	{
		if (!o->isDestroyed())
		{
			logic.destroyObject(o); // RW 0x796602: RW 0x62BBAB (TheGameLogic->destroyObject marks it; the list it walks is the query's copy)
			++n;
		}
	}
	return n;
}
} // namespace

int ConstructionSite::clearRemovableForConstruction(GameLogic &logic, const ThingTemplate &what, const Coord3D &pos, float angle)
{
	return clearSite(logic, what, pos, angle, false);
}

int ConstructionSite::clearSiteForFoundation(GameLogic &logic, const ThingTemplate &what, const Coord3D &pos, float angle)
{
	return clearSite(logic, what, pos, angle, true);
}

bool ConstructionSite::moveObjectsForConstruction(GameLogic &logic, const ThingTemplate &what, const Coord3D &pos, float angle, Player &builderPlayer)
{
	// RW 0x797465
	const Kinds &k = kinds();
	const Geometry tg = templateGeometry(what);
	// RW 0x7974AB: BOX(height 10 (RW 0xBD83D8), major = minor = the template's bounding circle); RW 0x7974BB: exactly one BOX shape -> a copy of the template's
	Geometry gi = boxGeometry(10.0f, tg.circle, tg.circle);
	if (tg.shapes.size() == 1 && tg.shapes[0].type == ObjectGeometry::SHAPE_BOX)
	{
		gi = tg;
	}
	const float radius = SimMath::mulf32(gi.circle, 1.4f); // RW 0x7974D0: gi + 0x10 * RW 0xBDBC98
	PathCutFilter pathCut(pos, radius);
	(void)logic.partition().iterateAllObjects({ &pathCut }); // RW 0x79750C: the result list is discarded
	ConstructionSite::GeometryFilter overlap(pos, gi.shapes, angle, true);
	PartitionFilterFn alive([](Object &o) { return !o.isEffectivelyDead(); }); // RW 0xC10E20
	PartitionFilterFn hasAI([](Object &o) { return o.getAIUpdateInterface() != nullptr; }); // RW 0xC2E65C
	PartitionFilterFn kindMask([&k](Object &o) { // RW 0x445139: SELECTABLE, none of INERT / MOVE_ONLY / BASE_FOUNDATION / WALK_ON_TOP_OF_WALL
		return objectIs(o, k.selectable) && !objectIs(o, k.inert) && !objectIs(o, k.moveOnly) && !objectIs(o, k.baseFoundation) && !objectIs(o, k.walkOnTopOfWall);
	});
	const float range = SimMath::mulf32(gi.sphere, 1.1f); // RW 0x7975AE: gi + 0x14 * RW 0xBE4398
	bool anyUnmovables = false;
	for (const PartitionHit &h : logic.partition().iterateObjectsInRange(pos, range, FROM_BOUNDINGSPHERE_3D, { &alive, &hasAI, &kindMask, &overlap }, ITER_FASTEST))
	{
		Object *o = h.object;
		if (objectIs(*o, k.immobile) || objectIs(*o, k.alwaysSelectable) || isRemovableForConstruction(logic, o))
		{
			continue; // RW 0x7975F4 .. 0x79761C
		}
		if (builderPlayer.getRelationship(o->getTeam()) == ENEMIES)
		{
			anyUnmovables = true; // RW 0x79762B: neither ALLIES nor NEUTRAL
			continue;
		}
		if (hordeMemberPathHasWaypoint(*o))
		{
			continue; // RW 0x79763E .. 0x797655
		}
		AIUpdateInterface *ai = o->getAIUpdateInterface();
		if (!ai)
		{
			anyUnmovables = true; // RW 0x797666
			continue;
		}
		// RW 0x79766C ..: the radius draw (0.5 .. 1.5, x87 fmul radius, fstp dword), then the angle draw (-pi .. pi)
		const float varied = NumericState::fstpDword(NumericState::pc24MulW(logic.random().getValueReal(0.5f, 1.5f, kBuildAssistantFile, 0xE41), radius));
		const float dir = logic.random().getValueReal(-3.14159274f, 3.14159274f, kBuildAssistantFile, 0xE44);
		const float s = SimMath::sinf32(dir), c = SimMath::cosf32(dir); // RW 0x7976C3 / 0x7976D1: MSVCR71 sin / cos, fstp dword
		Coord3D goal;
		goal.x = SimMath::addf32(pos.x, SimMath::subf32(SimMath::mulf32(c, varied), SimMath::mulf32(s, 0.0f)));
		goal.y = SimMath::addf32(pos.y, SimMath::addf32(SimMath::mulf32(s, varied), SimMath::mulf32(c, 0.0f)));
		goal.z = pos.z;
		if (!ai->isMoving())
		{
			ai->aiMoveToPositionEvenIfSleeping(goal, CMD_FROM_AI); // RW 0x797744: RW 0x79664F(goal, 2)
		}
	}
	return !anyUnmovables;
}

void ConstructionSite::prepareSite(GameLogic &logic, const Object &builder, const ThingTemplate &what, const Coord3D &pos, float angle, Player &owner)
{
	// RW 0x7977C9 .. 0x79780C: builder + 0x109 bit 6 (DOZER), template + 0x10B bit 6 (NO_COLLIDE), template + 0x11A bit 5 (DO_NOT_CLASSIFY)
	const Kinds &k = kinds();
	if (objectIs(builder, k.dozer) || templateIs(logic, what, k.noCollide) || templateIs(logic, what, k.doNotClassify))
	{
		return;
	}
	clearRemovableForConstruction(logic, what, pos, angle);
	moveObjectsForConstruction(logic, what, pos, angle, owner);
}

bool ConstructionSite::moveObjectsForFoundation(GameLogic &logic, const Object &foundation, const ThingTemplate &what, const Coord3D &pos, float angle, Player &owner)
{
	// RW 0x858CFC
	const Kinds &k = kinds();
	const Geometry tg = templateGeometry(what);
	// RW 0x858D0F .. 0x858D56: BOX(height 50 (RW 0xBD88C4), template + 0xC4 * 1.5f, template + 0xC8 * 1.5f (SSE, RW 0xBDE8C8))
	const Geometry gi = boxGeometry(50.0f, SimMath::mulf32(tg.majorExtent, 1.5f), SimMath::mulf32(tg.minorExtent, 1.5f));
	const float radius = SimMath::mulf32(gi.circle, 1.5f); // RW 0x858D5E
	ConstructionSite::GeometryFilter overlap(pos, gi.shapes, angle, true);
	const float range = SimMath::mulf32(gi.sphere, 1.5f); // RW 0x858D97
	bool anyUnmovables = false;
	const bool wallTemplate = templateIs(logic, what, k.walkOnTopOfWall);
	for (const PartitionHit &h : logic.partition().iterateObjectsInRange(pos, range, FROM_BOUNDINGSPHERE_3D, { &overlap }, ITER_FASTEST))
	{
		Object *o = h.object;
		if (o->getContainedBy())
		{
			o = o->getContainedBy(); // RW 0x858DE7: + 0x27C
		}
		if (objectIs(*o, k.inert) || objectIs(*o, k.baseFoundation) || objectIs(*o, k.walkOnTopOfWall) || objectIs(*o, k.moveOnly) || objectIs(*o, k.alwaysSelectable) ||
			isRemovableForFoundation(logic, o))
		{
			continue; // RW 0x858DFA .. 0x858E49
		}
		AIUpdateInterface *ai = o->getAIUpdateInterface();
		if (owner.getRelationship(o->getTeam()) == ENEMIES || !ai)
		{
			anyUnmovables = true; // RW 0x859192
			continue;
		}
		const float varied = NumericState::fstpDword(NumericState::pc24MulW(logic.random().getValueReal(0.5f, 1.5f, kFoundationFile, 0x2AB), radius));
		Coord3D dest1, dest2;
		if (!wallTemplate)
		{
			// RW 0x858EBA ..: v = (varied, 0) turned by the first draw; dest2 turns v again by the second (SSE)
			const float a1 = logic.random().getValueReal(-3.14159274f, 3.14159274f, kFoundationFile, 0x2AF);
			const float s1 = SimMath::sinf32(a1), c1 = SimMath::cosf32(a1);
			const float vx = SimMath::subf32(SimMath::mulf32(c1, varied), SimMath::mulf32(s1, 0.0f));
			const float vy = SimMath::addf32(SimMath::mulf32(s1, varied), SimMath::mulf32(c1, 0.0f));
			dest1 = { SimMath::addf32(pos.x, vx), SimMath::addf32(pos.y, vy), pos.z };
			const float a2 = logic.random().getValueReal(-3.14159274f, 3.14159274f, kFoundationFile, 0x2B7);
			const float s2 = SimMath::sinf32(a2), c2 = SimMath::cosf32(a2);
			const float wx = SimMath::subf32(SimMath::mulf32(c2, vx), SimMath::mulf32(s2, vy));
			const float wy = SimMath::addf32(SimMath::mulf32(vx, s2), SimMath::mulf32(c2, vy));
			dest2 = { SimMath::addf32(pos.x, wx), SimMath::addf32(pos.y, wy), pos.z };
		}
		else
		{
			Coord3D d;
			if (objectIs(foundation, k.baseSite))
			{
				// RW 0x859004: (the foundation - the site) * 0.5f (SSE)
				d.x = SimMath::mulf32(SimMath::subf32(foundation.getPosition()->x, pos.x), 0.5f);
				d.y = SimMath::mulf32(SimMath::subf32(foundation.getPosition()->y, pos.y), 0.5f);
				d.z = SimMath::mulf32(SimMath::subf32(foundation.getPosition()->z, pos.z), 0.5f);
			}
			else
			{
				// RW 0x85904B: the hit's x axis (RW 0x70BA38: transform column 0, normalised by RW 0x405553) * (varied + radius)
				const float a = o->getOrientation();
				float ux = SimMath::cosDet(a), uy = SimMath::sinDet(a), uz = 0.0f;
				const float inv = SimMath::divf32(1.0f, lengthX87(ux, uy, uz));
				ux = SimMath::mulf32(ux, inv);
				uy = SimMath::mulf32(uy, inv);
				uz = SimMath::mulf32(uz, inv);
				const float scale = SimMath::addf32(varied, radius);
				d = { SimMath::mulf32(scale, ux), SimMath::mulf32(scale, uy), SimMath::mulf32(scale, uz) };
			}
			dest1 = { SimMath::addf32(pos.x, d.x), SimMath::addf32(pos.y, d.y), SimMath::addf32(pos.z, d.z) };
			dest2 = { SimMath::subf32(pos.x, d.x), SimMath::subf32(pos.y, d.y), SimMath::subf32(pos.z, d.z) };
		}
		// RW 0x8590CF ..: squared 2D distances of the hit to both (SSE, dy^2 + dx^2); the nearer one: dest1 when dest2 is farther
		const float ax = SimMath::subf32(o->getPosition()->x, dest1.x), ay = SimMath::subf32(o->getPosition()->y, dest1.y);
		const float bx = SimMath::subf32(o->getPosition()->x, dest2.x), by = SimMath::subf32(o->getPosition()->y, dest2.y);
		const float d1 = SimMath::addf32(SimMath::mulf32(ay, ay), SimMath::mulf32(ax, ax));
		const float d2 = SimMath::addf32(SimMath::mulf32(by, by), SimMath::mulf32(bx, bx));
		const Coord3D goal = d2 > d1 ? dest1 : dest2;
		if (!ai->isMoving())
		{
			ai->aiMoveToPositionEvenIfSleeping(goal, CMD_FROM_AI); // RW 0x859143
		}
	}
	return !anyUnmovables;
}
