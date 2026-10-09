// OpenBFME. GPL-3.0.
// See GameLogic/Module/NotifyCrushModules.h for the sources and what is inference.

#include "GameLogic/Module/NotifyCrushModules.h"

#include "Common/GameCommon.h"
#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/AI/AIPathfindHost.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/EmotionModules.h"
#include "GameLogic/Module/SquishCollide.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/System/EmotionSystem.h"
#include "GameLogic/Weapon.h"

#include <algorithm>
#include <cstddef>
#include <stdexcept>

namespace
{
const char *const kMuxFile = "NotifyTargetsOfImminentProbableCrushingMux.cpp"; // RW 0xC76CE8 (the path ends with this name)

const char *const kStop =
	"[S-1029] crush warning: NotifyTargetsOfImminentProbableCrushingUpdate / HordeNotifyTargetsOfImminentProbableCrushingUpdate (RW 0x8D3225 / 0x8D3167 -> mux RW "
	"0x8D2B21: the draw, the velocity box, the region query with crushPolicy, the shape overlap RW 0xAD2CE0 and alive, BRACE / CHEER requests) are ported; "
	"INFERENCE: crushPolicy is PHYS-1's reading of RW 0x69519A (its line test RW 0x6F5BB0 passes), the x87 sin / cos are SimMath::sinCosDet, the horde's member map "
	"+ 0x170 is not ported";

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif
#define NC_OFF(field) (int)offsetof(NotifyCrushModuleData, field)
// RW 0xC76C48
const FieldParse kNotifyCrushParse[] = {
	{ "TimeBetweenUpdatesMS", INI::parseDurationUnsignedInt, nullptr, NC_OFF(m_timeBetweenUpdates) },
	{ "ScanAheadTimeMS", INI::parseDurationUnsignedInt, nullptr, NC_OFF(m_scanAheadTime) },
	{ "ScanHeight", INI::parseReal, nullptr, NC_OFF(m_scanHeight) },
	{ "ScanWidth", INI::parseReal, nullptr, NC_OFF(m_scanWidth) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef NC_OFF
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

float absF(float a)
{
	return a < 0.0f ? SimMath::subf32(0.0f, a) : a;
}

// RW 0x403720 Coord3D::GetLengthEstimate2D: |the larger| + 0.25 * |the smaller| (x87)
float lengthEstimate2D(float x, float y)
{
	const float ax = absF(x), ay = absF(y);
	const float big = ax <= ay ? ay : ax, small = ax <= ay ? ax : ay;
	return SimMath::pc24Add(SimMath::pc24Mul(small, 0.25f), big);
}

// RW 0x69519A(other, 2): PHYS-1's reading (HordeAIUpdateCombat.cpp crushPolicy)
bool crushPolicy(const Object &self, const Object &victim)
{
	if (!(ObjectCrush::crusherLevel(self) > 0 && ObjectCrush::crusherLevel(self) > ObjectCrush::crushableLevel(victim)))
	{
		return false;
	}
	if (self.testStatus((unsigned)CombatNames::status("RAMPAGING")) || self.testStatus((unsigned)CombatNames::status("FLEE_OFF_MAP")) ||
	    self.testModelCondition(CombatNames::modelCondition("CHARGING")))
	{
		return true;
	}
	if (self.getRelationship(victim) != ENEMIES)
	{
		return false;
	}
	ObjectWeapons *w = const_cast<Object &>(self).getWeapons();
	Weapon *weapon = w ? w->currentWeapon() : nullptr;
	return !weapon || (weapon->getTemplate() && weapon->getTemplate()->m_meleeWeapon);
}

// RW 0x68EF58: the object's velocity per frame
Coord3D velocityOf(Object &obj)
{
	Coord3D v{ 0.0f, 0.0f, 0.0f };
	const Coord3D &p = *obj.getPosition();
	const UnsignedInt now = obj.logic().getFrame();
	Coord3D pending;
	AIUpdateInterface *ai = obj.getAIUpdateInterface();
	if (ai && ai->pendingPositionOfFrame(now, pending)) // + 0x1A6 (RW 0x6260E1 clears it every recorded frame: SMOOTH-1's frame-matched pending position)
	{
		v.x = SimMath::subf32(pending.x, p.x);
		v.y = SimMath::subf32(pending.y, p.y);
		v.z = SimMath::subf32(pending.z, p.z);
		return v;
	}
	const UnsignedInt rec = obj.getRecordedFrame();
	if (obj.hasRecordedTransform() && rec < now)
	{
		const Coord3D &r = obj.getRecordedPosition();
		const float inv = SimMath::divf32(1.0f, (float)(now - rec)); // fild (unsigned fix-up for a negative), fdivr 1.0
		v.x = SimMath::mulf32(inv, SimMath::addf32(p.x, SimMath::mulf32(r.x, -1.0f)));
		v.y = SimMath::mulf32(SimMath::addf32(p.y, SimMath::mulf32(r.y, -1.0f)), inv);
		v.z = SimMath::mulf32(SimMath::addf32(p.z, SimMath::mulf32(r.z, -1.0f)), inv);
		return v;
	}
	if (obj.hasRecordedTransform() && rec <= now) // + 0x1A5: the previous position exists once a transform was recorded
	{
		const Coord3D &q = obj.getPreviousPosition();
		const float inv = SimMath::divf32(1.0f, (float)(now - rec + 1u));
		v.x = SimMath::mulf32(inv, SimMath::subf32(p.x, q.x));
		v.y = SimMath::mulf32(SimMath::subf32(p.y, q.y), inv);
		v.z = SimMath::mulf32(SimMath::subf32(p.z, q.z), inv);
	}
	return v;
}

struct WorldShape
{
	int type = 0;
	float height = 0.0f, major = 0.0f, minor = 0.0f, offsetZ = 0.0f;
	float x = 0.0f, y = 0.0f, z = 0.0f;
	float angle = 0.0f;
};

// RW 0xAD22F0
WorldShape worldShape(const ObjectGeometry::Shape &s, const Coord3D &pos, float angle)
{
	double sn = 0.0, cs = 0.0;
	SimMath::sinCosDet((double)angle, sn, cs);
	WorldShape w;
	w.type = s.type;
	w.height = s.height;
	w.major = s.majorRadius;
	w.minor = s.minorRadius;
	w.offsetZ = s.offsetZ;
	w.angle = angle;
	w.x = SimMath::pc24Add(pos.x, SimMath::pc24Sub(SimMath::pc24MulD((double)s.offsetX, cs), SimMath::pc24MulD((double)s.offsetY, sn)));
	w.y = SimMath::pc24Add(SimMath::pc24Add(SimMath::pc24MulD((double)s.offsetX, sn), SimMath::pc24MulD((double)s.offsetY, cs)), pos.y);
	w.z = SimMath::pc24Add(pos.z, s.offsetZ);
	return w;
}

// the box record of RW 0xAD4870 / 0xAD4790: centre, axis u = (cos, sin), axis v = (-sin, cos) stored as floats, half extents
struct BoxRecord
{
	float x, y, ux, uy, vx, vy, e1, e2;
};

BoxRecord boxOf(const WorldShape &b)
{
	double sn = 0.0, cs = 0.0;
	SimMath::sinCosDet((double)b.angle, sn, cs);
	const float c = (float)cs, s = (float)sn;
	return BoxRecord{ b.x, b.y, c, s, SimMath::subf32(0.0f, s), c, b.major, b.minor };
}

// RW 0xAD45A0
bool boxCircle(const BoxRecord &b, float cx, float cy, float r)
{
	const float dx = SimMath::pc24Sub(cx, b.x), dy = SimMath::pc24Sub(cy, b.y);
	float du = SimMath::pc24Add(SimMath::pc24Mul(dx, b.ux), SimMath::pc24Mul(dy, b.uy));
	float dv = SimMath::pc24Add(SimMath::pc24Mul(dx, b.vx), SimMath::pc24Mul(dy, b.vy));
	float d = 0.0f;
	const float ne1 = SimMath::subf32(0.0f, b.e1), ne2 = SimMath::subf32(0.0f, b.e2);
	if (ne1 <= du)
	{
		if (b.e1 < du)
		{
			du = SimMath::pc24Sub(du, b.e1);
			d = SimMath::pc24Mul(du, du);
		}
	}
	else
	{
		du = SimMath::pc24Add(du, b.e1);
		d = SimMath::pc24Mul(du, du);
	}
	bool inside = false;
	if (ne2 <= dv)
	{
		if (dv <= b.e2)
		{
			inside = true;
		}
		else
		{
			dv = SimMath::pc24Sub(dv, b.e2);
		}
	}
	else
	{
		dv = SimMath::pc24Add(dv, b.e2);
	}
	if (!inside)
	{
		d = SimMath::pc24Add(SimMath::pc24Mul(dv, dv), d);
	}
	const float r2 = SimMath::pc24Mul(r, r);
	return !(d > r2);
}

// RW 0xAD4660
bool boxBox(const BoxRecord &a, const BoxRecord &b)
{
	const float dx = SimMath::pc24Sub(b.x, a.x), dy = SimMath::pc24Sub(b.y, a.y);
	auto dot = [](float p, float q, float r, float s) { return SimMath::pc24Add(SimMath::pc24Mul(p, q), SimMath::pc24Mul(r, s)); };
	const float f3 = absF(dot(b.ux, a.ux, b.uy, a.uy));
	const float f4 = absF(dot(b.vx, a.ux, b.vy, a.uy));
	if (!(SimMath::pc24Add(SimMath::pc24Add(SimMath::pc24Mul(f3, b.e1), SimMath::pc24Mul(f4, b.e2)), a.e1) >= absF(dot(dx, a.ux, dy, a.uy))))
	{
		return false;
	}
	const float f5 = absF(dot(b.ux, a.vx, b.uy, a.vy));
	const float f8 = absF(dot(b.vx, a.vx, b.vy, a.vy));
	if (!(SimMath::pc24Add(SimMath::pc24Add(SimMath::pc24Mul(f5, b.e1), SimMath::pc24Mul(f8, b.e2)), a.e2) >= absF(dot(dx, a.vx, dy, a.vy))))
	{
		return false;
	}
	if (!(SimMath::pc24Add(SimMath::pc24Add(SimMath::pc24Mul(f5, a.e2), SimMath::pc24Mul(f3, a.e1)), b.e1) >= absF(dot(dx, b.ux, dy, b.uy))))
	{
		return false;
	}
	return SimMath::pc24Add(SimMath::pc24Add(SimMath::pc24Mul(f8, a.e2), SimMath::pc24Mul(f4, a.e1)), b.e2) >= absF(dot(dx, b.vx, dy, b.vy));
}

float shapeHeight(const WorldShape &s)
{
	if (s.type == ObjectGeometry::SHAPE_SPHERE)
	{
		return s.major;
	}
	return (s.type == ObjectGeometry::SHAPE_CYLINDER || s.type == ObjectGeometry::SHAPE_BOX) ? s.height : 0.0f;
}
} // namespace

bool NotifyCrushModules::geometriesOverlap(const std::vector<ObjectGeometry::Shape> &a, const Coord3D &posA, float angleA, const std::vector<ObjectGeometry::Shape> &b,
                                           const Coord3D &posB, float angleB)
{
	// RW 0xAD2CE0
	for (const ObjectGeometry::Shape &sa : a)
	{
		if (!sa.active)
		{
			continue;
		}
		const WorldShape wa = worldShape(sa, posA, angleA);
		const float ha = shapeHeight(wa);
		for (const ObjectGeometry::Shape &sb : b)
		{
			if (!sb.active)
			{
				continue;
			}
			const WorldShape wb = worldShape(sb, posB, angleB);
			const float hb = shapeHeight(wb);
			const float lowB = wb.type == ObjectGeometry::SHAPE_SPHERE ? SimMath::pc24Add(hb, sb.offsetZ) : 0.0f;
			if (!(SimMath::pc24Sub(posB.z, lowB) <= SimMath::pc24Add(SimMath::pc24Add(wa.z, ha), sa.offsetZ)))
			{
				continue;
			}
			if (!(wa.z <= SimMath::pc24Add(SimMath::pc24Add(wb.z, hb), sb.offsetZ)))
			{
				continue;
			}
			bool hit = false;
			if (wa.type == ObjectGeometry::SHAPE_SPHERE || wa.type == ObjectGeometry::SHAPE_CYLINDER)
			{
				if (wb.type == ObjectGeometry::SHAPE_BOX)
				{
					hit = boxCircle(boxOf(wb), wa.x, wa.y, wa.major); // RW 0xAD4870(a, b)
				}
				else
				{
					// RW 0xAD48F0
					const float dx = SimMath::pc24Sub(wb.x, wa.x), dy = SimMath::pc24Sub(wb.y, wa.y);
					const float r = SimMath::pc24Add(wa.major, wb.major);
					hit = !(SimMath::pc24Add(SimMath::pc24Mul(dx, dx), SimMath::pc24Mul(dy, dy)) > SimMath::pc24Mul(r, r));
				}
			}
			else if (wa.type == ObjectGeometry::SHAPE_BOX)
			{
				hit = wb.type == ObjectGeometry::SHAPE_BOX ? boxBox(boxOf(wa), boxOf(wb)) : boxCircle(boxOf(wa), wb.x, wb.y, wb.major);
			}
			if (hit)
			{
				return true;
			}
		}
	}
	return false;
}

// ---- the modules ------------------------------------------------------------------------------------------------------------------------------------------------

void NotifyCrushModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kNotifyCrushParse);
}

NotifyCrushUpdate::NotifyCrushUpdate(Thing *thing, const NotifyCrushModuleData *data, bool horde)
	: UpdateModule(thing, data)
	, m_data(data)
	, m_horde(horde)
{
}

std::vector<std::string> NotifyCrushUpdate::stopLines()
{
	return { kStop };
}

bool NotifyCrushUpdate::mux(Object &obj, unsigned &sleep)
{
	// RW 0x8D2B21
	GameLogic &logic = obj.logic();
	const unsigned delay = m_data->m_timeBetweenUpdates;
	sleep = (unsigned)logic.random().getValue((int)delay, (int)((delay * 3u) >> 1), kMuxFile, 0x3F);
	AIUpdateInterface *ai = obj.getAIUpdateInterface();
	Locomotor *loco = ai ? ai->curLocomotor() : nullptr; // RW 0x68B31D
	if (!loco || loco->speed() == 0.0f)                  // RW 0x5E36F7: fucomip with 0 (a NaN goes on)
	{
		return false;
	}
	if (!(ObjectCrush::crusherLevel(obj) > 0) || !ObjectCrush::canCrush(obj))
	{
		return false;
	}
	const unsigned ahead = m_data->m_scanAheadTime;
	if (ahead == 0)
	{
		return false;
	}
	logic.noteStop(kStop);
	Coord3D v = velocityOf(obj);
	const float k = (float)(int)ahead; // cvtsi2ss
	v.x = SimMath::mulf32(k, v.x);
	v.y = SimMath::mulf32(k, v.y);
	v.z = SimMath::mulf32(v.z, k);
	if (1.0f > lengthEstimate2D(v.x, v.y))
	{
		return false;
	}
	const Coord3D p = *obj.getPosition();
	Coord3D end;
	end.x = SimMath::addf32(p.x, v.x);
	end.y = SimMath::addf32(p.y, v.y);
	end.z = SimMath::addf32(v.z, p.z);
	Coord3D mid;
	mid.x = SimMath::mulf32(SimMath::addf32(end.x, p.x), 0.5f);
	mid.y = SimMath::mulf32(SimMath::addf32(end.y, p.y), 0.5f);
	mid.z = SimMath::mulf32(SimMath::addf32(end.z, p.z), 0.5f);
	const float angle = (float)SimMath::atan2d(v.y, v.x); // RW 0x441BD2; fstp dword
	const ThingTemplate *tt = static_cast<const ThingTemplate *>(obj.getTemplate())->getFinalOverride();
	const std::vector<ObjectGeometry::Shape> shapes = ObjectGeometry::shapesOf(*tt);
	float height = m_data->m_scanHeight;
	if (!(0.0f < height))
	{
		height = SimMath::pc24Mul(ObjectGeometry::maxHeightAbovePosition(shapes), 3.0f); // RW 0xAD1920 * 3.0 (RW 0xBDD42C)
	}
	mid.z = SimMath::subf32(mid.z, SimMath::mulf32(height, 0.333333343f)); // RW 0xBDBC78
	// RW 0xAD2040: the local box of the active shapes, from 0 .. 0
	float lo[3] = { 0.0f, 0.0f, 0.0f }, hi[3] = { 0.0f, 0.0f, 0.0f };
	for (const ObjectGeometry::Shape &s : shapes)
	{
		if (!s.active)
		{
			continue;
		}
		const float yr = s.type == ObjectGeometry::SHAPE_BOX ? s.minorRadius : s.majorRadius;
		lo[0] = std::min(lo[0], SimMath::subf32(s.offsetX, s.majorRadius));
		lo[1] = std::min(lo[1], SimMath::subf32(s.offsetY, yr));
		hi[0] = std::max(hi[0], SimMath::addf32(s.majorRadius, s.offsetX));
		hi[1] = std::max(hi[1], SimMath::addf32(s.offsetY, yr));
		if (s.type == ObjectGeometry::SHAPE_SPHERE)
		{
			lo[2] = std::min(lo[2], SimMath::subf32(s.offsetZ, s.majorRadius));
			hi[2] = std::max(hi[2], SimMath::addf32(s.offsetZ, s.majorRadius));
		}
		else
		{
			lo[2] = std::min(lo[2], s.offsetZ);
			hi[2] = std::max(hi[2], SimMath::addf32(s.height, s.offsetZ));
		}
	}
	float width = m_data->m_scanWidth;
	if (!(0.0f < width))
	{
		width = SimMath::subf32(hi[2], lo[2]); // RW 0x8D2CEB: hi.z - lo.z (as written)
	}
	const float depth = SimMath::pc24Sub(hi[0], lo[0]);
	ObjectGeometry::Shape box;
	box.type = ObjectGeometry::SHAPE_BOX;
	box.height = height;
	box.majorRadius = SimMath::pc24Mul(SimMath::pc24Add(lengthEstimate2D(v.x, v.y), depth), 0.5f);
	box.minorRadius = SimMath::mulf32(width, 0.5f);
	const std::vector<ObjectGeometry::Shape> boxShapes = { box };
	// RW 0xAD1D60: the box's 2D bounds at the midpoint, turned
	PathfindGeometry g;
	g.type = PATHFIND_GEOMETRY_BOX;
	g.majorRadius = box.majorRadius;
	g.minorRadius = box.minorRadius;
	g.height = box.height;
	float b[4];
	g.boundingBox2D(mid, angle, b);
	PartitionRegion region;
	region.loX = b[0];
	region.loY = b[1];
	region.hiX = b[2];
	region.hiY = b[3];
	PartitionFilterFn policy([&obj](Object &o) { return crushPolicy(obj, o); }); // RW 0xC76CD8
	PartitionFilterFn overlap([&](Object &o) {                                  // RW 0xC112C8
		const ThingTemplate *ot = static_cast<const ThingTemplate *>(o.getTemplate())->getFinalOverride();
		return NotifyCrushModules::geometriesOverlap(boxShapes, mid, angle, ObjectGeometry::shapesOf(*ot), *o.getPosition(), o.getOrientation());
	});
	PartitionFilterFn alive([](Object &o) { return !o.isEffectivelyDead(); }); // RW 0xC10E20
	const PartitionHits hits = logic.partition().iterateObjectsInRegion(region, { &policy, &overlap, &alive }, ITER_FASTEST);
	++m_scans;
	std::vector<Object *> victims;
	for (const PartitionHit &h : hits)
	{
		victims.push_back(h.object);
	}
	if (!victims.empty())
	{
		EmotionTrackerUpdate::requestEmotion(obj, EMOTION_CHEER_FOR_ABOUT_TO_CRUSH, nullptr, 1); // RW 0x68F37F(0xB, 0, 1)
	}
	for (Object *victim : victims)
	{
		EmotionTrackerUpdate::requestEmotion(*victim, EMOTION_BRACE_FOR_BEING_CRUSHED, &obj, 1); // RW 0x69035C
		++m_warned;
	}
	return true;
}

UpdateSleepTime NotifyCrushUpdate::update()
{
	Object *obj = getObject();
	unsigned sleep = m_data->m_timeBetweenUpdates;
	if (!m_horde)
	{
		mux(*obj, sleep); // RW 0x8D3225
	}
	else
	{
		// RW 0x8D3167
		ContainModuleInterface *c = obj->getContain();
		if (!c || !c->getHordeContainInterface())
		{
			return UPDATE_SLEEP_FOREVER;
		}
		std::vector<Object *> members;
		if (const ContainModuleInterface::ContainedItemsList *l = c->getContainedItemsList())
		{
			members.assign(l->begin(), l->end());
		}
		const Coord3D hp = *obj->getPosition();
		std::stable_sort(members.begin(), members.end(), [&hp](const Object *a, const Object *b) {
			// RW 0x8D2EC6
			const float ay = SimMath::pc24Sub(hp.y, a->getPosition()->y), ax = SimMath::pc24Sub(hp.x, a->getPosition()->x);
			const float bx = SimMath::pc24Sub(hp.x, b->getPosition()->x), by = SimMath::pc24Sub(hp.y, b->getPosition()->y);
			return SimMath::pc24Add(SimMath::pc24Mul(ay, ay), SimMath::pc24Mul(ax, ax)) < SimMath::pc24Add(SimMath::pc24Mul(by, by), SimMath::pc24Mul(bx, bx));
		});
		for (Object *m : members)
		{
			if (m && mux(*m, sleep))
			{
				break;
			}
		}
	}
	if (sleep < 1u)
	{
		return UPDATE_SLEEP_NONE;
	}
	return sleep >= (unsigned)UPDATE_SLEEP_FOREVER ? UPDATE_SLEEP_FOREVER : UPDATE_SLEEP((int)sleep);
}

void NotifyCrushUpdate::crc(StateHasher &h) const
{
	UpdateModule::crc(h);
	h.addBool(m_horde);
	h.addU32(m_scans);
	h.addU32(m_warned);
}

void NotifyCrushModules::registerAll(ModuleFactory &modules)
{
	for (const char *name : { "NotifyTargetsOfImminentProbableCrushingUpdate", "HordeNotifyTargetsOfImminentProbableCrushingUpdate" })
	{
		const bool horde = name[0] == 'H';
		modules.bindTypedData<NotifyCrushModuleData>(name, MODULETYPE_BEHAVIOR);
		modules.bindModuleProc(name, MODULETYPE_BEHAVIOR, [name, horde](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
			const NotifyCrushModuleData *typed = dynamic_cast<const NotifyCrushModuleData *>(data);
			if (!typed)
			{
				throw std::logic_error(std::string(name) + ": the module data is not typed");
			}
			return std::make_unique<NotifyCrushUpdate>(thing, typed, horde);
		});
	}
}
