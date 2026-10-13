// OpenBFME. GPL-3.0.
//
// The aerial path of the GIANT_BIRD flyers (lane FLY-1); see AerialPathfinder.h for the target facts and the stop.

#include "GameLogic/AI/AerialPathfinder.h"

#include "Common/NumericState.h"
#include "Common/StateHash.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/BuildPlacement.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/SimMath.h"

#include <cfloat>

namespace
{
using SimMath::addf32;
using SimMath::divf32;
using SimMath::mulf32;
using SimMath::subf32;

// one coordinate of the Catmull-Rom segment (RW 0x7139E6, in its SSE order): ((((P2 - P0) t + P1 * 2) + (((P0 * 2 - P1 * 5) + P2 * 4) - P3) t^2)
// + (((P1 * 3 - P0) - P2 * 3) + P3) t^3) * 0.5
float catmullRom(float p0, float p1, float p2, float p3, float t, float t2, float t3)
{
	const float a = mulf32(subf32(p2, p0), t);
	const float b = mulf32(p1, 2.0f);                                                       // RW 0xBD889C
	const float c = mulf32(subf32(addf32(subf32(mulf32(p0, 2.0f), mulf32(p1, 5.0f)), mulf32(p2, 4.0f)), p3), t2); // RW 0xBDAE58, 0xBD88C0
	const float d = mulf32(addf32(subf32(subf32(mulf32(p1, 3.0f), p0), mulf32(p2, 3.0f)), p3), t3);              // RW 0xBDD42C
	return mulf32(addf32(addf32(addf32(a, b), c), d), 0.5f); // RW 0xBD869C
}

// RW 0x403175 Coord3D::normalize: the x87 length (sqrt of the double sum), each component divided (SSE)
void normalize3(Coord3D &v)
{
	const float len = (float)SimMath::length3d(v.x, v.y, v.z);
	if (len != 0.0f)
	{
		v.x = divf32(v.x, len);
		v.y = divf32(v.y, len);
		v.z = divf32(v.z, len);
	}
}

float length3(const Coord3D &v)
{
	return (float)SimMath::length3d(v.x, v.y, v.z); // RW 0x403111 then fstp dword
}

// RW 0x644FD0 (two SSE loops)
float normalizeAngle(float a)
{
	const float pi = 3.14159274f, twoPi = 6.28318548f;
	if (a > pi)
	{
		do
		{
			a = subf32(a, twoPi);
		} while (a > pi);
	}
	if (-pi < a)
	{
		return a;
	}
	do
	{
		a = addf32(a, twoPi);
	} while (-pi >= a);
	return a;
}

bool isMobileForSeparation(const Object &o)
{
	static const int kImmobile = ObjectTemplateInfoBuilder::kindOfIndex("IMMOBILE");
	return !(kImmobile >= 0 && o.isKindOf((unsigned)kImmobile));
}
} // namespace

// ---- the spline ------------------------------------------------------------------------------------------------------------------------------------------
void AerialSpline::append(const Coord3D &p)
{
	Record r;
	r.point = p;
	m_records.push_back(r);
}

void AerialSpline::insertAfterSegment(const Coord3D &p)
{
	Record r;
	r.point = p;
	const size_t at = (size_t)m_segment + 2u;
	m_records.insert(m_records.begin() + (std::ptrdiff_t)(at <= m_records.size() ? at : m_records.size()), r);
}

// RW 0x712D75 (this, 1, 4000, 1000.0, 1000.0, 0, 0): + 0x18 = + 0x1C = + 0x20 = 0, + 0x40 = 0, + 0x3C = 0, + 0x48 .. + 0x50 = 0, + 0x24 = 1; with the last
// argument 0 the total length + 0x38 stays. The remaining stores (+ 4 the step count, + 8, + 0xC, + 0x28, + 0x45, + 0x46 and the + 0x10 interpolator of
// RW 0x70FCC0) feed the camera use of the class only.
void AerialSpline::prepare()
{
	m_time = 0.0f;
	m_segmentBegin = 0.0f;
	m_segment = 0;
	m_sample = 0;
	m_sampleBegin = 0.0f;
	m_result = Coord3D{ 0.0f, 0.0f, 0.0f };
	m_valid = true;
}

void AerialSpline::build()
{
	const int segments = (int)m_records.size() - 3;
	for (int i = 0; i < segments; ++i)
	{
		Record &rec = m_records[(size_t)i];
		rec.duration = 0.0f;
		const Coord3D p0 = m_records[(size_t)i].point;
		const Coord3D p1 = m_records[(size_t)i + 1].point;
		const Coord3D p2 = m_records[(size_t)i + 2].point;
		const Coord3D p3 = m_records[(size_t)i + 3].point;
		// t = 0 (RW multiplies the t terms by the literal 0.0)
		float lx = catmullRom(p0.x, p1.x, p2.x, p3.x, 0.0f, 0.0f, 0.0f);
		float ly = catmullRom(p0.y, p1.y, p2.y, p3.y, 0.0f, 0.0f, 0.0f);
		float lz = catmullRom(p0.z, p1.z, p2.z, p3.z, 0.0f, 0.0f, 0.0f);
		for (int k = 1; k <= kSamples; ++k)
		{
			const float t = mulf32(SimMath::sseFromInt32(k), 0.1f); // cvtsi2ss, RW 0xBD83D4
			const float t2 = mulf32(t, t);
			const float t3 = mulf32(t2, t);
			const float x = catmullRom(p0.x, p1.x, p2.x, p3.x, t, t2, t3);
			const float y = catmullRom(p0.y, p1.y, p2.y, p3.y, t, t2, t3);
			const float z = catmullRom(p0.z, p1.z, p2.z, p3.z, t, t2, t3);
			const float dx = subf32(x, lx), dy = subf32(y, ly), dz = subf32(z, lz);
			const float sum = addf32(addf32(mulf32(dx, dx), mulf32(dz, dz)), mulf32(dy, dy)); // RW 0x714439 .. 0x71445E
			const float step = SimMath::fstpDword(SimMath::sqrtPC24((double)sum));           // fld; fsqrt; fstp dword (RW 0x71446A)
			rec.duration = addf32(step, rec.duration);
			rec.steps[k - 1] = step;
			rec.samples[k - 1] = Coord3D{ x, y, z };
			lx = x;
			ly = y;
			lz = z;
		}
		m_totalLength = addf32(rec.duration, m_totalLength);
	}
	if (m_records.size() >= 2)
	{
		m_endPoint = m_records[m_records.size() - 2].point; // RW 0x713FFB: the record count - 2's control point
	}
}

namespace
{
// the record's float at byte offset 4 * index, as RW reads it (+ 0 duration, + 4 the ten sample lengths, + 0x2C the ten sample points, + 0xA4 the control
// point), through the named fields
float recordFloat(const AerialSpline::Record &r, int index)
{
	if (index == 0)
	{
		return r.duration;
	}
	if (index <= AerialSpline::kSamples)
	{
		return r.steps[index - 1];
	}
	const int k = index - 1 - AerialSpline::kSamples;
	const Coord3D &c = k < 3 * AerialSpline::kSamples ? r.samples[k / 3] : r.point;
	const int axis = k < 3 * AerialSpline::kSamples ? k % 3 : k - 3 * AerialSpline::kSamples;
	return axis == 0 ? c.x : axis == 1 ? c.y : c.z;
}
constexpr int kRecordFloats = 1 + AerialSpline::kSamples + 3 * AerialSpline::kSamples + 3; // the first 0xB0 bytes
} // namespace

bool AerialSpline::evaluate()
{
	if (!m_valid || m_records.empty())
	{
		return false;
	}
	const size_t count = m_records.size();
	if (m_records[m_segment].duration <= subf32(m_time, m_segmentBegin))
	{
		do
		{
			m_segmentBegin = addf32(m_records[m_segment].duration, m_segmentBegin);
			m_segment += 1;
			m_sample = 0;
			m_sampleBegin = 0.0f;
			if (count < (size_t)m_segment + 3u)
			{
				return false; // RW 0x7132D9: `count < old segment + 4`: no segment left
			}
		} while (m_records[m_segment].duration <= subf32(m_time, m_segmentBegin));
	}
	const Record &rec = m_records[m_segment];
	float t = subf32(subf32(m_time, m_segmentBegin), m_sampleBegin);
	// RW reads the sample length at record + 4 + 4 * sample without a bound: past the tenth it reads on into the samples (recordFloat; a walk beyond the
	// record would be a retail read of the next record, refused here)
	float step = recordFloat(rec, 1 + m_sample);
	while (step <= t)
	{
		m_sample += 1;
		m_sampleBegin = addf32(m_sampleBegin, step);
		t = subf32(t, step);
		if (1 + m_sample >= kRecordFloats)
		{
			return false;
		}
		step = recordFloat(rec, 1 + m_sample);
	}
	float f = divf32(t, step);
	if (step <= 0.0f)
	{
		f = 1.0f; // RW 0xC1B594 / 0xBD1908
	}
	if (m_sample >= kSamples)
	{
		return false; // past the samples (see above)
	}
	const Coord3D &a = m_sample < 1 ? m_records[m_segment + 1].point : rec.samples[m_sample - 1];
	const Coord3D &b = rec.samples[m_sample];
	m_result.x = addf32(mulf32(subf32(b.x, a.x), f), a.x);
	m_result.y = addf32(mulf32(subf32(b.y, a.y), f), a.y);
	m_result.z = addf32(mulf32(subf32(b.z, a.z), f), a.z);
	return true;
}

void AerialSpline::fixEndpoints()
{
	const size_t n = m_records.size();
	if (n < 3)
	{
		return; // RW reads records 0 .. 2 and n - 3 .. n - 1 unchecked; the builder always holds more
	}
	{
		const Coord3D p1 = m_records[1].point;
		const Coord3D p2 = m_records[2].point;
		Coord3D d{ subf32(p1.x, p2.x), subf32(p1.y, p2.y), subf32(p1.z, p2.z) };
		const float len = length3(d);
		Coord3D u{ subf32(p1.x, m_records[0].point.x), subf32(p1.y, m_records[0].point.y), subf32(p1.z, m_records[0].point.z) };
		normalize3(u);
		m_records[0].point = Coord3D{ subf32(p1.x, mulf32(u.x, len)), subf32(p1.y, mulf32(u.y, len)), subf32(p1.z, mulf32(u.z, len)) };
	}
	{
		const Coord3D pe = m_records[n - 2].point;
		const Coord3D pb = m_records[n - 3].point;
		Coord3D d{ subf32(pe.x, pb.x), subf32(pe.y, pb.y), subf32(pe.z, pb.z) };
		const float len = length3(d);
		Coord3D &last = m_records[n - 1].point;
		Coord3D u{ subf32(pe.x, last.x), subf32(pe.y, last.y), subf32(pe.z, last.z) };
		normalize3(u);
		last = Coord3D{ subf32(pe.x, mulf32(u.x, len)), subf32(pe.y, mulf32(u.y, len)), subf32(pe.z, mulf32(u.z, len)) };
	}
}

bool AerialSpline::nearInteriorPoint(const Coord3D &p, float radius) const
{
	const size_t n = m_records.size();
	for (size_t i = 2; i < n && i + 2 != n; ++i)
	{
		const float dx = subf32(m_records[i].point.x, p.x);
		const float dy = subf32(m_records[i].point.y, p.y);
		if ((double)radius > SimMath::length3d(dx, dy, 0.0f)) // fld radius; fcompi with the double length (RW 0x71388A)
		{
			return true;
		}
	}
	return false;
}

void AerialSpline::crc(StateHasher &h) const
{
	h.addU32((std::uint32_t)m_records.size());
	for (const Record &r : m_records)
	{
		h.addFloat(r.point.x);
		h.addFloat(r.point.y);
		h.addFloat(r.point.z);
		h.addFloat(r.duration);
	}
	h.addFloat(m_time);
	h.addFloat(m_segmentBegin);
	h.addU32(m_segment);
	h.addBool(m_valid);
	h.addFloat(m_totalLength);
	h.addFloat(m_sampleBegin);
	h.addI32(m_sample);
	h.addFloat(m_result.x);
	h.addFloat(m_result.y);
	h.addFloat(m_result.z);
	h.addFloat(m_endPoint.x);
	h.addFloat(m_endPoint.y);
	h.addFloat(m_endPoint.z);
}

// ---- TheAerialPathfinder ----------------------------------------------------------------------------------------------------------------------------------
namespace AerialPathfinder
{
bool query(AerialSpline &spline, float distance, Coord3D &out)
{
	if (!(0.0f <= distance))
	{
		return false;
	}
	spline.prepare();
	if (!spline.valid())
	{
		return false;
	}
	spline.setTime(distance);
	if (!spline.evaluate())
	{
		return false;
	}
	out = spline.result();
	return true;
}

float flightGroundHeight(const GameLogic &logic, float x, float y)
{
	const float terrain = logic.getGroundHeight(x, y); // TheTerrainLogic vslot 0x18
	const float noFly = 0.0f;                          // RW 0x777E54 with the empty no-fly list (S-4041)
	float h = terrain;
	if (terrain < noFly)
	{
		h = noFly;
	}
	const float minFlight = FLT_MAX; // AIData MinFlightHeight (+ 0xB0), the retail default (S-4041)
	if (minFlight != FLT_MAX && h < minFlight)
	{
		h = minFlight;
	}
	return h;
}

bool separation(const Flyer &flyer, const Coord3D &pos, float &worst, Coord3D &push)
{
	Object &obj = *flyer.object;
	GameLogic &logic = obj.logic();
	static const int kInfantry = ObjectTemplateInfoBuilder::kindOfIndex("INFANTRY");
	static const int kCavalry = ObjectTemplateInfoBuilder::kindOfIndex("CAVALRY");
	static const int kHorde = ObjectTemplateInfoBuilder::kindOfIndex("HORDE");
	const float radius = CombatQueries::boundingSphereRadius(obj);
	// PartitionFilterWouldCollide(pos, the flyer's geometry, its angle, true): the 2D footprints (S-4041: RW 0xAD2CE0 is not ported)
	std::vector<BuildPlacement::Footprint> mine;
	BuildPlacement::footprintsOf(logic, *obj.getTemplate(), pos, obj.getOrientation(), mine);
	auto wouldCollide = [&](Object &other) {
		std::vector<BuildPlacement::Footprint> theirs;
		if (!BuildPlacement::footprintsOf(logic, *other.getTemplate(), *other.getPosition(), other.getOrientation(), theirs))
		{
			return false;
		}
		for (const BuildPlacement::Footprint &a : mine)
		{
			for (const BuildPlacement::Footprint &b : theirs)
			{
				if (BuildPlacement::footprintsCollide(a, b))
				{
					return true;
				}
			}
		}
		return false;
	};
	PartitionFilterFn<decltype(wouldCollide)> filter(wouldCollide);
	const PartitionHits hits = logic.partition().iterateObjectsInRange(pos, radius, FROM_BOUNDINGSPHERE_3D, { &filter }, ITER_FASTEST);
	bool clear = true;
	for (const PartitionHit &hit : hits)
	{
		Object &them = *hit.object;
		if (&them == &obj)
		{
			continue;
		}
		if (flyer.partner != INVALID_ID && flyer.partner == them.getID())
		{
			continue;
		}
		if ((kInfantry >= 0 && them.isKindOf((unsigned)kInfantry)) || (kCavalry >= 0 && them.isKindOf((unsigned)kCavalry)) || (kHorde >= 0 && them.isKindOf((unsigned)kHorde)))
		{
			continue;
		}
		if (isMobileForSeparation(them))
		{
			const float range = mulf32(radius, 1.2f);
			const Coord3D &tp = *them.getPosition();
			const Coord3D &op = *obj.getPosition();
			// RW 0x5E3BE4: the x87 squared 3D distance
			const double d2 = SimMath::pc24AddW(SimMath::pc24AddW(SimMath::pc24MulW(subf32(tp.x, op.x), subf32(tp.x, op.x)), SimMath::pc24MulW(subf32(tp.y, op.y), subf32(tp.y, op.y))),
				SimMath::pc24MulW(subf32(tp.z, op.z), subf32(tp.z, op.z)));
			if (d2 > (double)mulf32(range, range))
			{
				continue;
			}
		}
		Coord3D delta{ subf32(pos.x, them.getPosition()->x), subf32(pos.y, them.getPosition()->y), subf32(pos.z, them.getPosition()->z) }; // RW 0x5E3C12
		const float overlap = subf32(addf32(mulf32(CombatQueries::boundingSphereRadius(them), 2.0f), radius), length3(delta));
		if (worst < overlap)
		{
			worst = overlap;
		}
		normalize3(delta);
		push.x = addf32(push.x, mulf32(delta.x, overlap));
		push.y = addf32(push.y, mulf32(delta.y, overlap));
		push.z = addf32(push.z, mulf32(delta.z, overlap));
		clear = false;
	}
	return clear;
}

int probe(const Flyer &flyer, const Coord3D &pos, Coord3D &push)
{
	if (!flyer.hasAI)
	{
		return 2;
	}
	const GameLogic &logic = flyer.object->logic();
	push = Coord3D{ 0.0f, 0.0f, 0.0f };
	const float height = flyer.preferredHeight;
	const float radius = CombatQueries::boundingSphereRadius(*flyer.object);
	float worst = 0.0f;
	Coord3D s{ pos.x, pos.y, subf32(pos.z, mulf32(height, 0.5f)) };
	float g = flightGroundHeight(logic, s.x, s.y);
	if (subf32(g, s.z) > 0.0f)
	{
		worst = subf32(g, s.z);
	}
	s.z = subf32(pos.z, mulf32(mulf32(height, 0.5f), 0.777f));
	const float cx[4] = { subf32(pos.x, radius), subf32(pos.x, radius), addf32(pos.x, radius), addf32(pos.x, radius) };
	const float cy[4] = { subf32(pos.y, radius), addf32(pos.y, radius), addf32(pos.y, radius), subf32(pos.y, radius) };
	for (int i = 0; i < 4; ++i)
	{
		g = flightGroundHeight(logic, cx[i], cy[i]);
		if (subf32(g, s.z) > worst)
		{
			worst = subf32(g, s.z);
		}
	}
	push.z = addf32(push.z, worst);
	separation(flyer, pos, worst, push);
	return worst > 0.0f ? 1 : 0;
}

bool pathClear(const Flyer &flyer, AerialSpline &spline, float distance, float clearance)
{
	if (!flyer.hasAI || !spline.valid())
	{
		return true;
	}
	Coord3D p;
	if (!query(spline, distance, p))
	{
		return true;
	}
	p.z = subf32(p.z, mulf32(clearance, 0.5f)); // RW 0xC2ECBC
	const bool above = p.z > flyer.object->logic().getGroundHeight(p.x, p.y);
	float worst = 0.0f;
	Coord3D push{ 0.0f, 0.0f, 0.0f };
	const bool clear = separation(flyer, p, worst, push);
	return above && clear;
}

// RW 0x778413. The control points:
//   1. behind: the flyer's position minus its facing (the transform's x axis, RW 0x70BA38) times min(3 * distance to the goal, 1500); with mask bit 2 and the
//      flyer significantly above the terrain (RW 0x70C55D) it is lowered by that much, else with mask bit 7 it is the flyer's x, y at z 1000;
//   2. the flyer's position; 3. with a look-ahead point: the goal, the look-ahead becoming the goal;
//   4. mask bit 0: the flyer's position again and the goal (no terminal point); else the goal and a terminal point beyond it at the clamped radius
//      (distance * 0.25 in [50, 150], truncated, x87): when the probe finds that point blocked, the first free point on the circle around the goal
//      (angles base +- k * 0.2, then +- 0.4 more, base = atan2 of the direction) is used, else the original one; mask bit 1 lowers it by min(3 * distance,
//      1500); then prepare and build;
//   5. walking the path in steps of half the bounding sphere radius (at most 200 steps, five corrections): where a blocked run ends, its middle point
//      pushed out by the probe's push is inserted after the segment (unless an interior control point is within the bounding sphere radius of it), the end
//      points are fixed, the spline is rebuilt and the walk resumes one radius further.
bool computeAerialPath(const Flyer &flyer, const Coord3D &goalIn, AerialSpline &spline, unsigned mask, const Coord3D *lookAhead, bool snap)
{
	Object &obj = *flyer.object;
	GameLogic &logic = obj.logic();
	if (!flyer.hasAI)
	{
		return false;
	}
	Coord3D goal = goalIn;
	if (snap)
	{
		// RW 0x778477 .. 0x7784D8: the flyer itself (found by its id) has the goal adjusted by the pathfinder (RW 0x6FE456 with the AI's surfaces, no group
		// goal) and reserved (RW 0x68B3BD); the goal's z is kept
		Object *self = logic.findObjectByID(obj.getID());
		if (!self)
		{
			return false;
		}
		AIWorld *world = logic.aiWorld();
		const float z = goal.z;
		if (world && world->mapReady())
		{
			PathfindLocomotorInfo info;
			info.validSurfaces = flyer.surfaces;
			world->pathfinder().adjustDestination(world->adapterFor(*self), info, &goal, nullptr);
			world->pathfinder().updateGoal(world->adapterFor(*self), &goal, LAYER_GROUND);
		}
		goal.z = z;
	}
	const Coord3D start = *obj.getPosition();
	Coord3D delta{ subf32(goal.x, start.x), subf32(goal.y, start.y), subf32(goal.z, start.z) }; // RW 0x6635F4
	const float dist = length3(delta);
	const float *basis = obj.getBasis();
	const Coord3D facing{ basis[0], basis[3], basis[6] }; // RW 0x70BA38: the transform's x axis
	const float far3 = mulf32(dist, 3.0f);
	const float back = far3 > 1500.0f ? 1500.0f : far3; // RW 0xC2ECB4
	const float neg = subf32(0.0f, back);
	Coord3D behind{ addf32(mulf32(neg, facing.x), start.x), addf32(mulf32(neg, facing.y), start.y), addf32(start.z, mulf32(neg, facing.z)) };
	// RW 0x70C55D -> RW 0x70C50C(0.0): not for the KindOf CAN_CLIMB_WALLS / SHIP (template + 0x11F & 0x81), the height above the terrain (RW 0x70B8AE) + 0 over
	// GlobalData Gravity (+ 0xC4) * -9 (RW 0xC1EBC0), x87 (the same predicate EmotionSystem ports)
	static const int kClimb = ObjectTemplateInfoBuilder::kindOfIndex("CAN_CLIMB_WALLS");
	static const int kShip = ObjectTemplateInfoBuilder::kindOfIndex("SHIP");
	bool significantlyAbove = false;
	if (!((kClimb >= 0 && obj.isKindOf((unsigned)kClimb)) || (kShip >= 0 && obj.isKindOf((unsigned)kShip))))
	{
		const float above = SimMath::pc24Add(SimMath::pc24Sub(start.z, logic.getGroundHeight(start.x, start.y)), 0.0f);
		significantlyAbove = above > SimMath::pc24Mul(logic.settings().gravity, -9.0f);
	}
	if ((mask & 4u) != 0 && significantlyAbove)
	{
		behind.z = subf32(behind.z, back);
	}
	else if ((mask & 0x80u) != 0)
	{
		behind = Coord3D{ start.x, start.y, 1000.0f }; // RW 0xBD4388
	}
	spline.clear();
	spline.append(behind);
	spline.append(start);
	if (lookAhead)
	{
		spline.append(goal);
		goal = *lookAhead;
	}
	if ((mask & 1u) != 0)
	{
		spline.append(start);
		spline.append(goal);
	}
	else
	{
		// "I calc terminal points": the radius, x87: dist * 0.25 (double), clamped to [50, 150] (RW 0xC2ED20 / 0xBED870), _ftol2
		double r = SimMath::pc24MulW((double)dist, 0.25); // RW 0xBE4400
		if (r > 150.0)
		{
			r = 150.0;
		}
		else if (50.0 > r)
		{
			r = 50.0;
		}
		const float radius = SimMath::sseFromInt32((std::int32_t)SimMath::ftol2Low32(r));
		Coord3D dir = delta;
		normalize3(dir);
		Coord3D terminal{ addf32(mulf32(dir.x, radius), goal.x), addf32(mulf32(dir.y, radius), goal.y), addf32(mulf32(radius, dir.z), goal.z) };
		Coord3D push;
		if (probe(flyer, terminal, push) != 0)
		{
			// RW 0x778759 .. 0x77891E: base = RW 0x441BF4(dx, dy) (atan2f with the x component as its first argument: sin(base) = dx / |d|), then the circle
			// of the radius around the goal at the goal's z, both ways in steps of 0.2 below pi (RW 0xBDAD78, 0xBDD388). A free point (probe 0) is turned 0.4
			// further (RW 0xBE0DE4) the same way; the first becomes the terminal point, the second is appended before the goal and the search ends. x87: CRT
			// sin / cos (RW 0xA3CF90 / 0xA3CF84), fmul the radius, fadd the goal, fstp
			const float base = SimMath::fstpDword(SimMath::atan2d(delta.x, delta.y));
			bool tryPlus = true, tryMinus = true, found = false;
			Coord3D pt{ 0.0f, 0.0f, goal.z };
			auto circle = [&](float a) {
				pt.x = SimMath::fstpDword(SimMath::pc24AddW(SimMath::pc24MulW(SimMath::sind(a), (double)radius), (double)goal.x));
				pt.y = SimMath::fstpDword(SimMath::pc24AddW(SimMath::pc24MulW(SimMath::cosd(a), (double)radius), (double)goal.y));
			};
			float k = 0.0f;
			do
			{
				bool done = false;
				for (int side = 0; side < 2 && !done; ++side)
				{
					bool &tryIt = side == 0 ? tryPlus : tryMinus;
					if (!tryIt)
					{
						continue;
					}
					const float a = normalizeAngle(side == 0 ? addf32(k, base) : subf32(base, k));
					circle(a);
					pt.z = goal.z;
					if (probe(flyer, pt, push) != 0)
					{
						continue;
					}
					const float b = SimMath::fstpDword(side == 0 ? SimMath::pc24AddW((double)a, (double)0.4f) : SimMath::pc24SubW((double)a, (double)0.4f));
					circle(b);
					tryIt = false;
					if (found)
					{
						spline.append(pt); // RW 0x778920
						done = true;
						break;
					}
					terminal = pt;
					found = true;
				}
				if (done)
				{
					break;
				}
				k = addf32(k, 0.2f);
			} while (3.14159274f > k);
		}
		if ((mask & 2u) != 0)
		{
			terminal.z = subf32(terminal.z, back);
		}
		spline.append(goal);
		spline.append(terminal);
	}
	// "Collision test for terrain and objects"
	spline.prepare();
	spline.build();
	const float sphere = CombatQueries::boundingSphereRadius(obj);
	int corrections = 0, steps = 0;
	bool inRun = false;
	float runStart = 0.0f;
	Coord3D push{ 0.0f, 0.0f, 0.0f };
	spline.setTime(0.0f);
	do
	{
		spline.setTime(addf32(mulf32(sphere, 0.5f), spline.time())); // RW 0xC2ECBC
		if (!spline.evaluate() || ++steps > 200)
		{
			break;
		}
		const bool blocked = probe(flyer, spline.result(), push) != 0;
		if (blocked && !inRun)
		{
			inRun = true;
			runStart = spline.time();
		}
		if (inRun && !blocked)
		{
			const float mid = mulf32(addf32(spline.time(), runStart), 0.5f);
			inRun = false;
			spline.setTime(mid);
			if (spline.evaluate())
			{
				Coord3D p = spline.result();
				if (probe(flyer, p, push) != 0)
				{
					p.x = addf32(p.x, push.x);
					p.y = addf32(p.y, push.y);
					p.z = addf32(p.z, push.z);
					if (!spline.nearInteriorPoint(p, sphere))
					{
						spline.insertAfterSegment(p);
					}
					spline.fixEndpoints();
					spline.prepare();
					spline.build();
					spline.setTime(addf32(sphere, mid));
					++corrections;
				}
			}
		}
	} while (corrections < 5);
	return true;
}

std::vector<std::string> stopLines()
{
	return { "S-4041: the aerial path (RW 0x778413) is ported without the no-fly zones (the map block AerialPathfindNoFlyZone is a recording stub: RW 0x777E54 "
		     "answers 0), AIData MinFlightHeight (not read: the retail default FLT_MAX), the 3D shape overlap of the separation's would-collide filter (2D footprints: "
		     "RW 0xAD2CE0 is not ported, S-1020) and the x87 / CRT parity of its terminal search (S-081)" };
}
} // namespace AerialPathfinder
