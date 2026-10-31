// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// PathNode and Path: the result of a search, its optimisation by line of sight and the follower queries the
// locomotor uses. Port of ZH AIPathfind.cpp:119-760 (PathNode, Path, optimize) as changed by BFME / RotWK; the
// follower queries (computePointAhead and the others behind LocomotorPath) are the RotWK ones, see Path section below.

#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/SimMath.h"

#include <cmath>

// ---------------------------------------------------------------------------------------------------------
// PathNode
// ---------------------------------------------------------------------------------------------------------
PathNode::PathNode()
	: m_nextOpti(nullptr), m_next(nullptr), m_prev(nullptr), m_layer(LAYER_INVALID), m_canOptimize(false), m_waypointID(NO_WAYPOINT), m_nextOptiDist2D(0.0f)
{
	m_nextOptiDirNorm2D.x = 0.0f;
	m_nextOptiDirNorm2D.y = 0.0f;
}

void PathNode::setNextOptimized(PathNode *node)
{
	m_nextOpti = node;
	if (node)
	{
		m_nextOptiDirNorm2D.x = SimMath::subf32(node->getPosition()->x, getPosition()->x);
		m_nextOptiDirNorm2D.y = SimMath::subf32(node->getPosition()->y, getPosition()->y);
		m_nextOptiDist2D = SimMath::length2d(m_nextOptiDirNorm2D.x, m_nextOptiDirNorm2D.y);
		if (m_nextOptiDist2D == 0.0f)
		{
			m_nextOptiDist2D = 0.01f; // a zero length segment is adjusted
		}
		m_nextOptiDirNorm2D.x = SimMath::divf32(m_nextOptiDirNorm2D.x, m_nextOptiDist2D);
		m_nextOptiDirNorm2D.y = SimMath::divf32(m_nextOptiDirNorm2D.y, m_nextOptiDist2D);
	}
	else
	{
		m_nextOptiDist2D = 0.0f;
	}
}

PathNode *PathNode::prependToList(PathNode *list)
{
	m_next = list;
	if (list)
	{
		list->m_prev = this;
	}
	m_prev = nullptr;
	return this;
}

PathNode *PathNode::appendToList(PathNode *list)
{
	if (list == nullptr)
	{
		m_next = nullptr;
		m_prev = nullptr;
		return this;
	}
	PathNode *tail;
	for (tail = list; tail->m_next; tail = tail->m_next)
	{
	}
	tail->m_next = this;
	m_prev = tail;
	m_next = nullptr;
	return list;
}

void PathNode::append(PathNode *newNode)
{
	newNode->m_next = this->m_next;
	newNode->m_prev = this;
	if (newNode->m_next)
	{
		newNode->m_next->m_prev = newNode;
	}
	this->m_next = newNode;
}

const Coord3D *PathNode::computeDirectionVector()
{
	static Coord3D dir;
	if (m_next == nullptr)
	{
		if (m_prev == nullptr)
		{
			dir.x = 0.0f;
			dir.y = 0.0f;
			dir.z = 0.0f;
		}
		else
		{
			return m_prev->computeDirectionVector();
		}
	}
	else
	{
		dir.x = SimMath::subf32(m_next->m_pos.x, m_pos.x);
		dir.y = SimMath::subf32(m_next->m_pos.y, m_pos.y);
		dir.z = SimMath::subf32(m_next->m_pos.z, m_pos.z);
	}
	return &dir;
}

// ---------------------------------------------------------------------------------------------------------
// Path
// ---------------------------------------------------------------------------------------------------------
Path::Path() : m_path(nullptr), m_pathTail(nullptr), m_isOptimized(false), m_blockedByAlly(false), m_closest(nullptr), m_t(0.0f), m_lastAhead()
{
}

Path::~Path()
{
	PathNode *node, *nextNode;
	for (node = m_path; node; node = nextNode)
	{
		nextNode = node->getNext();
		delete node;
	}
}

size_t Path::nodeCount() const
{
	size_t n = 0;
	for (const PathNode *p = m_path; p; p = p->getNext())
	{
		++n;
	}
	return n;
}

void Path::prependNode(const Coord3D *pos, PathfindLayerEnum layer)
{
	// RW 0x6653DC: the new node's nextOpti is the old head (ZH leaves it null), isOptimized is cleared
	PathNode *node = new PathNode();
	node->setPosition(pos);
	node->setLayer(layer);
	node->m_nextOpti = m_path;
	m_path = node->prependToList(m_path);
	if (m_pathTail == nullptr)
	{
		m_pathTail = node;
	}
	m_isOptimized = false;
}

void Path::appendNode(const Coord3D *pos, PathfindLayerEnum layer)
{
	if (m_isOptimized && m_pathTail)
	{
		// duplicate check (ZH)
		if (pos->x == m_pathTail->getPosition()->x && pos->y == m_pathTail->getPosition()->y)
		{
			return;
		}
	}
	PathNode *node = new PathNode();
	node->setPosition(pos);
	node->setLayer(layer);
	m_path = node->appendToList(m_path);
	if (m_isOptimized && m_pathTail)
	{
		m_pathTail->setNextOptimized(node);
	}
	m_pathTail = node;
}

void Path::updateLastNode(const Coord3D *pos)
{
	// RW 0x765565: the tail takes the position and the layer of the destination; unlike ZH there is no fix-up of the
	// optimised chain
	if (m_pathTail)
	{
		m_pathTail->setPosition(pos);
		m_pathTail->setLayer(LAYER_GROUND);
	}
}

namespace
{
// RW 0x76550F
inline bool isGroundLayer(PathfindLayerEnum l)
{
	return l == LAYER_GROUND || (int)l >= 16;
}

// RW aligned(c, a, step): the candidate is exactly `step` units away from the anchor on a straight or a diagonal line
inline bool aligned(const PathNode *c, const PathNode *a, int step)
{
	const int dx = SimMath::truncToInt32(SimMath::subf32(c->getPosition()->x, a->getPosition()->x));
	const int dy = SimMath::truncToInt32(SimMath::subf32(c->getPosition()->y, a->getPosition()->y));
	if (dx == 0 && std::abs(dy) == step) return true;
	if (dy == 0 && std::abs(dx) == step) return true;
	if (std::abs(dy) == std::abs(dx) && std::abs(dx) == step) return true;
	return false;
}

// RW 0x6EAD61: the cell at (layer, pos) is a cliff that is not pinched
inline bool cliffOK(Pathfinder &pf, const Coord3D *pos, PathfindLayerEnum layer)
{
	PathfindCell *cell = pf.getCell(layer, pos);
	return cell && cell->getType() == PathfindCell::CELL_CLIFF && !cell->getPinched();
}
} // namespace

void Path::optimize(Pathfinder &pathfinder, const PathfindObject *obj, unsigned acceptableSurfaces, bool blocked, const float *dir)
{
	// TARGET RW Path::optimize 0x7661A3 (S-001 caveat). It is NOT ZH's backward search: with `dir` null (every call of the
	// normal search) it is a greedy forward scan from the head; with `dir` (only the fallback of findPath, RW 0x6FE409) a
	// backward scan from the tail. Both set isOptimized and never write the tail's nextOpti.
	if (!m_path)
	{
		m_isOptimized = true;
		return;
	}
	Pathfinder::LineMemoScope lineMemo(pathfinder); // lane PATHPERF-2: the line cells' verdicts are asked once per optimize
	if (dir == nullptr)
	{
		const bool tflag = obj && obj->isKindOf(PK_MACHINE); // template KindOf bit 11 (MACHINE): RW tmpl+0x108 >> 11 & 1
		PathNode *anchor = m_path;
		for (;;)
		{
			PathNode *cand;
			if (!anchor || !(cand = anchor->m_next))
			{
				break;
			}
			int count = 0;
			PathfindLayerEnum prevLayer = anchor->m_layer;
			PathfindLayerEnum lastLayer = anchor->m_layer;
			int wpGate = anchor->m_waypointID != PathNode::NO_WAYPOINT ? 0 : 3;
			int step = 0;
			for (;;)
			{
				count++;
				step += 10;
				wpGate++;
				bool ok = false;
				if (cliffOK(pathfinder, &cand->m_pos, prevLayer))
				{
					ok = true;
				}
				else
				{
					ok = aligned(cand, anchor, step);
				}
				if (!ok)
				{
					if (cand->m_waypointID != PathNode::NO_WAYPOINT)
					{
						ok = true;
					}
					else if (wpGate < 3)
					{
						ok = true;
					}
					PathNode *n = cand->m_next;
					for (int i = 0; i < 4 && n; i++, n = n->m_next)
					{
						if (n->m_waypointID != PathNode::NO_WAYPOINT)
						{
							ok = true;
						}
					}
					if (!ok && pathfinder.isLinePassable(obj, acceptableSurfaces, prevLayer, anchor->m_pos, cand->m_pos, blocked, false))
					{
						ok = true;
					}
				}
				if (isGroundLayer(lastLayer))
				{
					if (cand->m_layer != lastLayer)
					{
						prevLayer = cand->m_layer; // (the step > 30 test of this branch changes nothing, RW 0x76xxxx)
					}
					if (tflag && !ok && std::abs(SimMath::truncToInt32(SimMath::subf32(cand->m_pos.y, m_pathTail->m_pos.y))) + std::abs(SimMath::truncToInt32(SimMath::subf32(cand->m_pos.x, m_pathTail->m_pos.x))) < 40)
					{
						if (pathfinder.isGroundLineClear(anchor->m_pos, cand->m_pos))
						{
							ok = true;
						}
					}
				}
				else
				{
					PathNode *n = cand->m_next;
					if (n && n->m_layer != lastLayer && step > 30)
					{
						ok = false;
					}
				}
				lastLayer = cand->m_layer;
				if (ok && cand->m_canOptimize)
				{
					anchor->m_nextOpti = cand;
					cand = cand->m_next;
					if (cand)
					{
						continue;
					}
					anchor = nullptr; // DONE
					break;
				}
				else if (count > 1)
				{
					anchor = cand->m_prev; // its nextOpti was set by the last success
					break;
				}
				else
				{
					anchor->m_nextOpti = cand;
					anchor = cand;
					break;
				}
			}
			if (!anchor)
			{
				break;
			}
		}
		m_isOptimized = true;
		return;
	}
	// backward scan from the tail (RW 0x7661B9), as decoded
	PathNode *anchor = m_pathTail;
	for (;;)
	{
		PathNode *cand;
		if (!anchor || !(cand = anchor->m_prev))
		{
			break;
		}
		PathfindLayerEnum L = anchor->m_layer;
		PathfindLayerEnum prevLayer = anchor->m_layer;
		int step = 0;
		bool done = false;
		for (;;)
		{
			step += 10;
			bool ok = false;
			if (cliffOK(pathfinder, &cand->m_pos, L))
			{
				ok = true;
			}
			else
			{
				ok = aligned(cand, anchor, step);
				if (!ok && pathfinder.isLinePassable(obj, acceptableSurfaces, L, anchor->m_pos, cand->m_pos, blocked, false))
				{
					ok = true;
				}
			}
			if (isGroundLayer(prevLayer))
			{
				if (cand->m_layer != prevLayer)
				{
					L = cand->m_layer;
					if (step > 30) ok = false;
				}
			}
			else
			{
				PathNode *n = cand->m_prev;
				if (n && n->m_layer != prevLayer && step > 30) ok = false;
			}
			prevLayer = cand->m_layer;
			if (ok && anchor == m_pathTail && step > 50)
			{
				const float vx = SimMath::subf32(cand->m_pos.x, anchor->m_pos.x);
				const float vy = SimMath::subf32(cand->m_pos.y, anchor->m_pos.y);
				const float inv = SimMath::divf32(1.0f, SimMath::length2d(vx, vy));
				const float dot = SimMath::addf32(SimMath::mulf32(dir[1], (SimMath::mulf32(vy, inv))), SimMath::mulf32(dir[0], (SimMath::mulf32(vx, inv))));
				if (!(0.9f <= SimMath::absD(dot)))
				{
					ok = false;
				}
			}
			if (ok && cand->m_canOptimize)
			{
				cand->m_nextOpti = anchor;
				cand = cand->m_prev;
				if (cand)
				{
					continue;
				}
				done = true;
				break;
			}
			else
			{
				// as read at RW 0x7663A9: the failing candidate also takes the anchor (suspected decoder or retail quirk, S-165)
				cand->m_nextOpti = anchor;
				anchor = cand;
				break;
			}
		}
		if (done)
		{
			break;
		}
	}
	m_isOptimized = true;
}

// ---------------------------------------------------------------------------------------------------------
// follower queries: the RotWK Path (RW 0x765598 updateClosestSegment, 0x765F31 computePointAhead, 0x765972 remaining
// length). TARGET FACTS read from the binary (S-001 caveat); the port is float32 in the retail operation order.
// ---------------------------------------------------------------------------------------------------------
namespace
{
// RW 0x7658C3: max + 0.25 * min of the two absolute components
inline float approxLength2D(float dx, float dy)
{
	const float ax = SimMath::absD(dx), ay = SimMath::absD(dy);
	return ax > ay ? SimMath::addf32(ax, SimMath::mulf32(0.25f, ay)) : SimMath::addf32(ay, SimMath::mulf32(0.25f, ax));
}

inline float length2D(float dx, float dy)
{
	return SimMath::length2d(dx, dy);
}
} // namespace

LocomotorPathPoint Path::computePointAhead(float distance)
{
	// RW 0x765F31 (lane WALL-2: with the waypoint types 1 and 3 of the nodes ahead)
	LocomotorPathPoint out;
	float rem = distance > 0.1f ? distance : 0.1f;
	if (!m_path)
	{
		m_lastAhead = Coord3D();
		m_lastAheadNode = nullptr;
		out.layer = 0;
		return out;
	}
	PathNode *cur = m_closest ? m_closest : m_path;
	float t = m_t;
	while (cur->m_nextOpti)
	{
		PathNode *nx = cur->m_nextOpti;
		float seg = length2D(SimMath::subf32(nx->m_pos.x, cur->m_pos.x), SimMath::subf32(nx->m_pos.y, cur->m_pos.y));
		if (seg < 0.01f)
		{
			seg = 0.01f; // RW 0x765819: PathNode::getNextOptimized reports max(length, 0.01)
		}
		float sf = 1.0f;
		// lane WALL-2, RW 0x765FBC .. 0x766082: the next node's waypoint (TheTerrainLogic vslot 0x8C): type 1 never stops inside the segment (its length becomes
		// rem / (1 - t) - 0.1); type 3 (Climb) slows the walk by seg / (|dz| + 1), at most 1, times 0.85 below 0.5
		if (nx->m_waypointID != PathNode::NO_WAYPOINT && m_waypoints)
		{
			if (const PathfindWaypoint *w = m_waypoints->findWaypoint(nx->m_waypointID))
			{
				if (w->type == PWAYPOINT_TYPE1)
				{
					const float left = SimMath::subf32(1.0f, t);
					if (SimMath::mulf32(left, seg) >= rem)
					{
						seg = SimMath::subf32(SimMath::divf32(rem, left), 0.1f);
					}
				}
				else if (w->type == PWAYPOINT_CLIMB)
				{
					sf = SimMath::divf32(seg, SimMath::addf32((float)SimMath::absD(SimMath::subf32(nx->m_pos.z, cur->m_pos.z)), 1.0f));
					if (sf > 1.0f)
					{
						sf = 1.0f;
					}
					else if (0.5f > sf)
					{
						sf = SimMath::mulf32(sf, 0.85f);
					}
				}
			}
		}
		if (SimMath::mulf32((SimMath::subf32(1.0f, t)), seg) >= SimMath::mulf32(sf, rem))
		{
			const float f = SimMath::addf32(t, SimMath::divf32(SimMath::mulf32(sf, rem), seg));
			Coord3D pos;
			pos.x = SimMath::addf32(cur->m_pos.x, SimMath::mulf32((SimMath::subf32(nx->m_pos.x, cur->m_pos.x)), f));
			pos.y = SimMath::addf32(cur->m_pos.y, SimMath::mulf32((SimMath::subf32(nx->m_pos.y, cur->m_pos.y)), f));
			pos.z = SimMath::addf32(cur->m_pos.z, SimMath::mulf32((SimMath::subf32(nx->m_pos.z, cur->m_pos.z)), f));
			m_lastAhead = pos;
			m_lastAheadNode = cur;
			out.position = pos;
			out.layer = (int)nx->m_layer; // RW 0x5E2D5D: the layer of (nextOpti ?: node)
			return out;
		}
		rem = SimMath::subf32(rem, SimMath::divf32(SimMath::mulf32((SimMath::subf32(1.0f, t)), seg), sf));
		t = 0.0f;
		cur = nx;
	}
	m_lastAhead = m_pathTail->m_pos;
	m_lastAheadNode = m_pathTail;
	out.position = m_pathTail->m_pos;
	out.layer = (int)m_pathTail->m_layer;
	return out;
}

void Path::updateClosestSegment(const Coord3D &pos)
{
	// RW 0x765598 with `opt` = 1 (every locomotor / AI caller): the segments walked are the optimised chain
	if (!m_path)
	{
		return;
	}
	PathNode *cur = m_closest ? m_closest : m_path;
	float best = 1e10f;
	PathNode *bestN = nullptr;
	float bestT = 0.0f;
	while (cur->m_nextOpti)
	{
		PathNode *end = cur->m_nextOpti;
		const float sx = SimMath::subf32(end->m_pos.x, cur->m_pos.x);
		const float sy = SimMath::subf32(end->m_pos.y, cur->m_pos.y);
		const float dot = SimMath::addf32(SimMath::mulf32((SimMath::subf32(pos.x, cur->m_pos.x)), sx), SimMath::mulf32((SimMath::subf32(pos.y, cur->m_pos.y)), sy));
		const float dd = SimMath::addf32(SimMath::mulf32(sx, sx), SimMath::mulf32(sy, sy));
		float t = SimMath::divf32(dot, dd);
		if (t < 0.0f) t = 0.0f;
		if (t > 1.0f) t = 1.0f;
		const float px = SimMath::subf32(SimMath::addf32(cur->m_pos.x, SimMath::mulf32(t, sx)), pos.x);
		const float py = SimMath::subf32(SimMath::addf32(cur->m_pos.y, SimMath::mulf32(t, sy)), pos.y);
		const float d2 = SimMath::addf32(SimMath::mulf32(px, px), SimMath::mulf32(py, py));
		if (d2 < best)
		{
			best = d2;
			bestN = cur;
			bestT = t;
		}
		else if (best < 0.1f)
		{
			break;
		}
		cur = cur->m_nextOpti;
	}
	if (!bestN)
	{
		return;
	}
	m_closest = bestN;
	m_t = bestT;
}

bool Path::updateClosestRawSegment(const Coord3D &pos)
{
	// RW 0x765598 with `opt` 0 (lane MOVE-3: the splice RW 0x767A66): the segment ends are the raw next nodes; the walk goes on while the next node has a next
	// optimised node (the tail has none), keeps the closest segment (a later one only when strictly closer) and stops once the closest is below 0.1
	if (!m_path)
	{
		return false;
	}
	if (!m_closest)
	{
		m_closest = m_path;
	}
	PathNode *cur = m_closest;
	if (!cur->m_nextOpti)
	{
		return false;
	}
	float best = 1e10f; // RW 0xBF7328
	float bestT = 0.0f;
	PathNode *bestN = nullptr;
	for (;;)
	{
		PathNode *end = cur->m_next;
		if (end == nullptr)
		{
			break; // RW reads the raw next of every node it reaches; a node with a next optimised node always has one
		}
		const float sx = SimMath::subf32(end->m_pos.x, cur->m_pos.x);
		const float sy = SimMath::subf32(end->m_pos.y, cur->m_pos.y);
		float t = SimMath::divf32(SimMath::addf32(SimMath::mulf32(SimMath::subf32(pos.y, cur->m_pos.y), sy), SimMath::mulf32(SimMath::subf32(pos.x, cur->m_pos.x), sx)),
			SimMath::addf32(SimMath::mulf32(sy, sy), SimMath::mulf32(sx, sx)));
		// RW 0x76563D..0x76564C: COMISS clamps only ordered values, so a zero-length segment's NaN `t` stays NaN
		if (0.0f > t)
		{
			t = 0.0f;
		}
		else if (t > 1.0f)
		{
			t = 1.0f;
		}
		const float ex = SimMath::subf32(SimMath::addf32(SimMath::mulf32(t, sx), cur->m_pos.x), pos.x);
		const float ey = SimMath::subf32(SimMath::addf32(SimMath::mulf32(t, sy), cur->m_pos.y), pos.y);
		const float d = SimMath::addf32(SimMath::mulf32(ey, ey), SimMath::mulf32(ex, ex));
		// RW 0x76567F: the segment replaces the best only when best > d (a NaN distance never does); otherwise the walk stops once the best is below 0.1
		if (best > d)
		{
			best = d;
			bestN = cur;
			bestT = t;
		}
		else if (best < 0.1f)
		{
			break;
		}
		cur = end;
		if (cur->m_nextOpti == nullptr)
		{
			break;
		}
	}
	if (bestN == nullptr)
	{
		return false;
	}
	m_closest = bestN;
	m_t = bestT;
	return true;
}

void Path::splicePatch(Path *patch)
{
	// RW 0x767A66
	if (patch == nullptr)
	{
		return;
	}
	if (patch->m_pathTail == nullptr || !updateClosestRawSegment(patch->m_pathTail->m_pos))
	{
		delete patch;
		return;
	}
	while (m_path != m_closest)
	{
		PathNode *n = m_path;
		m_path = n->m_next;
		delete n;
	}
	PathNode *join = m_closest->m_next;
	patch->m_pathTail->setNextOptimized(join);
	patch->m_pathTail->m_next = join;
	join->m_prev = patch->m_pathTail;
	delete m_path; // the closest segment's start
	m_path = patch->m_path;
	m_path->m_prev = nullptr;
	patch->m_path = nullptr;
	patch->m_pathTail = nullptr;
	delete patch;
	m_closest = m_path;
	m_t = 0.0f;
	m_lastAheadNode = nullptr;
}

bool Path::isNearPathEnd() const
{
	// RW 0x5E2DBA: a current node with a next optimised node and a 2D segment length below 10
	if (!m_path || !m_closest || !m_closest->m_nextOpti)
	{
		return false;
	}
	float seg = length2D(SimMath::subf32(m_closest->m_nextOpti->m_pos.x, m_closest->m_pos.x), SimMath::subf32(m_closest->m_nextOpti->m_pos.y, m_closest->m_pos.y));
	if (seg < 0.01f)
	{
		seg = 0.01f;
	}
	return seg < 10.0f;
}

bool Path::hasExplicitZ() const
{
	// RW 0x5E2DF4: (current.nextOpti ?: current).waypointID != none
	const PathNode *c = m_closest ? m_closest : nullptr;
	if (!c)
	{
		return false;
	}
	const PathNode *n = c->m_nextOpti ? c->m_nextOpti : c;
	return n->m_waypointID != PathNode::NO_WAYPOINT;
}

float Path::remainingFrom(const PathNode *node, const Coord3D &pos) const
{
	// RW 0x765972: 0 without a node or without a next optimised node, else the approximate 2D distance to the next
	// optimised node plus the same measure of every later segment
	if (!node || !node->m_nextOpti)
	{
		return 0.0f;
	}
	float total = approxLength2D(SimMath::subf32(node->m_nextOpti->m_pos.x, pos.x), SimMath::subf32(node->m_nextOpti->m_pos.y, pos.y));
	for (const PathNode *n = node->m_nextOpti; n->m_nextOpti; n = n->m_nextOpti)
	{
		total = SimMath::addf32(total, approxLength2D(SimMath::subf32(n->m_nextOpti->m_pos.x, n->m_pos.x), SimMath::subf32(n->m_nextOpti->m_pos.y, n->m_pos.y)));
	}
	return total;
}

float Path::remainingDistanceFrom(const LocomotorPathPoint &point) const
{
	// the locomotor passes the point computePointAhead returned: its node is the one stored with it
	return remainingFrom(m_lastAheadNode, point.position);
}

int Path::nextPortalID() const
{
	// RW 0x765710: no head or no current node -> none; else the first node from the current one along the optimised chain (+ 8) with an id
	if (!m_path || !m_closest)
	{
		return PathNode::NO_WAYPOINT;
	}
	const PathNode *n = m_closest;
	while (n && n->m_waypointID == PathNode::NO_WAYPOINT)
	{
		n = n->m_nextOpti;
	}
	return n ? n->m_waypointID : PathNode::NO_WAYPOINT;
}

void Path::advancePastPortal(int portal)
{
	// BFME2 WB 0xf1d2b0 Path::AdvancePastPortal (the asserts !m_pathHead, !m_curNode, portal != INVALID_WAYPOINT_ID return early)
	if (!m_path || !m_closest || portal == PathNode::NO_WAYPOINT)
	{
		return;
	}
	while (m_closest && m_closest->m_waypointID != portal)
	{
		m_closest = m_closest->m_nextOpti;
	}
	while (m_closest && m_closest->m_waypointID != PathNode::NO_WAYPOINT &&
		(m_closest->m_nextOpti == nullptr || m_closest->m_nextOpti->m_waypointID != PathNode::NO_WAYPOINT))
	{
		m_closest = m_closest->m_nextOpti; // (WB FUN_00f26240 on the passed node: a debug draw, no state)
	}
	if (!m_closest)
	{
		m_closest = m_pathTail;
	}
	m_t = 0.0f;
}

bool Path::hasWaypointNodes() const
{
	// RW 0x765522: a path with a current node (+ 0x10); the optimised chain (+ 8) from the head: a node whose + 0x20 is not 0x7FFFFFFF
	if (!m_path || !m_closest)
	{
		return false;
	}
	for (const PathNode *n = m_path; n; n = n->m_nextOpti)
	{
		if (n->m_waypointID != PathNode::NO_WAYPOINT)
		{
			return true;
		}
	}
	return false;
}

bool Path::hasInvalidPortals() const
{
	// RW 0x76573F: no node with an id (RW 0x765522, cached at + 0x24) -> false; else from the current node (or the head) along the raw chain (+ 0): a node
	// whose id names no portal waypoint (TheTerrainLogic vslot 0x8C, RW 0x6E82FD) -> true
	if (!m_path || !m_closest)
	{
		return false;
	}
	bool any = false;
	for (const PathNode *n = m_path; n && !any; n = n->m_nextOpti) // RW 0x765543: the optimised chain (+ 8) from the head
	{
		any = n->m_waypointID != PathNode::NO_WAYPOINT;
	}
	if (!any)
	{
		return false;
	}
	for (const PathNode *n = m_closest; n; n = n->m_next)
	{
		if (n->m_waypointID == PathNode::NO_WAYPOINT)
		{
			continue;
		}
		const PathfindWaypoint *w = m_waypoints ? m_waypoints->findWaypoint(n->m_waypointID) : nullptr;
		if (!w || !Pathfinder::isPortalWaypoint(*w))
		{
			return true;
		}
	}
	return false;
}

int Path::currentSpecialNodeType() const
{
	// RW 0x5E6E04 / 0x5E6E20: the type of the waypoint the node stands for (none here: S-165)
	return 0;
}

Coord3D Path::positionTwoAhead() const
{
	// RW 0x765A4B: the position of the node two ahead of the current one; the third node's position when it carries a
	// waypoint; the first node's when only one is ahead
	const PathNode *c = m_closest ? m_closest : m_path;
	if (!c)
	{
		return Coord3D();
	}
	const PathNode *n1 = c->m_nextOpti;
	if (!n1)
	{
		return c->m_pos;
	}
	const PathNode *n2 = n1->m_nextOpti;
	if (!n2)
	{
		return n1->m_pos;
	}
	const PathNode *n3 = n2->m_nextOpti;
	if (n3 && n3->m_waypointID != PathNode::NO_WAYPOINT)
	{
		return n3->m_pos;
	}
	return n2->m_pos;
}
