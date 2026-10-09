// OpenBFME. GPL-3.0.
// See GameLogic/Module/PhysicsBehavior.h for the target facts, the addresses and what is not ported (S-782).

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/PhysicsBehavior.h"

#include "Common/GameCommon.h"
#include "Common/INI.h"
#include "Common/RandomValue.h"
#include "Common/Thing/ThingTemplate.h"
#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/Combat/BezierSegment.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/FXEvents.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/ProjectileModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/SimMath.h"

#include <cstddef>
#include <stdexcept>

namespace
{
const char *const kStop =
	"[S-782] physics: PhysicsBehavior's fling flight, the shock stun (RW 0x792A69 / 0x792AFF, ShockStunnedTime*, ShockStandingTime), the end of the flight (RW 0x793372: "
	"GroundHitFX, SPLATTED, STUNNED), the rest (RW 0x79308D, KillWhenRestingOnGround), the bounce (RW 0x793224) and the fling's AI halt are ported (RW 0x792DBD, 0x792997, "
	"0x79261A, 0x79350E); NOT ported: OrientToFlightPath / TumbleRandomly (the angle is not set), the pathfinder's landing clip (RW 0x6EF8A8) and per step update "
	"(RW 0x6F0741), the AI's second machine (RW 0x6630D7), the status 70 call (RW 0x693919), the fling's extra words (RW 0x6CF590), the drawable's refresh and projectile "
	"type (client), the callers of the fling other than the crush knockback (shockwaves S-1600, slow death, weapons: their lanes)";

const char *const kPhysicsUpdateCpp = "PhysicsUpdate.cpp"; // RW 0xC306F8

struct PhysNames
{
	int flailing = CombatNames::modelCondition("STUNNED_FLAILING");      // 127: obj + 0x10C + 0xF bit 7
	int stunned = CombatNames::modelCondition("STUNNED");                // 128: + 0x11C bit 0
	int standingUp = CombatNames::modelCondition("STUNNED_STANDING_UP"); // 163: + 0x120 bit 3
	int freefall = CombatNames::modelCondition("FREEFALL");              // 72: + 0x115 bit 0
	int splatted = CombatNames::modelCondition("SPLATTED");              // 122: + 0x118 bit 26
	int dying = CombatNames::modelCondition("DYING");                    // 62: + 0x110 bit 30
};
const PhysNames &physNames()
{
	static const PhysNames n;
	return n;
}

void parseName(INI *ini, void *, void *store, const void *)
{
	*static_cast<std::string *>(store) = ini->getNextToken();
}

#define PB_OFF(member) (int)offsetof(PhysicsBehaviorModuleData, member)
// RW table 0xC30538 in the binary's row order (parse functions: 0x42E558 bool, 0x42ED00 real, 0x42EEFA percent, 0x42EC5E int, 0x73A429 duration, 0x73A302 FX name)
const FieldParse kPhysicsParse[] = {
	{ "TumbleRandomly", INI::parseBool, nullptr, PB_OFF(m_tumbleRandomly) },
	{ "AllowBouncing", INI::parseBool, nullptr, PB_OFF(m_allowBouncing) },
	{ "KillWhenRestingOnGround", INI::parseBool, nullptr, PB_OFF(m_killWhenRestingOnGround) },
	{ "GravityMult", INI::parseReal, nullptr, PB_OFF(m_gravityMult) },
	{ "OrientToFlightPath", INI::parseBool, nullptr, PB_OFF(m_orientToFlightPath) },
	{ "ShockStunnedTimeLow", INI::parseDurationUnsignedInt, nullptr, PB_OFF(m_shockStunnedTimeLow) },
	{ "ShockStunnedTimeHigh", INI::parseDurationUnsignedInt, nullptr, PB_OFF(m_shockStunnedTimeHigh) },
	{ "ShockStandingTime", INI::parseDurationUnsignedInt, nullptr, PB_OFF(m_shockStandingTime) },
	{ "FirstHeight", INI::parseReal, nullptr, PB_OFF(m_firstHeight) },
	{ "SecondHeight", INI::parseReal, nullptr, PB_OFF(m_secondHeight) },
	{ "FirstPercentIndent", INI::parsePercentToReal, nullptr, PB_OFF(m_firstPercentIndent) },
	{ "SecondPercentIndent", INI::parsePercentToReal, nullptr, PB_OFF(m_secondPercentIndent) },
	{ "BounceCount", INI::parseInt, nullptr, PB_OFF(m_bounceCount) },
	{ "BounceFirstHeight", INI::parseReal, nullptr, PB_OFF(m_bounceFirstHeight) },
	{ "BounceSecondHeight", INI::parseReal, nullptr, PB_OFF(m_bounceSecondHeight) },
	{ "BounceFirstPercentIndent", INI::parsePercentToReal, nullptr, PB_OFF(m_bounceFirstPercentIndent) },
	{ "BounceSecondPercentIndent", INI::parsePercentToReal, nullptr, PB_OFF(m_bounceSecondPercentIndent) },
	{ "GroundHitFX", parseName, nullptr, PB_OFF(m_groundHitFX) },
	{ "GroundBounceFX", parseName, nullptr, PB_OFF(m_groundBounceFX) },
	{ "IgnoreTerrainHeight", INI::parseBool, nullptr, PB_OFF(m_ignoreTerrainHeight) },
	{ "FirstPercentHeight", INI::parsePercentToReal, nullptr, PB_OFF(m_firstPercentHeight) },
	{ "SecondPercentHeight", INI::parsePercentToReal, nullptr, PB_OFF(m_secondPercentHeight) },
	{ "CurveFlattenMinDist", INI::parseReal, nullptr, PB_OFF(m_curveFlattenMinDist) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef PB_OFF

// RW 0x403111 on this path: the SSE float32 sum of squares, then the MSVCR71 sqrt (RW 0xA3CF96), which keeps the caller's precision control (PC24 under setFPMode
// RW 0x440809) and leaves the root in ST0 for the x87 arithmetic that follows (review r1: the binary64 root of SimMath::length3d gave speed bits 0x408a8cd7, retail 0x408a8cd6)
double retailLength(float x, float y, float z)
{
	return SimMath::sqrtPC24((double)SimMath::sumSquares3(x, y, z));
}

// RW 0x792DBD / 0x792997: a contained object flies only when its container is a HORDE (container template + 0x115 & 0x20, the HORDE bit of RW 0x693AC6)
bool containerForbidsFlight(const Object &obj)
{
	static const int kHorde = ObjectTemplateInfoBuilder::kindOfIndex("HORDE");
	const Object *c = obj.getContainedBy();
	return c != nullptr && !c->isKindOf((unsigned)kHorde);
}
} // namespace

// RW 0x792559
PhysicsBehaviorModuleData::PhysicsBehaviorModuleData()
	: m_shockStunnedTimeLow(LOGICFRAMES_PER_SECOND)
	, m_shockStunnedTimeHigh(2 * LOGICFRAMES_PER_SECOND)
	, m_shockStandingTime(LOGICFRAMES_PER_SECOND)
{
}

void PhysicsBehaviorModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kPhysicsParse);
}

void PhysicsBehavior::registerClass(ModuleFactory &modules)
{
	modules.bindTypedData<PhysicsBehaviorModuleData>("PhysicsBehavior", MODULETYPE_BEHAVIOR);
	modules.bindModuleProc("PhysicsBehavior", MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const PhysicsBehaviorModuleData *typed = dynamic_cast<const PhysicsBehaviorModuleData *>(data);
		if (!typed)
		{
			throw std::logic_error("PhysicsBehavior: the module data is not typed");
		}
		return std::make_unique<PhysicsBehavior>(thing, typed);
	});
}

const char *PhysicsBehavior::stopLine()
{
	return kStop;
}

PhysicsBehavior *PhysicsBehavior::find(Object &obj)
{
	for (const std::unique_ptr<BehaviorModule> &m : obj.modules())
	{
		if (PhysicsBehavior *p = dynamic_cast<PhysicsBehavior *>(m.get()))
		{
			return p;
		}
	}
	return nullptr;
}

// RW 0x792B69: everything zero, AllowBouncing copied, asleep
PhysicsBehavior::PhysicsBehavior(Thing *thing, const PhysicsBehaviorModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
	, m_allowBouncing(data->m_allowBouncing)
{
	setWakeFrame(getObject(), UPDATE_SLEEP_FOREVER);
}

// RW 0x792DBD
void PhysicsBehavior::fling(const Coord3D &velocity, unsigned a, unsigned b)
{
	Object *obj = getObject();
	if (containerForbidsFlight(*obj))
	{
		setWakeFrame(obj, UPDATE_SLEEP_FOREVER);
		return;
	}
	GameLogic &logic = obj->logic();
	logic.noteStop(kStop);
	// RW 0x792DF9 .. 0x792E32: status 70 (RW 0x693919: S-782); an AI whose current state does not answer State slot 0x38 (false for every ported state) drops its
	// orders: RW 0x6630D7 (the second machine: S-782), then aiIdle(CMD_FROM_AI) (RW 0x5E821A(2) on AI + 0x20)
	if (AIUpdateInterface *ai = obj->getAIUpdateInterface())
	{
		ai->aiIdle(CMD_FROM_AI);
	}
	setWakeFrame(obj, UPDATE_SLEEP_NONE);
	const Coord3D pos = *obj->getPosition();
	const float gravity = logic.settings().gravity; // GlobalData + 0xC4 (per frame^2)
	// the height above the ground (x87: the terrain's height in ST0, fsubr, fst dword), never below 0
	float height = SimMath::pc24Sub(pos.z, logic.getGroundHeight(pos.x, pos.y));
	if (0.0f > height)
	{
		height = 0.0f;
	}
	// the frames of the fall from that height: h += gravity * GravityMult * n until h is not above 0, at most 3 * LOGICFRAMES_PER_SECOND
	int fallFrames = 0;
	{
		float h = height;
		const int limit = LOGICFRAMES_PER_SECOND * 3;
		while (fallFrames < limit && h > 0.0f)
		{
			const float step = SimMath::mulf32(SimMath::mulf32(gravity, m_data->m_gravityMult), SimMath::sseFromInt32(fallFrames));
			h = SimMath::addf32(step, h);
			++fallFrames;
		}
	}
	// the upward speed: a quarter of the whole speed when that is more and the object is (nearly) on the ground
	float vz = velocity.z;
	const double quarter = SimMath::pc24MulW(retailLength(velocity.x, velocity.y, velocity.z), 0.25);
	if (quarter > (double)vz && fallFrames < 2)
	{
		vz = SimMath::fstpDword(quarter);
	}
	// the frames up and down: |2 vz / (gravity * GravityMult)| (x87 at 24 bits, CRT fabs, _ftol2), at least 1, plus the fall
	const double up = SimMath::pc24DivW(SimMath::pc24AddW((double)vz, (double)vz), SimMath::pc24MulW((double)gravity, (double)m_data->m_gravityMult));
	std::uint32_t flight = (std::uint32_t)SimMath::ftol2(SimMath::absD(up));
	if (flight < 1u)
	{
		flight = 1u;
	}
	flight += (std::uint32_t)fallFrames;
	const float frames = SimMath::fstpDword(SimMath::fildU32(flight)); // fild (the unsigned correction when negative), fstp dword
	float apex = SimMath::mulf32(SimMath::mulf32(frames, 0.5f), SimMath::mulf32(vz, 0.5f));
	if (0.0f > apex)
	{
		apex = 0.0f;
	}
	// the landing: the start moved by the horizontal velocity for the whole flight, on the ground
	const Coord3D move{ SimMath::mulf32(velocity.x, frames), SimMath::mulf32(velocity.y, frames), SimMath::mulf32(frames, 0.0f) };
	Coord3D landing{ SimMath::addf32(pos.x, move.x), SimMath::addf32(pos.y, move.y), SimMath::addf32(pos.z, move.z) };
	landing.z = logic.getGroundHeight(landing.x, landing.y); // RW: +10 for the layer query, then the ground height of that layer (ground only, S-161)
	// the speed along the curve: (|move| + 2 apex + height) / frames on the x87, at least -gravity
	const double along = SimMath::pc24DivW(SimMath::pc24AddW(SimMath::pc24AddW(retailLength(move.x, move.y, move.z), SimMath::pc24AddW((double)apex, (double)apex)), (double)height),
		(double)frames);
	float speed = SimMath::fstpDword(along);
	const float minSpeed = SimMath::subf32(0.0f, gravity);
	if (minSpeed > speed)
	{
		speed = minSpeed;
	}
	flyTo(landing, apex, speed, a, b);
}

// RW 0x792997
void PhysicsBehavior::flyTo(const Coord3D &landing, float apex, float speed, unsigned a, unsigned b)
{
	Object *obj = getObject();
	if (containerForbidsFlight(*obj))
	{
		setWakeFrame(obj, UPDATE_SLEEP_FOREVER);
		return;
	}
	obj->logic().noteStop(kStop);
	m_landing = landing;
	m_start = *obj->getPosition();
	m_speed = speed;
	if (buildCurve(true, apex))
	{
		m_step = 0;
		if (a != 0)
		{
			m_extraA = a;
		}
		if (b != 0)
		{
			m_extraB = b;
		}
		// the update runs twice at once (RW 0x792A12 / 0x792A2C) with the transform recorded between them (RW 0x792A18 .. 0x792A23: RW 0x6260E1 with the
		// frame, lane COMBAT-3): the object is recorded on the curve's start and stands on its second point in the frame it is flung, and the frame's own
		// record (phase 2, only for a frame not yet recorded) keeps the start, so the flight is drawn from where the object stood
		(void)update();
		obj->recordTransform(obj->logic().getFrame());
		(void)update();
		setWakeFrame(obj, UPDATE_SLEEP_NONE);
	}
}

// RW 0x79261A
bool PhysicsBehavior::buildCurve(bool recount, float apex)
{
	if (0.5f > apex)
	{
		apex = 0.5f;
	}
	const PhysicsBehaviorModuleData *d = m_data;
	const bool bounce = m_bounceIndex != 0;
	const float firstIndent = bounce ? d->m_bounceFirstPercentIndent : d->m_firstPercentIndent;
	const float secondIndent = bounce ? d->m_bounceSecondPercentIndent : d->m_secondPercentIndent;
	Coord3D cp[4];
	cp[0] = m_start;
	cp[3] = m_landing;
	float drop = SimMath::subf32(m_start.z, m_landing.z);
	if (0.0f > drop)
	{
		drop = 0.0f;
	}
	float frac = SimMath::divf32(SimMath::subf32(apex, drop), apex);
	if (0.0f > frac)
	{
		frac = 0.0f;
	}
	m_apex = SimMath::addf32(drop, apex);
	const float dx = SimMath::subf32(cp[3].x, cp[0].x);
	cp[1].x = SimMath::addf32(SimMath::mulf32(dx, firstIndent), cp[0].x);
	cp[2].x = SimMath::addf32(SimMath::mulf32(dx, secondIndent), cp[0].x);
	const float dy = SimMath::subf32(cp[3].y, cp[0].y);
	cp[1].y = SimMath::addf32(SimMath::mulf32(dy, firstIndent), cp[0].y);
	cp[2].y = SimMath::addf32(SimMath::mulf32(dy, secondIndent), cp[0].y);
	const float dz = SimMath::subf32(cp[3].z, cp[0].z);
	if (d->m_ignoreTerrainHeight)
	{
		cp[1].z = SimMath::addf32(SimMath::mulf32(d->m_firstPercentHeight, dz), cp[0].z);
		cp[2].z = SimMath::addf32(SimMath::mulf32(d->m_secondPercentHeight, dz), cp[0].z);
	}
	else
	{
		const float highest = ProjectileHighestAlongLine(getObject()->logic(), cp[0], cp[3]);
		const float h1 = bounce ? d->m_bounceFirstHeight : d->m_firstHeight;
		const float h2 = bounce ? d->m_bounceSecondHeight : d->m_secondHeight;
		const float rise2 = SimMath::mulf32(SimMath::mulf32(h2, frac), apex);
		const float rise1 = SimMath::mulf32(h1, apex);
		if (d->m_curveFlattenMinDist > 0.0f)
		{
			// (dz^2 + dy^2) + dx^2, the x87 root (24 bits) stored as a float
			const float sum = SimMath::addf32(SimMath::addf32(SimMath::mulf32(dz, dz), SimMath::mulf32(dy, dy)), SimMath::mulf32(dx, dx));
			const float dist = SimMath::fstpDword(SimMath::sqrtPC24((double)sum));
			float k = SimMath::divf32(dist, d->m_curveFlattenMinDist);
			if (k > 1.0f)
			{
				k = 1.0f;
			}
			float z1 = SimMath::addf32(SimMath::mulf32(dz, firstIndent), cp[0].z);
			if (highest > z1)
			{
				z1 = highest;
			}
			float z2 = SimMath::addf32(SimMath::mulf32(dz, secondIndent), cp[0].z);
			if (highest > z2)
			{
				z2 = highest;
			}
			cp[1].z = SimMath::addf32(SimMath::mulf32(k, rise1), z1);
			cp[2].z = SimMath::addf32(SimMath::mulf32(k, rise2), z2);
		}
		else
		{
			float base = highest > cp[0].z ? highest : cp[0].z;
			base = base > cp[3].z ? base : cp[3].z;
			cp[1].z = SimMath::addf32(rise1, base);
			cp[2].z = SimMath::addf32(rise2, base);
		}
	}
	BezierSegment curve(cp);
	if (recount)
	{
		// ceil(length / speed) on the x87, + 1.0, _ftol2
		const double q = SimMath::pc24DivW(curve.getApproximateLength(1.0f), (double)m_speed);
		m_pointCount = (int)SimMath::ftol2(SimMath::pc24AddW(SimMath::ceilD(q), 1.0));
	}
	if (m_pointCount < 3)
	{
		m_pointCount = 3;
	}
	curve.getSegmentPoints(m_pointCount, &m_points);
	return true;
}

// RW 0x79350E
UpdateSleepTime PhysicsBehavior::update()
{
	Object *obj = getObject();
	const PhysNames &n = physNames();
	m_bounced = false; // + 0x5E
	if (m_stunned && m_stunTimer > 0 && !obj->isEffectivelyDead())
	{
		--m_stunTimer;
		if (m_stunTimer < 1)
		{
			if (!obj->testModelCondition(n.stunned))
			{
				// RW 0x7935A2: up again
				if (obj->testModelCondition(n.standingUp))
				{
					setCondition(n.standingUp, false);
				}
				setStunned(false);
			}
			else
			{
				// RW 0x793564: lying -> standing up for ShockStandingTime (the drawable's refresh RW 0x67449C while in flight: client)
				setCondition(n.stunned, false);
				setCondition(n.standingUp, true);
				m_stunTimer = (int)m_data->m_shockStandingTime;
			}
		}
	}
	if (m_step >= m_points.size())
	{
		endOfFlight();
		return sleepTime();
	}
	obj->setPosition(&m_points[m_step]); // RW 0x70C201 (OrientToFlightPath's transform: S-782)
	// RW 0x793707 .. 0x793786 (lane COMBAT-3): the object's next position (obj + 0x198, + 0x1A6 set) is the next curve point; at the last point
	// point * 2.0 - the object's position (the point itself once the object stands on it); the pathfinder's per step update RW 0x6F0741 is S-782
	if (AIUpdateInterface *ai = obj->getAIUpdateInterface())
	{
		const bool last = m_step + 1 >= m_points.size();
		const Coord3D &p = m_points[m_step];
		const Coord3D &o = *obj->getPosition();
		const Coord3D next = last ? Coord3D{ SimMath::subf32(SimMath::mulf32(p.x, 2.0f), o.x), SimMath::subf32(SimMath::mulf32(p.y, 2.0f), o.y),
										 SimMath::subf32(SimMath::mulf32(p.z, 2.0f), o.z) }
								  : m_points[m_step + 1];
		ai->setPendingPosition(next, obj->logic().getFrame());
	}
	++m_step;
	return sleepTime();
}

// the object's model condition; RW 0x68B53C also wakes the AI (AI vslot 0x264 -> RW 0x662552)
void PhysicsBehavior::setCondition(int bit, bool on)
{
	Object *obj = getObject();
	if (obj->testModelCondition(bit) == on)
	{
		return;
	}
	obj->setModelConditionState(bit, on);
	if (AIUpdateInterface *ai = obj->getAIUpdateInterface())
	{
		ai->wakeUpNow();
	}
}

bool PhysicsBehavior::resistsShockwave(const Object &obj, bool askContainer)
{
	auto resists = [](const Object &o) {
		const ThingTemplate *tt = static_cast<const ThingTemplate *>(o.getTemplate())->getFinalOverride();
		const FieldValue *v = tt->findField("ShockwaveResistance"); // + 0x620, parseReal (RW 0x42ED00)
		const float *f = v ? std::get_if<float>(v) : nullptr;
		return f != nullptr && *f >= 100.0f; // RW 0xBD88D8, comiss / jae
	};
	if (resists(obj))
	{
		return true;
	}
	return askContainer && obj.getContainedBy() && resists(*obj.getContainedBy());
}

// RW 0x792A69
void PhysicsBehavior::setStunned(bool stunned)
{
	Object *obj = getObject();
	if (resistsShockwave(*obj, true))
	{
		stunned = false;
	}
	m_stunned = stunned;
	if (!stunned)
	{
		const PhysNames &n = physNames();
		setCondition(n.standingUp, false);
		setCondition(n.flailing, false);
		setCondition(n.stunned, false);
		m_stunTimer = 0;
	}
}

// RW 0x792AFF
void PhysicsBehavior::standUp()
{
	Object *obj = getObject();
	if (resistsShockwave(*obj, true))
	{
		return;
	}
	setCondition(physNames().stunned, true);
	m_stunned = true;
	m_stunTimer = (int)m_data->m_shockStandingTime;
	setWakeFrame(obj, UPDATE_SLEEP_NONE);
}

// RW 0x793372
void PhysicsBehavior::endOfFlight()
{
	if (m_points.empty())
	{
		return;
	}
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	const PhysNames &n = physNames();
	if (FXEventLog::isFXName(m_data->m_groundHitFX))
	{
		FXEvent e = FXEventLog::objectEvent(FXEvent::POSITION_FX, "PhysicsBehavior GroundHitFX", logic.getFrame(), m_data->m_groundHitFX, *obj); // RW 0x494615
		e.primary = INVALID_ID;
		e.position = *obj->getPosition();
		e.hasTransform = false;
		logic.fxEvents().emit(e);
	}
	if (obj->testModelCondition(n.freefall))
	{
		setCondition(n.splatted, true);
		setCondition(n.freefall, false);
		setCondition(n.flailing, false);
	}
	if (m_stunned && (m_stunTimer < 1 || obj->isEffectivelyDead()))
	{
		setCondition(n.flailing, false);
		setCondition(n.freefall, false);
		if (!obj->isEffectivelyDead())
		{
			setCondition(n.stunned, true);
			m_stunTimer = logic.random().getValue((int)m_data->m_shockStunnedTimeLow, (int)m_data->m_shockStunnedTimeHigh, kPhysicsUpdateCpp, 0x2F0); // RW 0x6D328E
		}
		else
		{
			setCondition(n.dying, true);
			setCondition(n.splatted, true);
		}
	}
	// (the drawable's refresh RW 0x67449C: client)
	if ((m_allowBouncing || m_data->m_allowBouncing) && m_bounceIndex < m_data->m_bounceCount)
	{
		bounce(false);
	}
	else
	{
		rest(true);
	}
}

// RW 0x79308D
void PhysicsBehavior::rest(bool final)
{
	Object *obj = getObject();
	m_points.clear(); // RW 0x8E6731
	m_step = 0;
	m_bounceIndex = 0;
	if (!m_killOnRest && !m_data->m_killWhenRestingOnGround)
	{
		if (!final)
		{
			setWakeFrame(obj, UPDATE_SLEEP_FOREVER);
		}
	}
	else
	{
		setCondition(physNames().splatted, true);
		obj->kill(0); // RW 0x698EC3(8, 0)
	}
	if (m_extraB != 0)
	{
		obj->logic().noteStop(kStop); // RW 0x6CF590(+0x64, +0x60, object): no ported caller passes the extra words
		m_extraB = 0;
	}
	m_extraA = 0;
}

// RW 0x793224
void PhysicsBehavior::bounce(bool final)
{
	Object *obj = getObject();
	if (!final)
	{
		setWakeFrame(obj, UPDATE_SLEEP_NONE);
	}
	++m_bounceIndex;
	const size_t count = m_points.size();
	if (count < 2)
	{
		rest(final);
		return;
	}
	const Coord3D &last = m_points[count - 1];
	const Coord3D &prev = m_points[count - 2];
	float dx = SimMath::subf32(last.x, prev.x);
	float dy = SimMath::subf32(last.y, prev.y);
	const float len2 = SimMath::addf32(SimMath::mulf32(dy, dy), SimMath::mulf32(dx, dx));
	if (len2 != 0.0f)
	{
		const float inv = ProjectileInvSqrt(len2); // RW 0x441C56, the result in ST0
		dx = SimMath::pc24Mul(dx, inv);
		dy = SimMath::pc24Mul(inv, dy);
	}
	// |landing - start| in the plane (RW 0x403111), * 0.5 (RW 0xBD869C) on the x87
	const double half = SimMath::pc24MulW(retailLength(SimMath::subf32(m_landing.x, m_start.x), SimMath::subf32(m_landing.y, m_start.y), 0.0f), 0.5);
	const Coord3D &pos = *obj->getPosition();
	Coord3D target;
	target.x = SimMath::fstpDword(SimMath::pc24AddW(SimMath::pc24MulW((double)dx, half), (double)pos.x));
	target.y = SimMath::fstpDword(SimMath::pc24AddW(SimMath::pc24MulW(half, (double)dy), (double)pos.y));
	target.z = obj->logic().getGroundHeight(target.x, target.y); // TheTerrainLogic vslot 0x18 (ground only, S-161)
	flyTo(target, SimMath::mulf32(m_apex, 0.35f), SimMath::mulf32(m_speed, 0.85f)); // RW 0xC30378 / 0xC3037C
	m_bounced = true;
}

// RW 0x792A41
UpdateSleepTime PhysicsBehavior::sleepTime() const
{
	if (!m_points.empty() || (m_stunned && m_stunTimer > 0))
	{
		return UPDATE_SLEEP_NONE;
	}
	return UPDATE_SLEEP_FOREVER;
}

void PhysicsBehavior::crc(StateHasher &h) const
{
	UpdateModule::crc(h);
	h.addU32((std::uint32_t)m_points.size());
	for (const Coord3D &p : m_points)
	{
		h.addFloat(p.x);
		h.addFloat(p.y);
		h.addFloat(p.z);
	}
	h.addFloat(m_start.x);
	h.addFloat(m_start.y);
	h.addFloat(m_start.z);
	h.addFloat(m_landing.x);
	h.addFloat(m_landing.y);
	h.addFloat(m_landing.z);
	h.addFloat(m_speed);
	h.addFloat(m_apex);
	h.addI32(m_pointCount);
	h.addU32(m_step);
	h.addI32(m_bounceIndex);
	h.addU32(m_extraA);
	h.addU32(m_extraB);
	h.addBool(m_allowBouncing);
	h.addI32(m_stunTimer);
	h.addBool(m_stunned);
	h.addBool(m_bounced);
	h.addBool(m_killOnRest);
}


// RW 0x4B3D8D Object::relativeAngle2D (x87 under PC24 for the offset and its length, SSE for the unit offset and the dot product, MSVCR71 acos (RW 0x42F4F0,
// stop S-167: SimMath::acosDet stands in), the sign of the cross product)
float ObjectKnockback::relativeAngle2D(const Object &obj, const Coord3D &point)
{
	const Coord3D &p = *obj.getPosition();
	const float dx = SimMath::pc24Sub(point.x, p.x);
	const float dy = SimMath::pc24Sub(point.y, p.y);
	const float sum = SimMath::pc24Add(SimMath::pc24Mul(dy, dy), SimMath::pc24Mul(dx, dx)); // fld st0; fmul st1; fld st2; fmul st3; faddp
	const float dist = SimMath::fstpDword(SimMath::sqrtPC24((double)sum));
	if (dist == 0.0f)
	{
		return 0.0f;
	}
	const float inv = SimMath::divf32(1.0f, dist);
	const float ux = SimMath::mulf32(inv, dx);
	const float uy = SimMath::mulf32(inv, dy);
	const float a = obj.getOrientation(); // RW 0x70B9E0: (fcos, fsin) of the angle, cached
	const float fx = SimMath::cosDet(a);
	const float fy = SimMath::sinDet(a);
	float dot = SimMath::addf32(SimMath::mulf32(fx, ux), SimMath::mulf32(fy, uy));
	if (-1.0f > dot)
	{
		dot = -1.0f; // RW 0xBDFC10 (double -1) / 0xBD19DC
	}
	else if (dot > 1.0f)
	{
		dot = 1.0f;
	}
	float angle = SimMath::fstpDword(SimMath::acosDet((double)dot));
	const float cross = SimMath::subf32(SimMath::mulf32(fx, uy), SimMath::mulf32(fy, ux));
	if (0.0f > cross)
	{
		angle = SimMath::subf32(0.0f, angle);
	}
	return angle;
}

// RW 0x692223 Object::doKnockback
bool ObjectKnockback::apply(Object &obj, float angleDegrees, float power, float zFactor, const char *projectileType)
{
	(void)projectileType; // RW 0x6922E5 .. 0x692307: the index of NONE / CATAPULT_ROCK / TREBUCHET_ROCK (RW 0xDA08FC) goes to the drawable (+ 0x360): client
	PhysicsBehavior *phys = PhysicsBehavior::find(obj); // Object + 0x264
	if (!phys || phys->isStunned())
	{
		return false;
	}
	if (PhysicsBehavior::resistsShockwave(obj, false)) // template + 0x620 >= 100 (the object only)
	{
		return false;
	}
	// fld deg; fmul pi / 180 (RW 0xBD1900); fst dword: the angle; MSVCR71 cos / sin of it as a double (stop S-167: the deterministic pair stands in)
	const float rad = SimMath::pc24Mul(angleDegrees, 0.0174532924f);
	double s = 0.0, c = 0.0;
	SimMath::sinCosDet((double)rad, s, c);
	const float cosF = SimMath::fstpDword(c);
	Coord3D v;
	v.x = SimMath::pc24Mul(cosF, power);                              // fld cos (dword); fmul power; fstp
	v.y = SimMath::fstpDword(SimMath::pc24MulW(s, (double)power));     // the sin result still in ST0; fmul power; fstp
	v.z = SimMath::mulf32(power, zFactor);                            // mulss
	phys->fling(v, 0, 0);                                             // RW 0x792DBD
	phys->setStunned(true);                                           // RW 0x792A69(1)
	obj.setModelConditionState(physNames().flailing, true);          // + 0x118 bit 31 (RW 0x68B53C)
	if (AIUpdateInterface *ai = obj.getAIUpdateInterface())
	{
		ai->wakeUpNow(); // AI vslot 0x264 (RW 0x662552)
	}
	return true;
}
