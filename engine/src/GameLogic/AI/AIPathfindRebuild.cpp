// OpenBFME. GPL-3.0.
// lane PATH-REBUILD: the finish of a found path, RW Path::rebuild 0x767B0E (thiscall: Object, the unit's 2D facing, the acceptable surfaces,
// blocked; ret 0x10). BFME2 decomp: Path::rva00365E98 (Pathfinder/rva00365E98Rva00365E98Farm.cpp) with its parts PathRva003649B1.cpp (the
// copy, RW 0x766603), PathRva00364AA2.cpp (the corner arc, RW 0x766718), PathRva00365309.cpp (the first segment bent by the facing, RW 0x766F7F),
// Pathfinder/PathRva00365B8A.cpp (the two-arc reverse, RW 0x767800) and Rva003641AEFinish.cpp (the arc's segment count, RW 0x765E00). RotWK's
// bytes (the disassembly of 0x767B0E) call the same parts in the same order as BFME2's source.
//
// What it does: the search's raw chain (every cell the A* walked, `next`) is detached and freed; the path is built again from the OPTIMISED
// nodes alone (the line-of-sight anchors, `nextOptimized`), so afterwards `next` == `nextOptimized` on every node. With a locomotor the first
// segment is bent into a circular arc from the unit's facing (the turn radius: FastTurnRadius above a quarter of the maximum speed, else
// SlowTurnRadius; the arc's points must be valid cells), a WHEELS_FOUR unit that can move backwards and faces away from its path reverses
// through two arcs, and every later corner between two plain nodes (no waypoint id) is rounded by an arc of the fast turn radius (5 with no
// locomotor). Without a locomotor the head is copied, the corners rounded with radius 5, the tail copied.
//
// Why it matters (stop S-163 before this lane; upstream review of PR #182): two readers of the RAW chain, the blocked unit's detour targets
// (RW 0x6F7938, AIPathfindPatch.cpp) and the splice's closest raw segment (RW 0x767A66, Path::splicePatch), were offered every search cell
// while retail only has the anchors (and arc points); a detour aimed at a point of the optimised line could rejoin a raw cell behind it and
// the spliced path doubled back along one line (the Rivendell campaign's starting horde flipped 180 degrees every frame, test_pathfind_move.cpp).
//
// Numerics (stop S-167): WWMath's Vector3 / LineSegClass arithmetic in float32 through the simulation facade, in the retail operation order;
// the trigonometry is SimMath's deterministic sin / cos / acos (retail: the CRT's), the normalisation the IEEE 1 / sqrt (retail: WWMath's
// fast inverse square root, RW 0x441C56). Bit parity with retail is not claimed; every platform of the port agrees.
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/SimMath.h"

#include <algorithm>

namespace
{
using Vec3 = Path::RebuildVec;

inline Vec3 vec(float x, float y, float z)
{
	Vec3 v;
	v.x = x;
	v.y = y;
	v.z = z;
	return v;
}

inline Vec3 vecOf(const Coord3D &c) { return vec(c.x, c.y, c.z); }
inline Vec3 vadd(const Vec3 &a, const Vec3 &b) { return vec(SimMath::addf32(a.x, b.x), SimMath::addf32(a.y, b.y), SimMath::addf32(a.z, b.z)); }
inline Vec3 vsub(const Vec3 &a, const Vec3 &b) { return vec(SimMath::subf32(a.x, b.x), SimMath::subf32(a.y, b.y), SimMath::subf32(a.z, b.z)); }
inline Vec3 vscale(const Vec3 &a, float s) { return vec(SimMath::mulf32(a.x, s), SimMath::mulf32(a.y, s), SimMath::mulf32(a.z, s)); }

// WWMath Vector3::Dot_Product: X * X + Y * Y + Z * Z, left to right
inline float vdot(const Vec3 &a, const Vec3 &b)
{
	return SimMath::addf32(SimMath::addf32(SimMath::mulf32(a.x, b.x), SimMath::mulf32(a.y, b.y)), SimMath::mulf32(a.z, b.z));
}

inline float crossZ(const Vec3 &a, const Vec3 &b) { return SimMath::subf32(SimMath::mulf32(a.x, b.y), SimMath::mulf32(a.y, b.x)); }

// WWMath Vector3::Cross_Product
inline Vec3 vcross(const Vec3 &a, const Vec3 &b)
{
	return vec(SimMath::subf32(SimMath::mulf32(a.y, b.z), SimMath::mulf32(a.z, b.y)), SimMath::subf32(SimMath::mulf32(a.z, b.x), SimMath::mulf32(a.x, b.z)),
		SimMath::subf32(SimMath::mulf32(a.x, b.y), SimMath::mulf32(a.y, b.x)));
}

inline float vlength2(const Vec3 &a) { return vdot(a, a); }
inline float vlength(const Vec3 &a) { return (float)SimMath::sqrtd((double)vlength2(a)); }

// WWMath Vector3::Normalize: nothing for a zero vector, else the scale by the inverse square root of the squared length (the IEEE one here, S-167)
inline Vec3 vnormalized(const Vec3 &a)
{
	const float len2 = vlength2(a);
	if (len2 == 0.0f)
	{
		return a;
	}
	return vscale(a, SimMath::divf32(1.0f, (float)SimMath::sqrtd((double)len2)));
}

// Coord3D::normalize (the decomp's Lib/Coord3D.h): the length, then the scale by 1 / length
inline Vec3 coordNormalized(const Vec3 &a)
{
	const float len = vlength(a);
	if (len == 0.0f)
	{
		return a;
	}
	return vscale(a, SimMath::divf32(1.0f, len));
}

// WWMath Vector3::Rotate_Z(angle): X' = c * X - s * Y, Y' = s * X + c * Y
inline Vec3 rotateZ(const Vec3 &a, float angle)
{
	const float s = SimMath::sinDet(angle);
	const float c = SimMath::cosDet(angle);
	return vec(SimMath::subf32(SimMath::mulf32(c, a.x), SimMath::mulf32(s, a.y)), SimMath::addf32(SimMath::mulf32(s, a.x), SimMath::mulf32(c, a.y)), a.z);
}

// RW 0x76579D (decomp rva00363B4BAngle): the angle between two unit vectors, 0 when they nearly agree
inline float angleBetween(const Vec3 &a, const Vec3 &b)
{
	float dot = SimMath::addf32(SimMath::addf32(SimMath::mulf32(a.z, b.z), SimMath::mulf32(a.y, b.y)), SimMath::mulf32(a.x, b.x));
	if (dot > 1.0f)
	{
		dot = 1.0f;
	}
	if (dot < -1.0f)
	{
		dot = -1.0f;
	}
	if (dot >= 0.99f)
	{
		return 0.0f;
	}
	return (float)SimMath::acosDet((double)dot);
}

// WWMath LineSegClass: P0, P1, the normalised direction and the length
struct Seg
{
	Vec3 p0, p1, dir;
	float length = 0.0f;
	void set(const Vec3 &a, const Vec3 &b)
	{
		p0 = a;
		p1 = b;
		const Vec3 dp = vsub(b, a);
		dir = vnormalized(dp);
		length = vlength(dp);
	}
};

// WWMath LineSegClass::Find_Intersection: the closest points of the two lines (the fractions the caller passes are unused)
bool findIntersection(const Seg &a, const Seg &b, Vec3 &pa, Vec3 &pb)
{
	const Vec3 cross1 = vcross(a.dir, b.dir);
	const Vec3 cross2 = vcross(vsub(b.p0, a.p0), b.dir);
	const float top1 = vdot(cross2, cross1);
	const float bottom1 = vdot(cross1, cross1);
	const Vec3 cross3 = vcross(b.dir, a.dir);
	const Vec3 cross4 = vcross(vsub(a.p0, b.p0), a.dir);
	const float top2 = vdot(cross4, cross3);
	const float bottom2 = vdot(cross3, cross3);
	if (bottom1 != 0.0f && bottom2 != 0.0f)
	{
		const float length1 = SimMath::divf32(top1, bottom1);
		const float length2 = SimMath::divf32(top2, bottom2);
		pa = vadd(a.p0, vscale(a.dir, length1));
		pb = vadd(b.p0, vscale(b.dir, length2));
		return true;
	}
	return false;
}

constexpr float kHalfPi = 1.5707963705062866f;
constexpr float kMinBend = 0.19634955f;        // pi / 16: a bend below it is not taken (RW 0x766F7F)
constexpr float kThreeHalfPi = 4.712389f;      // 3 pi / 2: a sweep past it is dropped (RW 0x766F7F)
constexpr float kTwoPi = 6.2831855f;
constexpr float kDefaultRadius = 5.0f; // the segment speed and the fallback radius with no locomotor entry
} // namespace

// RW 0x765E00 (decomp Path::rva003641AE): the arc's segment count, ceil(angle * radius / 30 * 16 / pi), 1..128. RW multiplies on the x87 at
// precision control 24 (RW 0x440809 sets it; NumericState.h), so each product rounds to a float: radius * (1 / 30) first, then the angle,
// then 16 / pi (pc24Mul); a double accumulator lands the ceil on the far side of an integer at the common corners (a right angle at radius
// 15: 5 segments instead of RW's 4; tools/retail_oracle/test_path_rebuild_oracle.py holds the table to RW). The integral value's rounding is FISTP's
int Path::arcSegments(float angle, float radius)
{
	float scaled = SimMath::pc24Mul(radius, 0.03333333507180214f);
	scaled = SimMath::pc24Mul(scaled, angle);
	scaled = SimMath::pc24Mul(scaled, 5.092957973480225f);
	const float ceiled = (float)SimMath::ceilD((double)scaled);
	int i = SimMath::fistp32((double)ceiled);
	if (i < 1)
	{
		i = 1;
	}
	if (i > 128)
	{
		i = 128;
	}
	return i;
}

// RW 0x766603 (decomp Path::rva003649B1): a copy of `src` (position, layer, waypoint id) appended; on an optimised path a copy at the tail's x / y
// is dropped and the tail's optimised link is the copy
// an arc point: a node at `pos` on `layer` with no waypoint id, linked at the tail as appendCopy links (appendNode walks from the head, so the arcs of
// a long chain would cost its square); a point at the tail's x / y is dropped on an optimised path (RW 0x665429)
void Path::appendPoint(const Coord3D *pos, PathfindLayerEnum layer)
{
	if (m_isOptimized && m_pathTail && pos->x == m_pathTail->getPosition()->x && pos->y == m_pathTail->getPosition()->y)
	{
		return;
	}
	PathNode *node = new PathNode();
	node->setPosition(pos);
	node->setLayer(layer);
	if (m_pathTail)
	{
		m_pathTail->append(node);
	}
	else
	{
		m_path = node;
	}
	if (m_isOptimized && m_pathTail)
	{
		m_pathTail->setNextOptimized(node);
	}
	m_pathTail = node;
}

void Path::appendCopy(const PathNode *src)
{
	const Coord3D *pos = src->getPosition();
	if (m_isOptimized && m_pathTail && pos->x == m_pathTail->getPosition()->x && pos->y == m_pathTail->getPosition()->y)
	{
		return;
	}
	PathNode *node = new PathNode();
	node->setPosition(pos);
	node->setLayer(src->getLayer());
	node->setWaypointID(src->getWaypointID());
	if (m_pathTail)
	{
		m_pathTail->append(node); // at the tail (appendToList walks from the head)
	}
	else
	{
		m_path = node;
	}
	if (m_isOptimized && m_pathTail)
	{
		m_pathTail->setNextOptimized(node);
	}
	m_pathTail = node;
}

// RW 0x766718 (decomp Path::rva00364AA2): the corner at `cur` between `prev` and `next` rounded by an arc of `radius` (at most three quarters of
// the shorter leg; 1.75 times more under 0.7226 rad), its points appended in place of the corner and the corner node moved to the arc's end. A
// corner with a waypoint id or sharper than 1.7279 rad is copied as it is; an arc of fewer than 2 segments too; a corner whose offset legs do not
// meet drops the node
void Path::roundCorner(PathNode *prev, PathNode *cur, PathNode *next, float radius)
{
	const Vec3 curPos = vecOf(*cur->getPosition());
	const Vec3 prevPos = vec(prev->getPosition()->x, prev->getPosition()->y, curPos.z);
	const Vec3 nextPos = vec(next->getPosition()->x, next->getPosition()->y, curPos.z);
	Seg seg1, seg2;
	seg1.set(vec(prevPos.x, prevPos.y, 0.0f), vec(curPos.x, curPos.y, 0.0f));
	seg2.set(vec(curPos.x, curPos.y, 0.0f), vec(nextPos.x, nextPos.y, 0.0f));
	float minLen = seg1.length;
	if (minLen > seg2.length)
	{
		minLen = seg2.length;
	}
	if (radius > SimMath::mulf32(minLen, 0.75f))
	{
		radius = SimMath::mulf32(minLen, 0.75f);
	}
	Vec3 a, b, c, d;
	bool turnLeft;
	if (crossZ(seg1.dir, seg2.dir) < 0.0f)
	{
		a = vec(prevPos.x, prevPos.y, 0.0f);
		b = vec(curPos.x, curPos.y, 0.0f);
		c = b;
		d = vec(nextPos.x, nextPos.y, 0.0f);
		turnLeft = true;
	}
	else
	{
		d = vec(prevPos.x, prevPos.y, 0.0f);
		c = vec(curPos.x, curPos.y, 0.0f);
		b = c;
		a = vec(nextPos.x, nextPos.y, 0.0f);
		turnLeft = false;
		seg1.set(a, b);
		seg2.set(c, d);
	}
	const float angle = angleBetween(seg1.dir, seg2.dir);
	if ((double)angle < 0.7225663304328919)
	{
		radius = SimMath::mulf32(radius, 1.75f);
	}
	if (cur->getWaypointID() != PathNode::NO_WAYPOINT || angle > 1.7278761f)
	{
		appendCopy(cur);
		return;
	}
	Vec3 v1 = rotateZ(vscale(seg1.dir, radius), -kHalfPi);
	Vec3 v2 = rotateZ(vscale(seg2.dir, radius), -kHalfPi);
	Seg off1, off2;
	off1.set(vadd(a, v1), vadd(b, v1));
	off2.set(vadd(c, v2), vadd(d, v2));
	const float arc = angleBetween(vnormalized(v1), vnormalized(v2));
	const int count = arcSegments(arc, radius);
	if (count < 2)
	{
		appendCopy(cur);
		return;
	}
	const float step = SimMath::divf32(arc, (float)count);
	Vec3 center, other;
	if (!findIntersection(off1, off2, center, other))
	{
		return;
	}
	const auto append = [this, cur, &curPos](const Vec3 &q) {
		Coord3D pt;
		pt.x = q.x;
		pt.y = q.y;
		pt.z = curPos.z;
		appendPoint(&pt, cur->getLayer());
		return pt;
	};
	if (turnLeft)
	{
		append(vsub(center, v1));
		v1 = vscale(v1, -1.0f);
		for (int i = 1; i < count; ++i)
		{
			const Vec3 q = vadd(center, v1);
			v1 = rotateZ(v1, -step);
			append(q);
		}
		const Coord3D end = append(vsub(center, v2));
		cur->setPosition(&end);
	}
	else
	{
		append(vsub(center, v2));
		v2 = vscale(v2, -1.0f);
		for (int i = 1; i < count; ++i)
		{
			v2 = rotateZ(v2, step);
			append(vadd(center, v2));
		}
		const Coord3D end = append(vsub(center, v1));
		cur->setPosition(&end);
	}
}

// RW 0x766F7F (decomp Path::rva00365309): the first segment (node -> its optimised next) bent into a circular arc of `opt.radius` from the facing
// `input`, when the bend is between 0.1963 rad and `maxAngle` and the node carries no waypoint id. `opt.dryRun`: only the test that every arc point
// is a valid cell for the unit and the last one sees the segment's end (false otherwise; nothing appended); `opt.otherWay`: the arc the other
// way round (refused, and cleared, under a right angle); `opt.noCornerAfter`: no corner arc after the bend (the reverse's two calls). Otherwise
// the node's copy, the arc points (the node's position moves with them) and, unless `noCornerAfter`, a corner arc from the arc's end
bool Path::bendFirstSegment(Pathfinder &pf, const PathfindObject *obj, const PathfindLocomotorInfo &loco, PathNode *node, const RebuildVec &input,
	BendOptions &opt, unsigned surfaces, float maxAngle, bool blocked)
{
	PathNode *next = node->getNextOptimized();
	if (node->getWaypointID() != PathNode::NO_WAYPOINT || !next)
	{
		if (!opt.dryRun)
		{
			appendCopy(node);
		}
		return true;
	}
	float radius = opt.radius;
	const Vec3 start = vecOf(*node->getPosition());
	Vec3 dest = vecOf(*next->getPosition());
	{
		const float halfLength = SimMath::mulf32(vlength(vsub(dest, start)), 0.5f);
		if (halfLength < radius)
		{
			radius = halfLength;
		}
	}
	const Vec3 direction = vnormalized(input);
	const Vec3 towards = vnormalized(vsub(dest, start));
	float sign = crossZ(direction, towards) > 0.0f ? 1.0f : -1.0f;
	bool positive = true;
	float bend = angleBetween(direction, towards);
	if (bend < kMinBend || bend > maxAngle)
	{
		if (!opt.dryRun)
		{
			appendCopy(node);
		}
		return true;
	}
	float rotation = kHalfPi;
	if (opt.otherWay)
	{
		if ((double)bend < (double)kHalfPi)
		{
			opt.otherWay = false;
			if (opt.dryRun)
			{
				return false;
			}
		}
		else
		{
			sign = -sign;
		}
	}
	if (sign < 0.0f)
	{
		positive = false;
		bend = -bend;
		rotation = -rotation;
	}
	Vec3 offset = vscale(rotateZ(direction, rotation), radius);
	if (!opt.dryRun)
	{
		appendCopy(node);
	}
	Vec3 center = vecOf(*node->getPosition());
	center.x = SimMath::addf32(center.x, offset.x);
	center.y = SimMath::addf32(center.y, offset.y);
	Vec3 toDest = dest;
	toDest.x = SimMath::subf32(toDest.x, center.x);
	toDest.y = SimMath::subf32(toDest.y, center.y);
	const Vec3 radial = vscale(vnormalized(rotateZ(toDest, -rotation)), radius);
	Vec3 end = center;
	end.x = SimMath::addf32(end.x, radial.x);
	end.y = SimMath::addf32(end.y, radial.y);
	offset = vscale(offset, -1.0f);
	Vec3 firstRadial = offset;
	firstRadial.z = 0.0f;
	firstRadial = vnormalized(firstRadial);
	Vec3 secondRadial = radial;
	secondRadial.z = 0.0f;
	secondRadial = vnormalized(secondRadial);
	float sweep = angleBetween(firstRadial, secondRadial);
	const float radialSign = crossZ(secondRadial, firstRadial) > 0.0f ? 1.0f : -1.0f;
	if (radialSign == sign)
	{
		sweep = SimMath::subf32(kTwoPi, sweep);
	}
	Vec3 remainder = vec(SimMath::subf32(dest.x, end.x), SimMath::subf32(dest.y, end.y), 0.0f);
	remainder = rotateZ(vnormalized(remainder), -rotation);
	sweep = SimMath::addf32(sweep, angleBetween(remainder, secondRadial));
	if (opt.otherWay)
	{
		sweep = SimMath::subf32(kTwoPi, sweep);
	}
	if (sweep > kThreeHalfPi)
	{
		sweep = SimMath::subf32(sweep, kTwoPi);
		if (sweep < 0.0f)
		{
			sweep = 0.0f;
		}
	}
	const int segments = arcSegments(sweep, radius);
	if (segments < 2)
	{
		return true;
	}
	if (!positive)
	{
		sweep = -sweep;
	}
	const float step = SimMath::divf32(sweep, (float)segments);
	for (int i = 1; i <= segments; ++i)
	{
		const Vec3 point = rotateZ(offset, SimMath::mulf32((float)i, step));
		Coord3D position;
		position.x = SimMath::addf32(center.x, point.x);
		position.y = SimMath::addf32(center.y, point.y);
		position.z = start.z;
		if (opt.dryRun)
		{
			if (!pf.validMovementPosition(obj, loco, node->getLayer(), &position))
			{
				return false;
			}
			Coord3D destPos;
			destPos.x = dest.x;
			destPos.y = dest.y;
			destPos.z = dest.z;
			if (i == segments && !pf.isLinePassable(obj, surfaces, node->getLayer(), position, destPos, blocked))
			{
				return false;
			}
		}
		else
		{
			appendPoint(&position, node->getLayer());
			node->setPosition(&position);
		}
	}
	if (!opt.dryRun && !opt.noCornerAfter)
	{
		PathNode saved;
		saved.setPosition(node->getPosition());
		saved.setLayer(LAYER_GROUND);
		Coord3D mid;
		mid.x = SimMath::mulf32(SimMath::addf32(dest.x, node->getPosition()->x), 0.5f);
		mid.y = SimMath::mulf32(SimMath::addf32(dest.y, node->getPosition()->y), 0.5f);
		mid.z = SimMath::mulf32(SimMath::addf32(dest.z, node->getPosition()->z), 0.5f);
		node->setPosition(&mid);
		const float cornerRadius = loco.present ? loco.fastTurnRadius : radius;
		roundCorner(&saved, node, next, cornerRadius);
	}
	return true;
}

// RW 0x767800 (decomp Path::rva00365B8A): a unit facing away from a long first segment reverses out: two bends of `radius` (no corner arc after
// either), the first from the facing's opposite toward a point beside the start, the second from that side back onto the segment
void Path::reverseTurn(Pathfinder &pf, const PathfindObject *obj, const PathfindLocomotorInfo &loco, PathNode *start, PathNode *end, const RebuildVec &input,
	float radius)
{
	const Vec3 reverse = vscale(input, -1.0f);
	const Vec3 direction = coordNormalized(vsub(vecOf(*end->getPosition()), vecOf(*start->getPosition())));
	BendOptions opt;
	opt.dryRun = false;
	opt.otherWay = false;
	opt.radius = radius;
	opt.noCornerAfter = true;
	const float angle = 3.1415927f;
	const float cross = SimMath::subf32(SimMath::mulf32(reverse.x, direction.y), SimMath::mulf32(direction.x, reverse.y));
	const bool positive = cross > 0.0f;
	const Vec3 middle = vscale(vadd(reverse, direction), SimMath::mulf32(radius, 0.5f));
	Vec3 side = vec(SimMath::subf32(-direction.y, reverse.y), SimMath::addf32(direction.x, reverse.x), 0.0f);
	side = coordNormalized(side);
	if (positive)
	{
		side = vscale(side, -1.0f);
	}
	side = vscale(side, SimMath::mulf32(radius, 2.0f));
	const Coord3D saved = *end->getPosition();
	const Vec3 target = vadd(vadd(vecOf(*start->getPosition()), side), middle);
	Coord3D targetPos;
	targetPos.x = target.x;
	targetPos.y = target.y;
	targetPos.z = target.z;
	end->setPosition(&targetPos);
	bendFirstSegment(pf, obj, loco, start, reverse, opt, 0xffff, angle, false);
	end->setPosition(&saved);
	side = vscale(side, -1.0f);
	bendFirstSegment(pf, obj, loco, start, side, opt, 0xffff, angle, false);
}

// RW 0x767B0E: see the file comment. `facing` is the unit's 2D direction (RW 0x70B9E0); `surfaces` the locomotor's acceptable surfaces
void Path::rebuild(Pathfinder &pf, const PathfindObject *obj, const PathfindLocomotorInfo &loco, const Coord2D &facing, unsigned surfaces, bool blocked)
{
	if (!m_path)
	{
		return;
	}
	PathNode *original = m_path;
	m_path = nullptr;
	m_pathTail = nullptr;
	m_isOptimized = true;
	m_closest = nullptr; // a fresh path: the follower has not selected a node (RW leaves +0x10 as the constructor did)
	m_t = 0.0f;
	m_lastAheadNode = nullptr;
	const Vec3 input = vec(facing.x, facing.y, 0.0f);
	Vec3 direction = input;
	float turn = 0.0f;
	float segmentSpeed = kDefaultRadius;
	bool reversed = false, longSegment = false;
	float longSpeed = kDefaultRadius;
	if (loco.present)
	{
		// RW 0x5E4CA7 picks the radius: FastTurnRadius (RW 0x69AE01, template + 0x38) above a quarter of the maximum speed, else SlowTurnRadius (0x69ADFA, + 0x34)
		turn = loco.fasterThanQuarterSpeed ? loco.fastTurnRadius : loco.slowTurnRadius;
		if (loco.canReverse && obj) // (a locomotor's facts come with an object; a search without one has none, see buildActualPath)
		{
			// RW 0x765819: the 2D leg to the next optimised node, its length at least 0.01; RW 0x5E389C: the unit faces away from that node
			const PathNode *next = original->getNextOptimized();
			if (next)
			{
				float length = SimMath::length2d(SimMath::subf32(next->getPosition()->x, original->getPosition()->x), SimMath::subf32(next->getPosition()->y, original->getPosition()->y));
				if (length < 0.01f)
				{
					length = 0.01f;
				}
				const Coord3D &unit = obj->getPosition();
				const float dot = SimMath::addf32(SimMath::mulf32(SimMath::subf32(next->getPosition()->x, unit.x), facing.x),
					SimMath::mulf32(SimMath::subf32(next->getPosition()->y, unit.y), facing.y));
				if (dot < 0.0f)
				{
					direction = vscale(direction, -1.0f);
					reversed = true;
					longSpeed = std::max(loco.fastTurnRadius, loco.slowTurnRadius);
					longSegment = length > SimMath::mulf32(longSpeed, 3.0f);
				}
			}
		}
		segmentSpeed = loco.fastTurnRadius;
	}
	PathNode *current = original->getNextOptimized();
	PathNode *previous = original;
	if (reversed && longSegment)
	{
		reverseTurn(pf, obj, loco, original, current, input, longSpeed);
	}
	else if (turn > 0.0f)
	{
		BendOptions opt;
		opt.dryRun = true;
		opt.otherWay = false;
		opt.radius = turn;
		opt.noCornerAfter = false;
		float maxAngle = loco.maxTurnWithoutReform;
		if (loco.fasterThanQuarterSpeed)
		{
			maxAngle = std::min(loco.turnThresholdHS, maxAngle);
		}
		bool ok = bendFirstSegment(pf, obj, loco, original, direction, opt, surfaces, maxAngle, blocked);
		if (loco.floatingZ)
		{
			ok = true;
		}
		if (!ok)
		{
			opt.otherWay = true;
			ok = bendFirstSegment(pf, obj, loco, original, direction, opt, surfaces, maxAngle, blocked);
			if (!ok)
			{
				opt.otherWay = false;
				opt.radius = kDefaultRadius;
			}
		}
		opt.dryRun = false;
		bendFirstSegment(pf, obj, loco, original, direction, opt, surfaces, maxAngle, blocked);
	}
	else
	{
		appendCopy(original);
	}
	while (current)
	{
		PathNode *next = current->getNextOptimized();
		if (next && current->getWaypointID() == PathNode::NO_WAYPOINT && next->getWaypointID() == PathNode::NO_WAYPOINT)
		{
			roundCorner(previous, current, next, segmentSpeed);
		}
		else
		{
			appendCopy(current);
		}
		previous = current;
		current = next;
	}
	for (PathNode *node = original; node;)
	{
		PathNode *next = node->getNext();
		delete node;
		node = next;
	}
}
