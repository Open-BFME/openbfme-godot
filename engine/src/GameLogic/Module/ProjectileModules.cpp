// OpenBFME. GPL-3.0.
// See GameLogic/Module/ProjectileModules.h for the target facts, the addresses and what is inference.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Module/ProjectileModules.h"
#include "GameLogic/Module/StructureModules.h"

#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/Combat/BezierSegment.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Combat/WeaponDelivery.h"
#include "GameLogic/Damage.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/Weapon.h"

#include <cstddef>
#include <cstring>
#include <stdexcept>

namespace
{
const char *const kEmotionNames[] = { "TAUNT", "CHEER", "HERO_CHEER", "POINT", "FEAR", "UNCONTROLLABLE_FEAR", "TERROR", "DOOM", "QUARRELSOME", "ALERT", "BRACE_FOR_BEING_CRUSHED",
	"CHEER_FOR_ABOUT_TO_CRUSH", nullptr }; // RW 0xD9FA08
const int kModelConditionThrownProjectile = 154;                      // ModelConditionNames.inc
const int kModelConditionAboutToHit = 155;
const int kModelConditionSplatted = 122;
const char *const kWeaponCpp = "Weapon.cpp";                          // RW 0xC18320
const float kPartitionCellSize = 40.0f;                               // ZH GameData PartitionCellSize (inference S-362)

// RW 0x8E09CE: a token, the index in the emotion table or -1 (an unknown name is accepted, like the binary)
void parseEmotion(INI *ini, void *, void *store, const void *)
{
	const char *t = ini->getNextToken();
	int found = -1;
	for (int i = 0; kEmotionNames[i]; ++i)
	{
		if (std::strcmp(t, kEmotionNames[i]) == 0)
		{
			found = i;
			break;
		}
	}
	*static_cast<int *>(store) = found;
}
void parseName(INI *ini, void *, void *store, const void *)
{
	*static_cast<std::string *>(store) = ini->getNextToken();
}

#define BP_OFF(member) (int)offsetof(BezierProjectileBehaviorModuleData, member)
// RW table 0xC58368 in the binary's row order
const FieldParse kBezierParse[] = {
	{ "TumbleRandomly", INI::parseBool, nullptr, BP_OFF(m_tumbleRandomly) },
	{ "DetonateCallsKill", INI::parseBool, nullptr, BP_OFF(m_detonateCallsKill) },
	{ "OrientToFlightPath", INI::parseBool, nullptr, BP_OFF(m_orientToFlightPath) },
	{ "FirstHeight", INI::parseReal, nullptr, BP_OFF(m_firstHeight) },
	{ "SecondHeight", INI::parseReal, nullptr, BP_OFF(m_secondHeight) },
	{ "FirstPercentIndent", INI::parsePercentToReal, nullptr, BP_OFF(m_firstPercentIndent) },
	{ "SecondPercentIndent", INI::parsePercentToReal, nullptr, BP_OFF(m_secondPercentIndent) },
	{ "CrushStyle", INI::parseBool, nullptr, BP_OFF(m_crushStyle) },
	{ "DieOnImpact", INI::parseBool, nullptr, BP_OFF(m_dieOnImpact) },
	{ "FinalStuckTime", INI::parseDurationUnsignedInt, nullptr, BP_OFF(m_finalStuckTime) },
	{ "PreLandingStateTime", INI::parseDurationUnsignedInt, nullptr, BP_OFF(m_preLandingStateTime) },
	{ "BounceCount", INI::parseInt, nullptr, BP_OFF(m_bounceCount) },
	{ "BounceDistance", INI::parseReal, nullptr, BP_OFF(m_bounceDistance) },
	{ "BounceFirstHeight", INI::parseReal, nullptr, BP_OFF(m_bounceFirstHeight) },
	{ "BounceSecondHeight", INI::parseReal, nullptr, BP_OFF(m_bounceSecondHeight) },
	{ "BounceFirstPercentIndent", INI::parsePercentToReal, nullptr, BP_OFF(m_bounceFirstPercentIndent) },
	{ "BounceSecondPercentIndent", INI::parsePercentToReal, nullptr, BP_OFF(m_bounceSecondPercentIndent) },
	{ "GarrisonHitKillRequiredKindOf", ParseKindOfMask, nullptr, BP_OFF(m_garrisonHitKillRequiredKindOf) },
	{ "GarrisonHitKillForbiddenKindOf", ParseKindOfMask, nullptr, BP_OFF(m_garrisonHitKillForbiddenKindOf) },
	{ "GarrisonHitKillCount", INI::parseUnsignedInt, nullptr, BP_OFF(m_garrisonHitKillCount) },
	{ "GarrisonHitKillFX", parseName, nullptr, BP_OFF(m_garrisonHitKillFX) },
	{ "GroundHitFX", parseName, nullptr, BP_OFF(m_groundHitFX) },
	{ "GroundBounceFX", parseName, nullptr, BP_OFF(m_groundBounceFX) },
	{ "GroundHitWeapon", parseName, nullptr, BP_OFF(m_groundHitWeapon) },
	{ "GroundBounceWeapon", parseName, nullptr, BP_OFF(m_groundBounceWeapon) },
	{ "FlightPathAdjustDistPerSecond", INI::parseVelocityReal, nullptr, BP_OFF(m_flightPathAdjustDistPerFrame) },
	{ "IgnoreTerrainHeight", INI::parseBool, nullptr, BP_OFF(m_ignoreTerrainHeight) },
	{ "FirstPercentHeight", INI::parsePercentToReal, nullptr, BP_OFF(m_firstPercentHeight) },
	{ "SecondPercentHeight", INI::parsePercentToReal, nullptr, BP_OFF(m_secondPercentHeight) },
	{ "CurveFlattenMinDist", INI::parseReal, nullptr, BP_OFF(m_curveFlattenMinDist) },
	{ "PreLandingEmotion", parseEmotion, nullptr, BP_OFF(m_preLandingEmotion) },
	{ "PreLandingEmotionRadius", INI::parseReal, nullptr, BP_OFF(m_preLandingEmotionRadius) },
	{ "InvisibleFrames", INI::parseUnsignedInt, nullptr, BP_OFF(m_invisibleFrames) },
	{ "FadeInTime", INI::parseUnsignedInt, nullptr, BP_OFF(m_fadeInTime) },
	{ "PostLandingStateTime", INI::parseDurationUnsignedInt, nullptr, BP_OFF(m_postLandingStateTime) },
	{ "PostLandingEmotion", parseEmotion, nullptr, BP_OFF(m_postLandingEmotion) },
	{ "PostLandingEmotionRadius", INI::parseReal, nullptr, BP_OFF(m_postLandingEmotionRadius) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef BP_OFF

// RW 0x6CA03E getMinimumAttackRange: the template's MinimumAttackRange less 2.5, never below 0
float minimumAttackRange(const WeaponTemplate &w)
{
	const float v = SimMath::subf32(w.m_minimumAttackRange, 2.5f);
	return 0.0f > v ? 0.0f : v;
}

// The highest terrain along the line (ZH PartitionManager::estimateTerrainExtremesAlongLine: Bresenham over the partition cells, each cell's maximum height). INFERENCE S-362: RW's
// own routine (the terrain object's slot 0x40 of 0xDE4690) was not read; a cell's maximum is sampled on a 3x3 lattice of the ground height.
float highestAlongLine(const GameLogic &logic, const Coord3D &a, const Coord3D &b)
{
	auto cellOf = [](float v) { return SimMath::floorToInt(SimMath::divf32(v, kPartitionCellSize)); };
	const int sx = cellOf(a.x), sy = cellOf(a.y), ex = cellOf(b.x), ey = cellOf(b.y);
	const int dxAbs = ex >= sx ? ex - sx : sx - ex;
	const int dyAbs = ey >= sy ? ey - sy : sy - ey;
	int x = sx, y = sy;
	int xinc1 = ex >= sx ? 1 : -1, xinc2 = xinc1, yinc1 = ey >= sy ? 1 : -1, yinc2 = yinc1;
	int den, num, numadd, numpixels;
	if (dxAbs >= dyAbs)
	{
		xinc1 = 0;
		yinc2 = 0;
		den = dxAbs;
		num = dxAbs / 2;
		numadd = dyAbs;
		numpixels = dxAbs;
	}
	else
	{
		xinc2 = 0;
		yinc1 = 0;
		den = dyAbs;
		num = dyAbs / 2;
		numadd = dxAbs;
		numpixels = dyAbs;
	}
	float best = -1.0e10f;
	for (int i = 0; i <= numpixels; ++i)
	{
		for (int ix = 0; ix < 3; ++ix)
		{
			for (int iy = 0; iy < 3; ++iy)
			{
				const float wx = SimMath::mulf32(SimMath::addf32(SimMath::sseFromInt32(x), SimMath::mulf32(SimMath::sseFromInt32(ix), 0.5f)), kPartitionCellSize);
				const float wy = SimMath::mulf32(SimMath::addf32(SimMath::sseFromInt32(y), SimMath::mulf32(SimMath::sseFromInt32(iy), 0.5f)), kPartitionCellSize);
				const float h = logic.getGroundHeight(wx, wy);
				if (h > best)
				{
					best = h;
				}
			}
		}
		num += numadd;
		if (num >= den)
		{
			num -= den;
			x += xinc1;
			y += yinc1;
		}
		x += xinc2;
		y += yinc2;
	}
	return best;
}

Coord3D normalizeCoord(Coord3D v) // RW 0x403175
{
	const float len = SimMath::fstpDword(SimMath::sqrtPC24((double)SimMath::sumSquares3(v.x, v.y, v.z)));
	if (len != 0.0f)
	{
		const float inv = SimMath::divf32(1.0f, len);
		v.x = SimMath::mulf32(v.x, inv);
		v.y = SimMath::mulf32(v.y, inv);
		v.z = SimMath::mulf32(v.z, inv);
	}
	return v;
}
} // namespace

float ProjectileHighestAlongLine(const GameLogic &logic, const Coord3D &a, const Coord3D &b)
{
	return highestAlongLine(logic, a, b);
}

// RW 0x441C56 WWMath::Inv_Sqrt: bit-pattern guess, then three Newton steps with the products regrouped (ab, ab^2, ab^2c, ab^2c^2), every x87 operation at 24 bits
float ProjectileInvSqrt(float value)
{
	std::uint32_t bits;
	std::memcpy(&bits, &value, sizeof(bits));
	const std::uint32_t guessBits = (0xbe6eb508u - bits) >> 1;
	const std::uint32_t halfBits = bits - 0x800000u;
	float y0, xh;
	std::memcpy(&y0, &guessBits, sizeof(y0));
	std::memcpy(&xh, &halfBits, sizeof(xh));
	const float y2 = SimMath::pc24Mul(y0, y0);
	const float a = SimMath::pc24Mul(y2, xh);
	const float b = SimMath::pc24Sub(1.5f, a);
	const float ab = SimMath::pc24Mul(a, b);
	const float ab2 = SimMath::pc24Mul(ab, b);
	const float yb = SimMath::pc24Mul(y0, b);
	const float c = SimMath::pc24Sub(1.5f, ab2);
	const float ab2c = SimMath::pc24Mul(ab2, c);
	const float ybc = SimMath::pc24Mul(yb, c);
	const float ab2c2 = SimMath::pc24Mul(ab2c, c);
	const float d = SimMath::pc24Sub(1.5f, ab2c2);
	return SimMath::pc24Mul(ybc, d);
}

void BezierProjectileBehaviorModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kBezierParse);
}

void ProjectileModules::registerAll(ModuleFactory &modules)
{
	modules.bindTypedData<BezierProjectileBehaviorModuleData>("BezierProjectileBehavior", MODULETYPE_BEHAVIOR);
	modules.bindModuleProc("BezierProjectileBehavior", MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const BezierProjectileBehaviorModuleData *typed = dynamic_cast<const BezierProjectileBehaviorModuleData *>(data);
		if (!typed)
		{
			throw std::logic_error("BezierProjectileBehavior: the module data is not typed");
		}
		return std::make_unique<BezierProjectileBehavior>(thing, typed);
	});
}

// RW 0x85F899 (the constructor): everything zero, the height scale 1, the update asleep until the launch
BezierProjectileBehavior::BezierProjectileBehavior(Thing *thing, const BezierProjectileBehaviorModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
	setWakeFrame(getObject(), UPDATE_SLEEP_FOREVER);
}

// RW 0x85EBAF: a module with a path keeps updating every frame, one without sleeps
UpdateSleepTime BezierProjectileBehavior::sleepTime() const
{
	return m_flightPath.empty() ? UPDATE_SLEEP_FOREVER : UPDATE_SLEEP_NONE;
}

// RW 0x85E99A
void BezierProjectileBehavior::projectileLaunchAtObjectOrPosition(const Object *victim, const Coord3D *victimPos, Object *launcher, int wslot, int barrel, const WeaponTemplate *weapon,
	const WeaponTemplate *warhead)
{
	Object *obj = getObject();
	m_launcherID = launcher ? launcher->getID() : (ObjectID)INVALID_ID;
	m_bonusFlags = launcher ? launcher->weaponBonusConditionMask() : 0u;
	m_victimID = victim ? victim->getID() : (ObjectID)INVALID_ID;
	m_weapon = weapon;
	m_warhead = warhead;
	m_altCurve = 0;
	positionForLaunch(launcher, wslot, barrel);
	m_originPos = launcher ? *launcher->getPosition() : *obj->getPosition();
	projectileFireAtObjectOrPosition(victim, victimPos);
	obj->setModelConditionState(kModelConditionThrownProjectile, true);
	obj->setStatus((unsigned)CombatNames::statuses().noAttack, true);
	++obj->logic().combat().counters().projectilesLaunched;
}

// RW 0x6CAB85 Weapon::calcProjectileLaunchPosition (lane RENDER-2): the launch bone transform of the launcher's drawable (RW 0x6756A1; the identity when there is
// no drawable or no bone, RW 0x6CACDA) composed with the launcher's transform (RW 0x70BCE7 Object::convertBonePosToWorldPos). TARGET FACTS: a launcher with status
// INSIDE_GARRISON (bit 0x3A, RW 0x6CAC0F) inside a container whose contain module says so (vslot 0xB0, bit 29 of the answer) asks the CONTAINER's AI, drawable and
// transform instead; the turret composition (RW 0x6CAD3F..0x6CB43B) runs when the slot's weapon is a turret weapon (WeaponTemplate + 0x16C) and the launcher's AI names a
// turret for the slot (RW 0x66246B). This port's AI has no turrets (every slot answers TURRET_INVALID, so the composition is never reached) and the garrison branch is
// not ported (counted: CombatState::Counters::garrisonLaunchUnported, stop S-360); both fall back to the launcher's own drawable and transform, the branch retail
// takes when the status is clear.
// The products and sums are retail's SSE order: W[r][c] = (T[r][2] * B[2][c] + T[r][1] * B[1][c]) + T[r][0] * B[0][c], the translation adding T[r][3] last.
void BezierProjectileBehavior::calcLaunchTransform(const Object &launcher, int wslot, int barrel, Coord3D &pos, float basis[9])
{
	GameLogic &logic = getObject()->logic();
	// lane GARRISON-1: RW 0x6CAC0F .. 0x6CAC8E, the garrison branch: an INSIDE_GARRISON launcher's container (its horde's container when the container is a HORDE,
	// RW 0x6CAC25) whose contain's ObjectStatusOfContained has ENCLOSED (slot 0xB0, bit 29 of word 1) lends its drawable (the launch bone) and its transform
	// (RW 0x6CACB8 onwards); its AI's turret for the slot (RW 0x66246B) is TURRET_INVALID in this port (no AI turrets, S-1103)
	const Object *source = &launcher;
	if (launcher.testStatus((unsigned)CombatNames::statuses().insideGarrison) && launcher.getContainedBy())
	{
		const Object *container = launcher.getContainedBy();
		if (container->isKindOf((unsigned)CombatNames::kinds().horde))
		{
			container = container->getContainedBy();
		}
		if (container && container->getContain())
		{
			static const int enclosed = CombatNames::status("ENCLOSED");
			const ObjectStatusMaskType *osoc = container->getContain()->getObjectStatusOfContained();
			if (osoc && (((*osoc)[(size_t)enclosed >> 5] >> (enclosed & 31)) & 1u))
			{
				source = container;
				++logic.combat().counters().garrisonLaunches;
			}
		}
	}
	float bone[12] = { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f };
	ProjectileLaunchOffsets *offsets = logic.combat().launchOffsets();
	if (!offsets)
	{
		// no provider: a GameLogic run without the W3D assets (unit tests); retail always has the drawable's answer. Counted and reported (S-460)
		++logic.combat().counters().launchesWithoutBones;
	}
	else if (offsets->launchOffset(*source, wslot, barrel, bone))
	{
		++logic.combat().counters().launchBonesFound;
	}
	const float *t = source->getBasis();
	const Coord3D &p = *source->getPosition();
	const float tt[3] = { p.x, p.y, p.z };
	float world[12];
	for (int r = 0; r < 3; ++r)
	{
		for (int c = 0; c < 4; ++c)
		{
			float v = SimMath::addf32(SimMath::mulf32(t[r * 3 + 2], bone[8 + c]), SimMath::mulf32(t[r * 3 + 1], bone[4 + c]));
			v = SimMath::addf32(v, SimMath::mulf32(t[r * 3 + 0], bone[c]));
			if (c == 3)
			{
				v = SimMath::addf32(v, tt[r]);
			}
			world[r * 4 + c] = v;
		}
	}
	for (int r = 0; r < 3; ++r)
	{
		for (int c = 0; c < 3; ++c)
		{
			basis[r * 3 + c] = world[r * 4 + c];
		}
	}
	pos.x = world[3];
	pos.y = world[7];
	pos.z = world[11];
}

// RW 0x6CB490 Weapon::positionProjectileForLaunch: no launcher destroys the projectile (RW 0x6CB49E); otherwise the projectile takes the transform and then the
// position of RW 0x6CAB85 (RW 0x70BA76 setTransformMatrix, RW 0x70C201 setPosition; the drawable call between them is client side)
void BezierProjectileBehavior::positionForLaunch(Object *launcher, int wslot, int barrel)
{
	Object *obj = getObject();
	if (!launcher)
	{
		obj->logic().destroyObject(obj); // RW 0x6CB49E
		return;
	}
	Coord3D pos;
	float basis[9];
	calcLaunchTransform(*launcher, wslot, barrel, pos, basis);
	obj->setTransform(&pos, basis);
}

// RW 0x6CB85A getAimPosition(out, projectile, victim, flag = 1). The weapon's aim bone is a drawable query (not available); the aim point of the victim's geometry (RW 0xAD30E0, the
// victim's aim points chosen by a seed) is INFERENCE S-362: the geometry centre. The seed IS drawn (GameLogicRandomValue(0, 12345678) at Weapon.cpp:1749, RW 0x6CB8C2..0x6CB8D3): the
// retail logic random sequence advances once per aim query
Coord3D BezierProjectileBehavior::aimPosition(const Object &victim)
{
	Object *obj = getObject();
	obj->logic().random().getValue(0, 12345678, kWeaponCpp, 1749);
	Coord3D p = *victim.getPosition();
	p.z = SimMath::addf32(p.z, SimMath::mulf32(CombatQueries::geometry(victim).height, 0.5f));
	return p;
}

// RW 0x85EBEE
void BezierProjectileBehavior::projectileFireAtObjectOrPosition(const Object *victim, const Coord3D *victimPos)
{
	Object *obj = getObject();
	const BezierProjectileBehaviorModuleData *d = m_data;
	const WeaponTemplate *weapon = m_weapon;
	float weaponSpeed = weapon ? weapon->m_weaponSpeed : 0.0f;
	const float minWeaponSpeed = weapon ? weapon->m_minWeaponSpeed : 0.0f;
	setWakeFrame(obj, UPDATE_SLEEP_NONE);
	if (!d)
	{
		return;
	}
	Coord3D victimPosToUse;
	if (victim)
	{
		victimPosToUse = aimPosition(*victim);
	}
	else
	{
		victimPosToUse = *victimPos;
	}
	if (d->m_crushStyle)
	{
		// RW 0x85EC66: a destination on the ground layer (this port has only the ground layer, S-362) lands on the terrain
		victimPosToUse.z = obj->logic().getGroundHeight(victimPosToUse.x, victimPosToUse.y);
	}
	if (weapon && weapon->m_isScaleWeaponSpeed)
	{
		weaponSpeed = weaponSpeed > minWeaponSpeed ? weaponSpeed : minWeaponSpeed;
		const float minRange = minimumAttackRange(*weapon);
		const float maxRange = weapon->m_attackRange;
		const Coord3D &op = *obj->getPosition();
		const float dx = SimMath::subf32(op.x, victimPosToUse.x);
		const float dy = SimMath::subf32(op.y, victimPosToUse.y);
		const float distSq = SimMath::addf32(SimMath::mulf32(dy, dy), SimMath::mulf32(dx, dx));
		// the binary subtracts the minimum RANGE from the SQUARED distance (RW 0x85ED16 subss on the sum of squares): kept
		float speed = SimMath::divf32(SimMath::subf32(distSq, minRange), SimMath::subf32(maxRange, minRange));
		speed = SimMath::addf32(SimMath::mulf32(speed, SimMath::subf32(weaponSpeed, minWeaponSpeed)), minWeaponSpeed);
		m_flightPathSpeed = speed;
		if (speed > weapon->m_maxWeaponSpeed)
		{
			m_flightPathSpeed = weapon->m_maxWeaponSpeed;
		}
	}
	else
	{
		m_flightPathSpeed = weaponSpeed;
	}
	// RW 0x85ED52: the orientation reset of a KindOf'd projectile and the tumble rates need the physics module (not ported, S-363)
	m_flightPathStart = *obj->getPosition();
	m_flightPathEnd = victimPosToUse;
	if (!calcFlightPath(true))
	{
		obj->logic().destroyObject(obj);
		return;
	}
	m_currentFlightPathStep = 0;
	m_fireFrame = obj->logic().getFrame();
	// RW 0x85EE00 .. 0x85EF31: the drawable's fade (InvisibleFrames / FadeInTime, or a fade over the flight when the launcher or the target is fogged for the
	// local player) is client state: the client snapshot carries m_fireFrame (projectileClientInfo) and GameClient/DrawableFade starts it (lane PROJ-2).
	// RW 0x85EF34 .. 0x85EF5E (lane PROJ-2): the projectile takes its first two steps at once: update, the partition refresh (RW 0x68C7E9: this port's
	// partition reads the current position), Object::recordTransform(now) (RW 0x6260E1), update, the partition refresh. So at the end of the launch frame the
	// projectile stands on path point 1 with path point 0 recorded, and its scheduled update (woken at now + 1 above) takes point 2 in the next frame
	update();
	obj->recordTransform(obj->logic().getFrame());
	update();
}

// RW 0x85E658
bool BezierProjectileBehavior::calcFlightPath(bool recalcNumSegments)
{
	Object *obj = getObject();
	const BezierProjectileBehaviorModuleData *d = m_data;
	const bool alt = m_altCurve != 0;
	const float firstPct = alt ? d->m_bounceFirstPercentIndent : d->m_firstPercentIndent;
	const float secondPct = alt ? d->m_bounceSecondPercentIndent : d->m_secondPercentIndent;
	m_heightScale = 1.0f;
	Coord3D cp[4];
	cp[0] = m_flightPathStart;
	cp[3] = m_flightPathEnd;
	const float dx = SimMath::subf32(cp[3].x, cp[0].x);
	cp[1].x = SimMath::addf32(SimMath::mulf32(dx, firstPct), cp[0].x);
	cp[2].x = SimMath::addf32(SimMath::mulf32(dx, secondPct), cp[0].x);
	const float dy = SimMath::subf32(cp[3].y, cp[0].y);
	cp[1].y = SimMath::addf32(SimMath::mulf32(dy, firstPct), cp[0].y);
	cp[2].y = SimMath::addf32(SimMath::mulf32(dy, secondPct), cp[0].y);
	const float dz = SimMath::subf32(cp[3].z, cp[0].z);
	if (d->m_ignoreTerrainHeight)
	{
		cp[1].z = SimMath::addf32(SimMath::mulf32(d->m_firstPercentHeight, dz), cp[0].z);
		cp[2].z = SimMath::addf32(SimMath::mulf32(d->m_secondPercentHeight, dz), cp[0].z);
	}
	else
	{
		float highest = highestAlongLine(obj->logic(), cp[0], cp[3]);
		const float firstHeight = alt ? d->m_bounceFirstHeight : d->m_firstHeight;
		const float secondHeight = alt ? d->m_bounceSecondHeight : d->m_secondHeight;
		if (d->m_curveFlattenMinDist > 0.0f)
		{
			const float thresh = SimMath::mulf32(d->m_curveFlattenMinDist, 0.5f);
			// RW 0x85E7E8: (z^2 + y^2) + x^2, the root of the x87 (24 bits), stored as a float
			const float sum = SimMath::addf32(SimMath::addf32(SimMath::mulf32(dz, dz), SimMath::mulf32(dy, dy)), SimMath::mulf32(dx, dx));
			const float len = SimMath::fstpDword(SimMath::sqrtPC24((double)sum));
			if (thresh > len)
			{
				m_heightScale = 0.0f;
			}
			else if (d->m_curveFlattenMinDist > len)
			{
				m_heightScale = SimMath::divf32(SimMath::subf32(len, thresh), thresh);
			}
			float z1 = SimMath::addf32(SimMath::mulf32(dz, firstPct), cp[0].z);
			float z2 = SimMath::addf32(SimMath::mulf32(dz, secondPct), cp[0].z);
			if (highest > z1)
			{
				z1 = highest;
			}
			if (highest > z2)
			{
				z2 = highest;
			}
			cp[1].z = SimMath::addf32(SimMath::mulf32(m_heightScale, firstHeight), z1);
			cp[2].z = SimMath::addf32(SimMath::mulf32(m_heightScale, secondHeight), z2);
		}
		else
		{
			highest = highest > cp[0].z ? highest : cp[0].z;
			highest = highest > cp[3].z ? highest : cp[3].z;
			cp[1].z = SimMath::addf32(firstHeight, highest);
			cp[2].z = SimMath::addf32(secondHeight, highest);
		}
	}
	BezierSegment curve(cp);
	if (recalcNumSegments)
	{
		const float speed = alt ? SimMath::mulf32(m_flightPathSpeed, 0.5f) : m_flightPathSpeed;
		const double flightDistance = curve.getApproximateLength(1.0f);
		const double quotient = SimMath::pc24DivW(flightDistance, (double)speed);
		m_flightPathSegments = SimMath::ftol2(SimMath::ceilD(quotient));
	}
	if (m_flightPathSegments < 2)
	{
		m_flightPathSegments = 2;
	}
	curve.getSegmentPoints(m_flightPathSegments, &m_flightPath);
	return true;
}

// RW 0x85F28A
UpdateSleepTime BezierProjectileBehavior::update()
{
	Object *obj = getObject();
	const BezierProjectileBehaviorModuleData *d = m_data;
	GameLogic &logic = obj->logic();
	if (!d || !obj)
	{
		return UPDATE_SLEEP_FOREVER;
	}
	const int n = (int)m_flightPath.size();
	if (m_currentFlightPathStep >= n)
	{
		projectileHandleCollision(nullptr);
		return sleepTime();
	}
	// RW 0x85F2DD: PreLandingStateTime frames before the end the model state changes (and the units near the landing spot are scared: not ported, S-363)
	if (d->m_preLandingStateTime != 0 && m_currentFlightPathStep == n - (int)d->m_preLandingStateTime)
	{
		obj->setModelConditionState(kModelConditionAboutToHit, true);
		if (d->m_preLandingEmotion != -1)
		{
			++logic.combat().counters().projectileEmotionsUnported;
		}
	}
	// RW 0x85F42B: FlightPathAdjustDistPerSecond: the end of the path follows the victim, at most that far per frame
	if (m_victimID != INVALID_ID && d->m_flightPathAdjustDistPerFrame > 0.0f)
	{
		Object *victim = logic.findObjectByID(m_victimID);
		if (victim)
		{
			const Coord3D now = aimPosition(*victim);
			Coord3D delta{ SimMath::subf32(now.x, m_flightPathEnd.x), SimMath::subf32(now.y, m_flightPathEnd.y), SimMath::subf32(now.z, m_flightPathEnd.z) };
			const float distSq = SimMath::addf32(SimMath::addf32(SimMath::mulf32(delta.z, delta.z), SimMath::mulf32(delta.y, delta.y)), SimMath::mulf32(delta.x, delta.x));
			if (distSq > 0.1f)
			{
				float dist = SimMath::fstpDword(SimMath::sqrtPC24((double)distSq));
				if (dist > d->m_flightPathAdjustDistPerFrame)
				{
					dist = d->m_flightPathAdjustDistPerFrame;
				}
				delta = normalizeCoord(delta);
				m_flightPathEnd.x = SimMath::addf32(SimMath::mulf32(dist, delta.x), m_flightPathEnd.x);
				m_flightPathEnd.y = SimMath::addf32(SimMath::mulf32(dist, delta.y), m_flightPathEnd.y);
				m_flightPathEnd.z = SimMath::addf32(SimMath::mulf32(dist, delta.z), m_flightPathEnd.z);
				if (!calcFlightPath(false))
				{
					detonate();
					return sleepTime();
				}
			}
		}
	}
	// RW 0x85F565: the step of the path
	const Coord3D step = m_flightPath[(size_t)m_currentFlightPathStep];
	if (d->m_orientToFlightPath && !d->m_tumbleRandomly)
	{
		const float lower = SimMath::mulf32(m_heightScale, 20.0f);
		const int cur = m_currentFlightPathStep;
		Coord3D a, b;
		float azFixed, bzFixed;
		if (cur > 0)
		{
			a = m_flightPath[(size_t)cur - 1];
			azFixed = a.z;
		}
		else
		{
			a = m_flightPath[(size_t)cur];
			azFixed = SimMath::subf32(a.z, lower);
		}
		if (cur < n - 1)
		{
			b = m_flightPath[(size_t)cur + 1];
			bzFixed = b.z;
		}
		else
		{
			b = m_flightPath[(size_t)cur];
			bzFixed = SimMath::subf32(b.z, lower);
		}
		Coord3D dir{ SimMath::subf32(b.x, a.x), SimMath::subf32(b.y, a.y), SimMath::subf32(bzFixed, azFixed) };
		const float lenSq = SimMath::addf32(SimMath::addf32(SimMath::mulf32(dir.x, dir.x), SimMath::mulf32(dir.z, dir.z)), SimMath::mulf32(dir.y, dir.y));
		if (lenSq != 0.0f)
		{
			const float inv = ProjectileInvSqrt(lenSq);
			dir.x = SimMath::mulf32(dir.x, inv);
			dir.y = SimMath::mulf32(dir.y, inv);
			dir.z = SimMath::mulf32(dir.z, inv);
		}
		// Matrix3D::buildTransformMatrix (RW 0xB26610): yaw about Z by (dir.x, dir.y) / len2, then the pitch about Y; the columns are the object's X, Y, Z axes
		const float len2 = SimMath::fstpDword(SimMath::sqrtPC24(SimMath::pc24AddW(SimMath::pc24MulW(dir.x, dir.x), SimMath::pc24MulW(dir.y, dir.y))));
		float siny = 0.0f, cosy = 1.0f;
		if (len2 != 0.0f)
		{
			const float inv2 = SimMath::divf32(1.0f, len2);
			siny = SimMath::mulf32(inv2, dir.y);
			cosy = SimMath::mulf32(inv2, dir.x);
		}
		const float sinp = dir.z, cosp = len2;
		// rows of the rotation: X axis (cosy cosp, siny cosp, sinp), Y axis (-siny, cosy, 0), Z axis (-cosy sinp, -siny sinp, cosp)
		const float basis[9] = { SimMath::mulf32(cosy, cosp), SimMath::subf32(0.0f, siny), SimMath::subf32(0.0f, SimMath::mulf32(cosy, sinp)),
			SimMath::mulf32(siny, cosp), cosy, SimMath::subf32(0.0f, SimMath::mulf32(siny, sinp)), sinp, 0.0f, cosp };
		obj->setTransform(&step, basis);
	}
	else
	{
		obj->setPosition(&step);
	}
	// RW 0x85F6E7 .. 0x85F71E (lane PROJ-2): the drawable's look-ahead point, the next path point; at the last point 2 * point - position (DAT 0xBD889C =
	// 2.0, scalar SSE), i.e. the point itself. The client's Catmull-Rom (RW 0x6765B9, GameClient/RenderInterpolation) draws through it.
	{
		const int cur = m_currentFlightPathStep;
		if (cur < n - 1)
		{
			m_pendingPosition = m_flightPath[(size_t)cur + 1];
		}
		else
		{
			const Coord3D &p = m_flightPath[(size_t)cur];
			const Coord3D &o = *obj->getPosition();
			m_pendingPosition = Coord3D{ SimMath::subf32(SimMath::mulf32(p.x, 2.0f), o.x), SimMath::subf32(SimMath::mulf32(p.y, 2.0f), o.y),
				SimMath::subf32(SimMath::mulf32(p.z, 2.0f), o.z) };
		}
		m_pendingFrame = logic.getFrame();
		m_pendingValid = true;
	}
	// RW 0x85F78E..0x85F83A the bridge layers: ground only (S-362)
	if (m_victimID != INVALID_ID)
	{
		Object *victim = logic.findObjectByID(m_victimID);
		if (victim)
		{
			// RW 0x85F737..0x85F783. The vector tested is the victim's centre in WORLD coordinates (nothing is subtracted): the length is the distance from the map origin. The binary
			// does this; the projectile only meets its victim early when the victim stands within radius + speed of (0, 0, 0)
			const float geomHeight = CombatQueries::geometry(*victim).height;
			Coord3D c = *victim->getPosition();
			c.z = SimMath::fstpDword(SimMath::pc24AddW(SimMath::pc24MulW(geomHeight, 0.5), (double)c.z));
			const double length = SimMath::sqrtPC24((double)SimMath::sumSquares3(c.x, c.y, c.z));
			const float radius = CombatQueries::boundingCircleRadius(*victim);
			if (SimMath::pc24AddW((double)radius, (double)m_flightPathSpeed) > length)
			{
				projectileHandleCollision(victim);
			}
		}
	}
	++m_currentFlightPathStep;
	return sleepTime();
}

// RW 0x85EF6A: the weapon's ProjectileCollidesWith test (RW 0x6CC79C) is not ported (S-364): a live path and a warhead
bool BezierProjectileBehavior::projectileCanCollideWith(const Object *other) const
{
	if (m_flightPath.empty() || !m_warhead)
	{
		return false;
	}
	if (other && other->isEffectivelyDead())
	{
		return false;
	}
	return true;
}

bool BezierProjectileBehavior::alreadyHit(ObjectID id) const
{
	for (ObjectID h : m_hitList)
	{
		if (h == id)
		{
			return true;
		}
	}
	return false;
}

// RW 0x85FDB7
bool BezierProjectileBehavior::projectileHandleCollision(Object *other)
{
	if (m_flightPath.empty())
	{
		return false;
	}
	Object *obj = getObject();
	const BezierProjectileBehaviorModuleData *d = m_data;
	GameLogic &logic = obj->logic();
	CombatState::Counters &counters = logic.combat().counters();
	if (other)
	{
		// RW 0x85EA5F: back the projectile off along its path until it no longer overlaps `other` (GeometryInfo overlap RW 0xAD2CE0 is not ported: S-363, the position stays)
		if (!d->m_crushStyle)
		{
			detonate();
			return true;
		}
		if (!alreadyHit(other->getID()))
		{
			// RW 0x85F85B: the warhead goes off at the position of the thing hit, the thing is remembered
			if (m_warhead)
			{
				Object *producer = logic.findObjectByID(obj->getProducerID());
				const ObjectID source = producer ? producer->getID() : obj->getID();
				Coord3D pos = *other->getPosition();
				DeliverNuggets(logic, source, *m_warhead, WeaponBonus(), nullptr, &pos, true, &counters.unportedNuggets);
			}
			m_hitList.push_back(other->getID());
		}
		return true;
	}
	// the ground or the end of the path: the hit (or bounce) FX and weapon
	const bool bouncing = m_altCurve != 0;
	const std::string &fx = bouncing ? d->m_groundBounceFX : d->m_groundHitFX;
	const std::string &weaponName = bouncing ? d->m_groundBounceWeapon : d->m_groundHitWeapon;
	if (!fx.empty() && fx != "None")
	{
		++counters.projectileFxUnplayed; // hashed run counter, kept (lane FX-2 changes no hash)
	}
	// lane FX-2: RW 0x85FDB7 plays GroundBounceFX (+0xA0) while bouncing, else GroundHitFX (+0x9C) through RW 0x494615 doFXPos(fx, object position, null, 0, null)
	{
		FXEvent e = FXEventLog::objectEvent(FXEvent::POSITION_FX, bouncing ? "GroundBounceFX" : "GroundHitFX", logic.getFrame(), fx, *obj);
		e.hasTransform = false;
		logic.fxEvents().emit(e);
	}
	++counters.projectileGroundHits;
	if (!weaponName.empty() && TheWeaponStore)
	{
		if (const WeaponTemplate *w = TheWeaponStore->findWeaponTemplate(weaponName))
		{
			// RW 0x85FE49 .. 0x85FE63: TheWeaponStore->createAndFireTempWeapon(weapon, projectile, projectile position) (RW 0x6CF530; lane DECOMP-1)
			ObjectWeapons::createAndFireTempWeapon(w, obj, *obj->getPosition());
		}
	}
	if (m_altCurve < d->m_bounceCount)
	{
		bounce(false);
	}
	else if (!d->m_crushStyle)
	{
		detonate();
	}
	else
	{
		landed();
	}
	return true;
}

// RW 0x85FCBB
void BezierProjectileBehavior::bounce(bool flag)
{
	Object *obj = getObject();
	const BezierProjectileBehaviorModuleData *d = m_data;
	if (!d->m_dieOnImpact && !flag)
	{
		setWakeFrame(obj, UPDATE_SLEEP_NONE);
	}
	++m_altCurve;
	++obj->logic().combat().counters().projectileBounces;
	const int n = (int)m_flightPath.size();
	if (n < 2)
	{
		landed();
		return;
	}
	const Coord3D &last = m_flightPath[(size_t)n - 1];
	const Coord3D &prev = m_flightPath[(size_t)n - 2];
	float dx = SimMath::subf32(last.x, prev.x);
	float dy = SimMath::subf32(last.y, prev.y);
	const float lenSq = SimMath::addf32(SimMath::mulf32(dy, dy), SimMath::mulf32(dx, dx));
	if (lenSq != 0.0f)
	{
		const float inv = ProjectileInvSqrt(lenSq);
		dx = SimMath::mulf32(dx, inv);
		dy = SimMath::mulf32(dy, inv);
	}
	Coord3D end;
	end.x = SimMath::addf32(SimMath::mulf32(dx, d->m_bounceDistance), obj->getPosition()->x);
	end.y = SimMath::addf32(SimMath::mulf32(dy, d->m_bounceDistance), obj->getPosition()->y);
	end.z = obj->logic().getGroundHeight(end.x, end.y);
	projectileFireAtObjectOrPosition(nullptr, &end);
}

// RW 0x85F9BE: the projectile has come to rest (a CrushStyle one after its last bounce)
void BezierProjectileBehavior::landed()
{
	Object *obj = getObject();
	const BezierProjectileBehaviorModuleData *d = m_data;
	GameLogic &logic = obj->logic();
	Coord3D pos = *obj->getPosition();
	pos.z = logic.getGroundHeight(pos.x, pos.y);
	obj->setPosition(&pos);
	m_flightPath.clear();
	m_currentFlightPathStep = 0;
	m_altCurve = 0;
	m_launcherID = INVALID_ID;
	m_hitList.clear();
	obj->setModelConditionState(kModelConditionThrownProjectile, false);
	obj->setModelConditionState(kModelConditionAboutToHit, false);
	if (d->m_dieOnImpact)
	{
		obj->setModelConditionState(kModelConditionSplatted, true);
		obj->kill(DEATH_NORMAL);
	}
	obj->setStatus((unsigned)CombatNames::statuses().noAttack, false);
	++logic.combat().counters().projectilesLanded;
	// RW 0x85FAA4: FinalStuckTime > 0 sends the object's AI the command 0x31 (RW 0x852E2A; dispatch 0x66498A -> 0x74168E: a temporary busy state 0x2A for FinalStuckTime
	// frames); retail skips the dispatch for an object without AI. That AI command is not ported, and FinalStuckTime == 0 sends a message (RW 0x5E821A, type 5) that is not
	// read (S-363). A landed object leaves through its own modules (a LifetimeUpdate started at creation)
}

// RW 0x85F08A
void BezierProjectileBehavior::detonate()
{
	if (m_hasDetonated)
	{
		return;
	}
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	CombatState::Counters &counters = logic.combat().counters();
	Object *victim = m_victimID != INVALID_ID ? logic.findObjectByID(m_victimID) : nullptr;
	if (m_data && m_data->m_postLandingEmotion != -1 && m_data->m_postLandingStateTime != 0 && victim)
	{
		++counters.projectileEmotionsUnported; // the units near the landing spot react (RW 0x85F0C7..0x85F19D): not ported, S-363
	}
	if (m_warhead)
	{
		Object *producer = logic.findObjectByID(obj->getProducerID());
		const ObjectID source = producer ? producer->getID() : obj->getID();
		if (victim && m_warhead->m_hitStoredTarget)
		{
			// RW 0x85F1E0 .. 0x85F1F7: RW 0x6CF590 createAndFireTempWeapon(warhead, source, victim): the full temporary weapon at the victim (lane DECOMP-1)
			ObjectWeapons::createAndFireTempWeaponAt(m_warhead, producer ? producer : obj, *victim);
		}
		else
		{
			// RW 0x6CF4D6 handleProjectileDetonation(warhead, &origin, source, &position, bonus)
			Coord3D pos = *obj->getPosition();
			DeliverNuggets(logic, source, *m_warhead, WeaponBonus(), nullptr, &pos, true, &counters.unportedNuggets);
		}
		if (m_data && m_data->m_detonateCallsKill)
		{
			obj->kill(DEATH_DETONATED);
		}
		else
		{
			logic.destroyObject(obj);
		}
	}
	else
	{
		obj->kill(DEATH_DETONATED);
	}
	m_hasDetonated = true;
	obj->setStatus(OBJECT_STATUS_NO_COLLISIONS, true);
	++counters.projectilesDetonated;
}

bool BezierProjectileBehavior::projectilePendingPositionOfFrame(unsigned frame, Coord3D &out) const
{
	if (!m_pendingValid || m_pendingFrame != frame)
	{
		return false;
	}
	out = m_pendingPosition;
	return true;
}

ProjectileUpdateInterface::ClientInfo BezierProjectileBehavior::projectileClientInfo() const
{
	ClientInfo c;
	c.fireFrame = m_fireFrame;
	c.launcher = m_launcherID;
	c.end = m_flightPathEnd;
	c.segments = m_flightPathSegments;
	return c;
}

// RW 0x85E54D (slot 5): kill, damage type 8 (UNRESISTABLE), death 0
void BezierProjectileBehavior::projectileNowJammed()
{
	getObject()->kill(DEATH_NORMAL);
}

void BezierProjectileBehavior::crc(StateHasher &h) const
{
	UpdateModule::crc(h);
	h.addU32(m_launcherID);
	h.addFloat(m_originPos.x);
	h.addFloat(m_originPos.y);
	h.addFloat(m_originPos.z);
	h.addU32(m_victimID);
	h.addString(m_weapon ? m_weapon->getName() : std::string());
	h.addString(m_warhead ? m_warhead->getName() : std::string());
	h.addU32((std::uint32_t)m_flightPath.size());
	for (const Coord3D &p : m_flightPath)
	{
		h.addFloat(p.x);
		h.addFloat(p.y);
		h.addFloat(p.z);
	}
	h.addFloat(m_flightPathStart.x);
	h.addFloat(m_flightPathStart.y);
	h.addFloat(m_flightPathStart.z);
	h.addFloat(m_flightPathEnd.x);
	h.addFloat(m_flightPathEnd.y);
	h.addFloat(m_flightPathEnd.z);
	h.addFloat(m_flightPathSpeed);
	h.addI32(m_flightPathSegments);
	h.addI32(m_currentFlightPathStep);
	h.addU32(m_bonusFlags);
	h.addI32(m_altCurve);
	h.addU32((std::uint32_t)m_hitList.size());
	for (ObjectID id : m_hitList)
	{
		h.addU32(id);
	}
	h.addBool(m_hasDetonated);
	h.addFloat(m_heightScale);
	h.addFloat(m_pendingPosition.x); // lane PROJ-2
	h.addFloat(m_pendingPosition.y);
	h.addFloat(m_pendingPosition.z);
	h.addU32(m_pendingFrame);
	h.addBool(m_pendingValid);
	h.addU32(m_fireFrame);
}

std::vector<std::string> ProjectileModules::stops()
{
	return {
		"[S-360] projectile launch: RW 0x6CB490 -> 0x6CAB85 is ported (lane RENDER-2): the launch bone of the launcher's drawable (RW 0x6756A1 -> 0x4C34A2, a pristine pose of the "
		"model condition state's data, never the shown animation frame, computed from logic state and template data without a drawable) composed with the launcher's transform (RW 0x70BCE7); "
		"without a ProjectileLaunchOffsets provider the bone is the identity, counted as `launches without bones` (a GameLogic run without the W3D assets has no provider: stop S-460); the garrison branch (status INSIDE_GARRISON: the container's drawable and transform) and the turret composition (no AI turrets) are not ported; a ProjectileNugget "
		"with ProjectileFilterInContainer, WeaponLaunchBoneSlotOverride beyond the slot and ProjectileStreamName (RW 0x74094C / 0x7408A8) are not ported",
		"[S-361] flight path numerics: BezierSegment, BezFwdIterator and the length approximation follow RW 0x960E8D .. 0x9612A3 / 0x9D6A35 operation by operation; D3DXVec4Transform is an import of the D3DX DLL "
		"(not in game.dat), evaluated left to right as float32 products and sums",
		"[S-362] aim and terrain: the aim point of a victim is its geometry centre (RW 0x6CB85A -> 0x690BD2 -> 0xAD30E0 picks aim points of the victim's geometry by a seed; the weapon's aim bone is a "
		"drawable query) and the seed is drawn from the logic random (Weapon.cpp:1749) like retail; the highest terrain along the line is sampled over partition cells of 40 on a 3x3 lattice (ZH "
		"estimateTerrainExtremesAlongLine; RW's own routine, slot 0x40 of the terrain object RW 0xDE4690, was not read); only the ground layer exists (no bridges)",
		"[S-363] projectile behaviour: the victim proximity test of RW 0x85F737 is ported as the binary has it (the length of the victim's WORLD position against radius + speed: it only fires near the map "
		"origin); nothing calls projectileHandleCollision(other) in flight (arrows are NO_COLLIDE; a stone needs the physics / collide dispatch), so a CrushStyle projectile and the GeometryInfo back-off of "
		"RW 0x85EA5F are reached only by direct calls; GarrisonHitKill, the Pre / PostLanding emotions and TumbleRandomly are parsed and counted, not executed (InvisibleFrames / FadeInTime and the drawable look-ahead point: lane PROJ-2, S-1001); "
		"FinalStuckTime > 0 sends the object's AI command 0x31 (RW 0x852E2A -> 0x66498A -> 0x74168E: a temporary busy state 0x2A; skipped without AI), not ported; FinalStuckTime 0 sends a message (RW 0x5E821A) that is not read; the hit and bounce FX lists are counted, not played",
		"[S-364] warhead: the detonation delivers the warhead's nuggets through DeliverNuggets (RW 0x6CF590 / 0x6CF4D6 create a temporary Weapon: its own HitPercentage / scatter / dodge rolls and the weapon "
		"bonus flags are not applied), the ProjectileCollidesWith test (RW 0x6CC79C) is not ported",
		"[S-1000] siege launch (lane PROJ-2): the WeaponStatusHelper runs RW 0x68E197 every frame in the FINAL update phase (RW 0x68D19A, 0x8311B1) and the fire state sets the "
		"current slot's FIRING conditions right before the shot (RW 0x74C4A7 -> 0x69036C), so the launch bone is the attack state's pose at FrameForPristineBonePositions; the "
		"launch takes the first two path points at once (RW 0x85EF34). Not ported: RW 0x68E197 / 0x69036C change the conditions only for an object with a drawable (this "
		"port: every object that has weapons), the drawable's clip hint RW 0x671913, the partition refresh RW 0x68C7E9 between the two launch steps (this port's partition reads "
		"the current position)",
		"[S-365] projectile drawing: an arrow object draws through W3DStreakDraw (lane RENDER-1: the trail of RW 0x4CF884 with the template's Length / Width / NumSegments / Color / "
		"Texture, drawn by the client's GameWorld; the ribbon geometry and the weather textures are stop S-391); stones and bolts are drawn by their own model draw",
	};
}
