// OpenBFME. GPL-3.0.
//
// The live Weapon, the range checks, the FiringTracker and the AI wait decisions. See GameLogic/WeaponState.h for the target facts.
// Lane WEAPON-1. Numerics (the lockstep contract): every float operation goes through the shared facade in Common/NumericState.h: the SSE steps as
// NumericState::sse* calls in the instruction order, the x87 steps as chained WIDE pc24*W results narrowed with fstpDword only where the retail
// instruction sequence stores, conversions with the retail results (ftol2Low32 = _ftol2, fistp32, cvttss2si, including invalid input), and floor /
// ceil / fabs / sqrt without libm. Frame arithmetic is unsigned 32 bit with defined wrapping.

#include "GameLogic/WeaponState.h"
#include "Common/StateHash.h"

#include "Common/NumericState.h"

#include <cstdlib>
#include <cstring>

namespace
{
const float kPi = 3.14159274f; // RW 0xBDD388 (+pi), 0xBDD390 (-pi)

const char *const kWeaponCpp = "Weapon.cpp"; // the retail file string (RW 0xC18320); only the line numbers matter for the call log

using namespace NumericState;

// the signed reading of a 32-bit word: a bit reinterpretation (the retail code reads the same dword as signed); a C++ cast of an out-of-range unsigned
// value is implementation-defined before C++20
std::int32_t asInt32(std::uint32_t v)
{
	std::int32_t r;
	std::memcpy(&r, &v, sizeof(r));
	return r;
}

// a 32-bit two's complement add with defined wrapping (the retail add / inc / dec instructions)
int wrapAdd(int a, int b)
{
	return asInt32((std::uint32_t)a + (std::uint32_t)b);
}
}

// =============================================================================================================================
// Weapon
// =============================================================================================================================
Weapon::Weapon(const WeaponTemplate *tmpl, int slot, std::uint32_t currentFrame)
	: m_template(tmpl)
	, m_slot(slot)
{
	m_numShotsForCurBarrel = tmpl ? tmpl->m_shotsPerBarrel : 1;
	m_suspendFXFrame = tmpl ? currentFrame + tmpl->m_suspendFXDelay : 0;
	m_pitchLimited = tmpl && (tmpl->m_minTargetPitch > -kPi || tmpl->m_maxTargetPitch < kPi);
}

Weapon::Weapon(const Weapon &that)
	: m_template(that.m_template)
	, m_ownerID(that.m_ownerID)
	, m_slot(that.m_slot)
{
	m_numShotsForCurBarrel = m_template ? m_template->m_shotsPerBarrel : 1;
	m_suspendFXFrame = that.m_suspendFXFrame;
	m_pitchLimited = m_template && (m_template->m_minTargetPitch > -kPi || m_template->m_maxTargetPitch < kPi);
}

Weapon &Weapon::operator=(const Weapon &that)
{
	if (this != &that)
	{
		// RW 0x6CA19F: the template, owner, slot and suspend-FX frame are copied; the timers, ammo and status are reset
		m_template = that.m_template;
		m_ownerID = that.m_ownerID;
		m_slot = that.m_slot;
		m_status = WEAPON_OUT_OF_AMMO;
		m_ammoInClip = 0;
		m_whenWeCanFireAgain = 0;
		m_whenPreAttackFinished = 0;
		m_whenFiringEnds = 0;
		m_followThruEnd = 0;
		m_timerStart = 0;
		m_lastFireFrame = 0;
		m_suspendFXFrame = that.m_suspendFXFrame;
		m_maxShotCount = 0x7FFFFFFF;
		m_curBarrel = 0;
		m_numShotsForCurBarrel = m_template ? m_template->m_shotsPerBarrel : 1;
		m_scatterTargets.clear();
		m_pitchLimited = m_template && (m_template->m_minTargetPitch > -kPi || m_template->m_maxTargetPitch < kPi);
		m_leechRangeDeadline = 0;
		m_linearTargetCursor = 0;
		m_preAttackJitter = 0;
	}
	return *this;
}

void Weapon::rebuildScatterTargets()
{
	m_scatterTargets.clear();
	if (m_template)
	{
		for (size_t i = 0; i < m_template->m_scatterTargets.size(); ++i)
		{
			m_scatterTargets.push_back((int)i);
		}
	}
}

// RW 0x6CD298
unsigned Weapon::getRemainingAmmo(WeaponHost &host, bool countReloadingAsEmpty) const
{
	if (m_template->m_projectileFilterInContainer.flag)
	{
		unsigned count = 0;
		if (host.containedAmmo(m_template->m_projectileFilterInContainer, count))
		{
			return count;
		}
	}
	if (countReloadingAsEmpty && getStatus(host, nullptr) == WEAPON_RELOADING_CLIP)
	{
		return 0;
	}
	return m_ammoInClip;
}

// RW 0x6CD142
int Weapon::getStatus(WeaponHost &host, bool *valid) const
{
	const std::uint32_t now = host.currentFrame();
	if (now < m_whenPreAttackFinished)
	{
		if (valid)
		{
			*valid = false;
		}
		return WEAPON_PRE_ATTACK;
	}
	if (now < m_whenFiringEnds)
	{
		if (valid)
		{
			*valid = false;
		}
		return WEAPON_FIRING;
	}
	const WeaponTemplate &t = *m_template;
	const bool filterValid = t.m_projectileFilterInContainer.flag;
	if (asInt32(t.m_idleAfterFiringDelay) >= 0)
	{
		if (now < m_whenWeCanFireAgain && !filterValid)
		{
			return m_status;
		}
		if (getRemainingAmmo(host, false) > 0)
		{
			return WEAPON_READY_TO_FIRE;
		}
		if (now < m_whenWeCanFireAgain)
		{
			return WEAPON_OUT_OF_AMMO;
		}
		if (filterValid && host.ownerHasContainAmmo(t.m_projectileFilterInContainer))
		{
			return WEAPON_READY_TO_FIRE;
		}
		return WEAPON_OUT_OF_AMMO;
	}
	if (now < m_whenWeCanFireAgain && !filterValid)
	{
		return m_status;
	}
	return getRemainingAmmo(host, false) > 0 ? WEAPON_READY_TO_FIRE : WEAPON_OUT_OF_AMMO;
}

// RW 0x6CDCE7 / 0x6CA232
int Weapon::getStatusWriteBack(WeaponHost &host)
{
	bool valid = true;
	const int status = getStatus(host, &valid);
	if (valid && m_status != status)
	{
		m_status = status;
	}
	return status;
}

// RW 0x6CDB73
float Weapon::getPercentReadyToFire(WeaponHost &host) const
{
	const int status = getStatus(host, nullptr);
	if (status == WEAPON_READY_TO_FIRE)
	{
		return 1.0f;
	}
	if (status == WEAPON_OUT_OF_AMMO || status == WEAPON_PRE_ATTACK || status > WEAPON_FIRING)
	{
		return 0.0f;
	}
	const std::uint32_t now = host.currentFrame();
	if (now >= m_whenWeCanFireAgain)
	{
		return 1.0f;
	}
	const std::uint32_t total = m_whenWeCanFireAgain - m_timerStart;
	if (total == 0)
	{
		return 1.0f;
	}
	const std::uint32_t progress = total - m_whenWeCanFireAgain + now; // = now - timerStart
	if (progress >= total)
	{
		return 1.0f;
	}
	return fstpDword(pc24DivW(fildU32(progress), fildU32(total))); // fild; fild; fdivp; the float result
}

// RW 0x6CA7AC
void Weapon::computeBonus(WeaponHost &host, std::uint32_t extraFlags, WeaponBonus &bonus) const
{
	ComputeWeaponBonus(host.weaponBonusConditionMask(), extraFlags, host.globalWeaponBonusSet(), *m_template, bonus);
}

// RW 0x6CA066: v = (min == max) ? min : GameLogicRandomValue(min, max) [Weapon.cpp:988]; floor(v / (rof * attr)) at PC24
unsigned Weapon::getDelayBetweenShots(const WeaponTemplate &t, const WeaponBonus &bonus, float rateOfFireAttribute, GameLogicRandom &rng)
{
	int v;
	if (t.m_delayBetweenShotsMin == t.m_delayBetweenShotsMax)
	{
		v = asInt32(t.m_delayBetweenShotsMin);
	}
	else
	{
		v = rng.getValue(asInt32(t.m_delayBetweenShotsMin), asInt32(t.m_delayBetweenShotsMax), kWeaponCpp, 988);
	}
	// fild v; fld rof; fmul attr; fdivp; fstp qword; the CRT floor; fstp dword; fld; fistp dword (RW 0x6CA066): registers stay wide until the qword store
	const double m = pc24MulW((double)bonus.m_field[WEAPONBONUS_RATE_OF_FIRE], (double)rateOfFireAttribute);
	const double q = pc24DivW((double)v, m);
	return (unsigned)fistp32((double)fstpDword(floorD(q)));
}

// RW 0x6CA0C0: v = (min == max) ? min : GameLogicRandomValue(min, max) [Weapon.cpp:1006]; v -= v % 3; floor(v / rof)
unsigned Weapon::getClipReloadTime(const WeaponTemplate &t, const WeaponBonus &bonus, GameLogicRandom &rng)
{
	int v;
	if (t.m_clipReloadMin == t.m_clipReloadMax)
	{
		v = asInt32(t.m_clipReloadMin);
	}
	else
	{
		v = rng.getValue(asInt32(t.m_clipReloadMin), asInt32(t.m_clipReloadMax), kWeaponCpp, 1006);
	}
	v -= v % 3; // the frames round DOWN to a multiple of 3 before the rate division
	const double q = pc24DivW((double)v, (double)bonus.m_field[WEAPONBONUS_RATE_OF_FIRE]); // fild v; fdiv rof (RW 0x6CA0C0)
	return (unsigned)fistp32((double)fstpDword(floorD(q)));
}

// RW 0x6CA123 / 0x6CDD10: cvtsi2ss, mulss, cvttss2si
int Weapon::getPreAttackDelayScaled(const WeaponTemplate &t, const WeaponBonus &bonus)
{
	const float base = sseFromInt32(asInt32(t.m_preAttackDelay));
	const float scaled = sseMul(base, bonus.m_field[WEAPONBONUS_PRE_ATTACK]);
	return cvttss2si(scaled);
}

// RW 0x6CDD10: the pre-attack delay the weapon would wait now, from the stored jitter (+0x58), no RNG draw. PER_CLIP: none while the clip is not full;
// PER_ATTACK: none while the tracker counts shots at the target (RW 0x68C54E -> 0x8E2E12; a null victim AND a null position answer 0); PER_POSITION: none
// while the owner stands where it last fired. Otherwise (int)(PreAttackDelay * bonus[PRE_ATTACK]) + jitter (RW 0x6CDDA1 .. 0x6CDDC1, cvttss2si then add).
int Weapon::getPreAttackDelay(WeaponHost &host, const WeaponShotTarget *target) const
{
	const WeaponTemplate &t = *m_template;
	switch (t.m_preAttackType)
	{
		case PREFIRE_PER_CLIP:
			if (asInt32(t.m_clipSize) > 0 && getRemainingAmmo(host, false) < (unsigned)t.m_clipSize)
			{
				return 0;
			}
			break;
		case PREFIRE_PER_ATTACK:
			if (target && host.firingTrackerShotsAtTarget(target->hasVictim, target->victimID, target->victimPosition) > 0)
			{
				return 0;
			}
			break;
		case PREFIRE_PER_POSITION:
			if (host.ownerPositionMatchesLastShot())
			{
				return 0;
			}
			break;
		default:
			break;
	}
	WeaponBonus bonus;
	computeBonus(host, 0, bonus);
	return wrapAdd(getPreAttackDelayScaled(t, bonus), m_preAttackJitter); // the jitter is not scaled
}

// RW 0x6CE95D
int Weapon::preFireWeapon(WeaponHost &host, const WeaponShotTarget &target)
{
	const WeaponTemplate &t = *m_template;
	// 1. the jitter is drawn on EVERY call, before any other test
	if (t.m_preAttackRandomAmount != 0)
	{
		m_preAttackJitter = host.logicRandom().getValue(0, asInt32(t.m_preAttackRandomAmount), kWeaponCpp, 3644);
	}
	else
	{
		m_preAttackJitter = 0;
	}
	// 2. getPreAttackDelay (RW 0x6CDD10)
	const int delay = getPreAttackDelay(host, &target);
	if (delay <= 0)
	{
		return delay;
	}
	// 3-7
	const std::uint32_t now = host.currentFrame();
	m_status = WEAPON_PRE_ATTACK;
	WeaponBonus bonus;
	computeBonus(host, 0, bonus);
	m_whenPreAttackFinished = now + (std::uint32_t)delay;
	if (asInt32(t.m_firingDuration) > 0)
	{
		m_followThruEnd = now + t.m_firingDuration + (std::uint32_t)delay; // unsigned: wraps
	}
	if (t.m_leechRangeWeapon)
	{
		const double product = pc24MulW((double)asInt32(t.m_preAttackDelay), (double)bonus.m_field[WEAPONBONUS_PRE_ATTACK]); // fild; fmul; _ftol2
		m_leechRangeDeadline = ftol2Low32(product) + now + t.m_firingDuration;
	}
	return delay;
}

// RW 0x6CE8B9
void Weapon::reloadWithBonus(WeaponHost &host, const WeaponBonus &bonus, bool loadInstantly)
{
	const WeaponTemplate &t = *m_template;
	m_ammoInClip = (unsigned)t.m_clipSize;
	if (getRemainingAmmo(host, false) == 0)
	{
		m_ammoInClip = 0x7FFFFFFFu; // ClipSize 0 = unlimited
	}
	m_status = WEAPON_RELOADING_CLIP;
	const unsigned reloadFrames = loadInstantly ? 0u : getClipReloadTime(t, bonus, host.logicRandom());
	const float f = sseFromInt32(asInt32(reloadFrames)); // cvtsi2ss
	const std::uint32_t now = host.currentFrame();
	m_timerStart = now;
	m_whenWeCanFireAgain = ftol2Low32(pc24AddW(fildU32(now), (double)f)); // fild now (+2^32); fadd f; _ftol2: the low word
	if (f > 0.0f && t.m_holdDuringReload)
	{
		host.setOwnerDisabledUntil(m_whenWeCanFireAgain);
	}
	rebuildScatterTargets();
}

// RW 0x6CEE0F
void Weapon::loadAmmoNow(WeaponHost &host)
{
	WeaponBonus bonus;
	computeBonus(host, 0, bonus);
	reloadWithBonus(host, bonus, true);
}

// RW 0x6CEE4C
void Weapon::reloadAmmo(WeaponHost &host)
{
	WeaponBonus bonus;
	computeBonus(host, 0, bonus);
	reloadWithBonus(host, bonus, false);
}

// RW 0x6CEEE3
void Weapon::setClipPercentFull(WeaponHost &host, float percent, bool allowReduction)
{
	const WeaponTemplate &t = *m_template;
	if (t.m_clipSize == 0)
	{
		return;
	}
	const double product = pc24MulW((double)t.m_clipSize, (double)percent); // fild; fmul; fstp qword; floor; fstp dword; fld; fistp
	const unsigned n = (unsigned)fistp32((double)fstpDword(floorD(product)));
	const unsigned current = getRemainingAmmo(host, false);
	if (n > current || (allowReduction && n < current))
	{
		m_ammoInClip = n;
		setStatus(getRemainingAmmo(host, false) != 0 ? 1 : 0); // sic (RW 0x6CEF46..0x6CEF4F): OUT_OF_AMMO when it HAS ammo, as in ZH
		const std::uint32_t now = host.currentFrame();
		m_timerStart = now;
		m_whenWeCanFireAgain = now;
		rebuildScatterTargets();
	}
}

// RW 0x6CEF6D
bool Weapon::privateFireWeapon(WeaponHost &host, WeaponDeliverer &out, const FireArgs &args)
{
	const WeaponTemplate *tp = m_template;
	WeaponBonus bonus;
	computeBonus(host, args.extraBonusFlags, bonus); // 1
	if (!tp)
	{
		return false; // 2
	}
	const WeaponTemplate &t = *tp;
	if (t.m_requestAssistRange != 0.0f && args.target.hasVictim) // 3
	{
		out.requestAssistance(args.target);
	}
	if (t.m_leechRangeWeapon) // 4
	{
		const std::uint32_t frames = asInt32(t.m_firingDuration) >= 1 ? t.m_firingDuration : 1u;
		m_leechRangeDeadline = host.currentFrame() + frames;
	}
	if (getStatusWriteBack(host) != WEAPON_READY_TO_FIRE) // 5
	{
		return false;
	}
	// 6: the projectile stream object is the host's
	const std::uint32_t now = host.currentFrame();
	const float rof = host.rateOfFireAttributeProduct(); // 7
	const unsigned delay = getDelayBetweenShots(t, bonus, rof, host.logicRandom()); // 8: drawn BEFORE the ammo test
	if (getRemainingAmmo(host, false) == 0) // 9
	{
		return false;
	}
	if (m_curBarrel >= host.barrelCount(m_slot)) // 10
	{
		m_curBarrel = 0;
		m_numShotsForCurBarrel = t.m_shotsPerBarrel;
	}
	// RW 0x6CF111 / 0x6CF259: argument 6 and the weapon (its + 0x50 leech-range deadline, set by step 4) go to fireWeaponTemplate's range gate
	WeaponFireGate gate;
	gate.ignoreRanges = args.ignoreRanges;
	gate.leechRangeDeadline = m_leechRangeDeadline;
	if (args.isProjectileDetonation) // 11
	{
		out.fireProjectileDetonation(bonus, args.target);
	}
	else if (!t.m_linearTargets.empty()) // 12a
	{
		const size_t n = t.m_linearTargets.size();
		for (size_t i = 0; i < n; ++i)
		{
			const size_t cursor = m_linearTargetCursor % n;
			const WeaponTemplate::LinearTarget &lt = t.m_linearTargets[cursor];
			const Coord3D pos = host.linearTargetPosition(lt.x, lt.y);
			if (!(t.m_ignoreLinearFirstTarget && cursor == 0))
			{
				WeaponShotTarget shot;
				shot.hasVictim = false;
				shot.victimPosition = pos;
				out.fireWeaponTemplate(bonus, m_curBarrel, shot, false, gate);
			}
			m_linearTargetCursor = (unsigned)((cursor + 1) % n);
			if (lt.t != 0)
			{
				break;
			}
		}
	}
	else if (!m_scatterTargets.empty()) // 12b
	{
		const Coord3D p = args.target.victimPosition;
		const int pick = host.logicRandom().getValue(0, (int)m_scatterTargets.size() - 1, kWeaponCpp, 3261);
		const Coord2D off = t.m_scatterTargets[(size_t)m_scatterTargets[(size_t)pick]];
		WeaponShotTarget shot;
		shot.hasVictim = false;
		const float dx = sseMul(off.x, t.m_scatterTargetScalar);
		const float dy = sseMul(off.y, t.m_scatterTargetScalar);
		shot.victimPosition.x = sseAdd(p.x, dx);
		shot.victimPosition.y = sseAdd(p.y, dy);
		shot.victimPosition.z = host.groundHeightAt(shot.victimPosition.x, shot.victimPosition.y);
		m_scatterTargets[(size_t)pick] = m_scatterTargets.back();
		m_scatterTargets.pop_back();
		out.fireWeaponTemplate(bonus, m_curBarrel, shot, true, gate);
	}
	else // 12c
	{
		out.fireWeaponTemplate(bonus, m_curBarrel, args.target, false, gate);
	}
	// 13: the tail (32-bit wrapping counters, as the retail dec / inc instructions)
	--m_ammoInClip;
	m_maxShotCount = wrapAdd(m_maxShotCount, -1);
	m_numShotsForCurBarrel = wrapAdd(m_numShotsForCurBarrel, -1);
	m_lastFireFrame = now;
	if (m_numShotsForCurBarrel <= 0)
	{
		m_curBarrel = wrapAdd(m_curBarrel, 1);
		m_numShotsForCurBarrel = t.m_shotsPerBarrel;
	}
	if (getRemainingAmmo(host, false) == 0)
	{
		if (t.m_autoReloadsClip == AUTO_RELOAD)
		{
			reloadAmmo(host); // RNG [1006] iff ClipReloadTime min != max
			return true;
		}
		m_status = WEAPON_OUT_OF_AMMO;
		if (!t.m_projectileFilterInContainer.flag)
		{
			m_whenWeCanFireAgain = 0x7FFFFFFFu;
		}
		return false;
	}
	m_status = WEAPON_BETWEEN_FIRING_SHOTS;
	m_whenWeCanFireAgain = delay + now;
	m_timerStart = now;
	if (asInt32(t.m_firingDuration) > 0)
	{
		m_whenFiringEnds = now + t.m_firingDuration;
	}
	return false;
}

// =============================================================================================================================
// Range and geometry
// =============================================================================================================================
// RW 0x6634BF (disassembled): fld a.x; fsub b.x; fld a.y; fsub b.y; dy*dy + dx*dx stay in the register, fstp qword, the CRT sqrt (PC24), fsub rA, fsub
// rB (all wide), fst dword (the float d), then the SSE d*d when 0 <= the WIDE d (an unordered compare takes the same branch), else 0.0f
static float edgeDistanceSquared(double dxWide, double dyWide, const float *radii, int radiusCount)
{
	const double dy2 = pc24MulW(dyWide, dyWide);
	const double dx2 = pc24MulW(dxWide, dxWide);
	const double sum = pc24AddW(dy2, dx2);
	double edge = sqrtPC24(sum);
	for (int i = 0; i < radiusCount; ++i)
	{
		edge = pc24SubW(edge, (double)radii[i]);
	}
	const float d = fstpDword(edge);
	if (edge < 0.0)
	{
		return 0.0f;
	}
	return sseMul(d, d);
}

float CircleDistanceSquared(const Coord3D &a, float radiusA, const Coord3D &b, float radiusB)
{
	const float radii[2] = { radiusA, radiusB };
	return edgeDistanceSquared(pc24SubW((double)a.x, (double)b.x), pc24SubW((double)a.y, (double)b.y), radii, 2);
}

// RW 0x6CA525
float CircleDistanceSquaredToPoint(const Coord3D &a, float radiusA, const Coord3D &b)
{
	const float radii[1] = { radiusA };
	return edgeDistanceSquared(pc24SubW((double)a.x, (double)b.x), pc24SubW((double)a.y, (double)b.y), radii, 1);
}

namespace
{
const float kUndersize = 2.5f; // RW 0xBE5AE8 (the pathfind cell 10 * 0.25)

float objectDistanceSquared(WeaponRangeHost &host, const RangeSubject &a, const Coord3D &posA, const RangeSubject &b, const Coord3D &posB)
{
	if (a.isBox != b.isBox) // exactly one BOX: RW 0x68F430
	{
		return host.boxDistanceSquared(a, b);
	}
	return CircleDistanceSquared(posA, a.boundingCircleRadius, posB, b.boundingCircleRadius);
}

// fld m; fmul st, st (the x87 square of a range, wide)
double squareWide(float v)
{
	return pc24MulW((double)v, (double)v);
}
}

// RW 0x6C9F5B (SSE)
float WeaponTemplateRangeBase(const WeaponTemplate &t, const WeaponBonus &bonus, float dz)
{
	float r = sseMul(bonus.m_field[WEAPONBONUS_RANGE], t.m_attackRange);
	r = sseSub(r, kUndersize);
	if (!(sseSub(0.0f, dz) < t.m_rangeBonusMinHeight))
	{
		const float height = sseAdd(t.m_rangeBonusMinHeight, dz);
		const float perFoot = sseMul(height, t.m_rangeBonusPerFoot);
		const float extra = sseSub(t.m_rangeBonus, perFoot);
		r = sseAdd(extra, r);
	}
	if (t.m_restrictedHeightRange > 0.0f && absD((double)dz) > (double)t.m_restrictedHeightRange)
	{
		r = 0.0f;
	}
	if (r < 0.0f)
	{
		r = 0.0f;
	}
	return r;
}

// RW 0x6CA03E
float WeaponTemplateMinimumRange(const WeaponTemplate &t)
{
	const float r = sseSub(t.m_minimumAttackRange, kUndersize);
	return r > 0.0f ? r : 0.0f;
}

// RW 0x6CA61F
float WeaponRangeScale(WeaponRangeHost &host, const RangeSubject &source)
{
	float scale = 1.0f;
	float sum = 0.0f;
	if (host.rangeAttributeSum(sum))
	{
		scale = sseAdd(sum, 1.0f);
	}
	const float garrison = host.garrisonRangeScale();
	if (garrison >= 0.0f && source.insideGarrison)
	{
		scale = sseMul(scale, garrison);
	}
	return scale;
}

// RW 0x6CA69A
float WeaponGetAttackRange(const WeaponTemplate &t, const WeaponBonus &bonus, WeaponRangeHost &host, const RangeSubject &source, float dz)
{
	const float base = WeaponTemplateRangeBase(t, bonus, dz);
	const float scale = WeaponRangeScale(host, source);
	float r = sseMul(scale, base);
	if (scale > 1.0f && source.insideGarrison)
	{
		const float cap = host.garrisonRangeCap(source);
		if (cap < r)
		{
			r = cap;
		}
	}
	return r;
}

// RW 0x6CA935 -> 0x6CA701: the attribute / garrison scale is applied a second time
float WeaponGetAttackRangeNoTarget(const WeaponTemplate &t, const WeaponBonus &bonus, WeaponRangeHost &host, const RangeSubject &source)
{
	const float dz = sseSub(0.0f, source.position.z);
	const float inner = WeaponGetAttackRange(t, bonus, host, source, dz);
	return sseMul(inner, WeaponRangeScale(host, source));
}

// RW 0x6CA974 (x87 fadd of the two radii, stored once as the returned float)
float WeaponGetAttackDistance(const WeaponTemplate &t, const WeaponBonus &bonus, WeaponRangeHost &host, const RangeSubject &source, const RangeSubject *victim, const Coord3D *position)
{
	if (victim)
	{
		const float range = WeaponGetAttackRange(t, bonus, host, source, sseSub(victim->position.z, source.position.z));
		const double sum = pc24AddW(pc24AddW((double)range, (double)source.boundingSphereRadius), (double)victim->boundingSphereRadius);
		return fstpDword(sum);
	}
	if (position)
	{
		return WeaponGetAttackRange(t, bonus, host, source, sseSub(position->z, source.position.z));
	}
	return WeaponGetAttackRange(t, bonus, host, source, 0.0f);
}

// RW 0x6CA83B (x87 fcompi of m*m against the float distance)
bool WeaponIsTooClose(const WeaponTemplate &t, WeaponRangeHost &host, const RangeSubject &source, const RangeSubject &victim)
{
	const float minRange = WeaponTemplateMinimumRange(t);
	if (minRange != 0.0f)
	{
		const float distance = objectDistanceSquared(host, source, source.position, victim, victim.position);
		return squareWide(minRange) > (double)distance;
	}
	return false;
}

// RW 0x6CA87A
bool WeaponIsTooCloseToPoint(const WeaponTemplate &t, const RangeSubject &source, const Coord3D &position)
{
	const float minRange = WeaponTemplateMinimumRange(t);
	if (minRange != 0.0f)
	{
		return squareWide(minRange) > (double)CircleDistanceSquaredToPoint(source.position, source.boundingCircleRadius, position);
	}
	return false;
}

// RW 0x6CC07C
bool WeaponIsWithinAttackRange(const WeaponTemplate &t, const WeaponBonus &bonus, WeaponRangeHost &host, const RangeSubject &source, const Coord3D &sourcePos,
	const RangeSubject *victim, const Coord3D &victimPos, float extra, bool checkMinRange)
{
	// 1. melee weapons
	if (t.m_meleeWeapon)
	{
		if (!victim)
		{
			return false;
		}
		if (source.contestingBuilding && victim->contestingBuilding)
		{
			return true;
		}
		if (victim->isStructure || victim->hasMeleeAI)
		{
			if (source.runningDownFromBehind)
			{
				const float limit = host.meleeRunDownLimit();
				if (squareWide(limit) > (double)objectDistanceSquared(host, source, sourcePos, *victim, victimPos))
				{
					return true;
				}
			}
			const bool ok = host.meleeReach(source, *victim);
			if (!ok || host.victimFlag11A_40())
			{
				return ok;
			}
			const int sourceLayer = host.layerOf(false);
			const int victimLayer = host.layerOf(true);
			if (sourceLayer == 1 && victimLayer >= 0x11)
			{
				return false;
			}
			if (victimLayer == 1 && sourceLayer >= 0x11)
			{
				return host.victimLayerException() ? ok : false;
			}
			return ok;
		}
	}
	// 2. the distance and the range
	float distance;
	double range; // the x87 register: the position target's `- 2.5` is not stored before the square
	const float dz = sseSub(victimPos.z, sourcePos.z);
	if (victim)
	{
		distance = objectDistanceSquared(host, source, sourcePos, *victim, victimPos);
		range = (double)WeaponGetAttackRange(t, bonus, host, source, dz);
	}
	else
	{
		distance = CircleDistanceSquaredToPoint(sourcePos, source.boundingCircleRadius, victimPos);
		range = pc24SubW((double)WeaponGetAttackRange(t, bonus, host, source, dz), (double)kUndersize);
	}
	// 3. the compare: A = range * range and the minimum range squared in the x87 register, B = extra * extra + d in SSE
	const double rangeSquared = pc24MulW(range, range);
	const float extraSquared = sseMul(extra, extra);
	const float b = sseAdd(extraSquared, distance);
	const float minRange = WeaponTemplateMinimumRange(t);
	const double minSquared = source.contestingBuilding ? 0.0 : squareWide(minRange);
	if (checkMinRange && minSquared > (double)b)
	{
		return false;
	}
	return rangeSquared >= (double)b;
}

// RW 0x6CBFF1
bool WeaponIsSourceWithGoalPositionWithinAttackRange(const WeaponTemplate &t, const WeaponBonus &bonus, WeaponRangeHost &host, const RangeSubject &source,
	const Coord3D &goalPos, const RangeSubject *victim, const Coord3D &victimPos)
{
	float distance;
	if (victim)
	{
		distance = objectDistanceSquared(host, source, goalPos, *victim, victim->position);
	}
	else
	{
		distance = CircleDistanceSquaredToPoint(goalPos, source.boundingCircleRadius, victimPos);
	}
	const float range = WeaponGetAttackRange(t, bonus, host, source, sseSub(victimPos.z, goalPos.z));
	const float minRange = WeaponTemplateMinimumRange(t);
	return squareWide(minRange) <= (double)distance && squareWide(range) >= (double)distance;
}

// RW 0x6CA9BF
bool WeaponIsWithinTargetPitch(const WeaponTemplate &t, bool pitchLimited, float sourceZ, float victimZ, float minPitch, float maxPitch)
{
	if (!pitchLimited)
	{
		return true;
	}
	if (absD((double)sseSub(victimZ, sourceZ)) < 10.0) // RW 0xBD83D8
	{
		return true;
	}
	const float tmin = t.m_minTargetPitch;
	const float tmax = t.m_maxTargetPitch;
	return (minPitch >= tmin && minPitch <= tmax) || (maxPitch >= tmin && maxPitch <= tmax) || (minPitch <= tmin && maxPitch >= tmax);
}

// =============================================================================================================================
// FiringTracker
// =============================================================================================================================
// RW 0x8E302A
void FiringTracker::reset(FiringTrackerHost &host, bool hard)
{
	const std::uint32_t bits = (1u << CONDITION_CONTINUOUS_FIRE_MEAN) | (1u << CONDITION_CONTINUOUS_FIRE_FAST);
	const std::uint32_t mask = host.weaponBonusConditionMask();
	if (hard || (mask & bits) == 0)
	{
		host.setWeaponBonusConditionMask(mask & ~bits);
		m_coastEnd = 0;
		m_lastOwnerPosition = Coord3D{};
	}
	else
	{
		host.setWeaponBonusConditionMask(mask & ~bits);
	}
	m_count = 0;
	m_victimID = 0;
}

// RW 0x8E3174
void FiringTracker::speedUp(FiringTrackerHost &host)
{
	const std::uint32_t mean = 1u << CONDITION_CONTINUOUS_FIRE_MEAN;
	const std::uint32_t fast = 1u << CONDITION_CONTINUOUS_FIRE_FAST;
	const std::uint32_t mask = host.weaponBonusConditionMask();
	if (mask & mean)
	{
		host.setWeaponBonusConditionMask((mask & ~mean) | fast);
	}
	else if (!(mask & fast))
	{
		host.setWeaponBonusConditionMask(mask | mean);
	}
}

// RW 0x8E2E12
int FiringTracker::getShotsAtTarget(bool hasVictim, unsigned victimID, const Coord3D &pos) const
{
	if (hasVictim)
	{
		return (m_positionMode == 0 && m_victimID == victimID) ? m_count : 0;
	}
	return (m_positionMode == 1 && m_victimPos.x == pos.x && m_victimPos.y == pos.y && m_victimPos.z == pos.z) ? m_count : 0;
}

// RW 0x8E326D
void FiringTracker::shotFired(FiringTrackerHost &host, const Weapon &weapon, unsigned victimID, const Coord3D &victimPos, bool hard)
{
	if (hard)
	{
		m_count = 1;
		reset(host, true);
		return;
	}
	const std::uint32_t now = host.currentFrame();
	const bool idShot = victimID != 0;
	if (idShot)
	{
		if (m_positionMode == 0 && m_victimID == victimID)
		{
			++m_count;
		}
		else if (m_positionMode == 0)
		{
			m_count = now < m_coastEnd ? m_count + 1 : 1;
		}
		else
		{
			m_count = 1; // mode switch
		}
		m_victimID = victimID;
		m_positionMode = 0;
	}
	else
	{
		const bool same = m_victimPos.x == victimPos.x && m_victimPos.y == victimPos.y && m_victimPos.z == victimPos.z;
		if (m_positionMode == 1 && same)
		{
			++m_count;
		}
		else if (m_positionMode == 1)
		{
			m_count = now < m_coastEnd ? m_count + 1 : 1;
		}
		else
		{
			m_count = 1;
		}
		m_victimPos = victimPos;
		m_positionMode = 1;
	}
	const WeaponTemplate &t = *weapon.getTemplate();
	if (t.m_autoReloadWhenIdle > 0)
	{
		m_autoReloadDeadline = now + t.m_autoReloadWhenIdle;
	}
	m_coastEnd = t.m_continuousFireCoastFrames != 0 ? weapon.whenWeCanFireAgain() + t.m_continuousFireCoastFrames : 0;
	m_lastShotFrame = now;
	m_lastOwnerPosition = host.ownerPosition();
	const int one = t.m_continuousFireOneShotsNeeded;
	const int two = t.m_continuousFireTwoShotsNeeded;
	const std::uint32_t mask = host.weaponBonusConditionMask();
	if (mask & (1u << CONDITION_CONTINUOUS_FIRE_MEAN))
	{
		if (m_count < one)
		{
			reset(host, false);
		}
		if (m_count > two)
		{
			speedUp(host);
		}
	}
	else if (mask & (1u << CONDITION_CONTINUOUS_FIRE_FAST))
	{
		if (m_count < two)
		{
			reset(host, false);
		}
	}
	else if (m_count > one)
	{
		speedUp(host);
	}
}

// RW 0x8E30DD
std::uint32_t FiringTracker::update(FiringTrackerHost &host)
{
	const std::uint32_t now = host.currentFrame();
	const Coord3D pos = host.ownerPosition();
	if (pos.x != m_lastOwnerPosition.x || pos.y != m_lastOwnerPosition.y || pos.z != m_lastOwnerPosition.z)
	{
		reset(host, true); // the owner moved
	}
	if (now > m_coastEnd)
	{
		m_coastEnd = now + COAST_POLL_FRAMES;
		reset(host, false);
	}
	if (m_autoReloadDeadline != 0 && now >= m_autoReloadDeadline)
	{
		host.reloadAllWeapons();
		m_autoReloadDeadline = 0;
	}
	return m_autoReloadDeadline != 0 ? 1u : 0x3FFFFFFFu;
}

// =============================================================================================================================
// AI decisions
// =============================================================================================================================
// RW 0x7479B7
WaitEnterResult WeaponWaitUntilFinishedFiringEnter(const WeaponTemplate &t, int status, bool hordeMemberOddFrame, std::uint32_t frame)
{
	WaitEnterResult r;
	if (status == WEAPON_READY_TO_FIRE)
	{
		r.state = hordeMemberOddFrame ? STATE_CONTINUE : STATE_SUCCESS;
		return r;
	}
	if (asInt32(t.m_idleAfterFiringDelay) >= 0)
	{
		r.lockWeapon = true;
	}
	if (asInt32(t.m_holdAfterFiringDelay) > 0)
	{
		r.setDisabled = true;
		r.disabledUntil = frame + t.m_holdAfterFiringDelay;
	}
	r.state = STATE_CONTINUE;
	return r;
}

// RW 0x742CEF
int WeaponWaitUntilFinishedFiringUpdate(const WeaponTemplate &t, int status, std::uint32_t lastFireFrame, std::uint32_t frame)
{
	const std::int32_t idle = asInt32(t.m_idleAfterFiringDelay);
	if (idle >= 0 && lastFireFrame + (std::uint32_t)idle > frame)
	{
		return STATE_CONTINUE;
	}
	if (status == WEAPON_FIRING)
	{
		return STATE_CONTINUE;
	}
	return idle < 0 ? STATE_SUCCESS : STATE_FAILURE;
}

// RW 0x68E197 (one slot)
int WeaponStatusCondition(const WeaponTemplate &t, const Weapon &w, int status, std::uint32_t frame, bool isAttacking, bool isFiringWeaponFlag, bool isAimingWeaponFlag)
{
	static const int kTable[6] = { 0, 0, 2, 3, 4, 1 }; // RW 0xC11F74
	const std::int32_t idle = asInt32(t.m_idleAfterFiringDelay);
	const std::uint32_t last = w.lastFireFrame();
	int cond;
	if (last != 0 && idle >= 0 && last + (std::uint32_t)idle > frame)
	{
		cond = 1;
	}
	else if (last == frame)
	{
		cond = 1;
	}
	else if (t.m_requireFollowThru && frame < w.followThruEnd() && frame > w.whenPreAttackFinished())
	{
		cond = 5;
	}
	else if (!isAttacking && !(w.whenPreAttackFinished() <= frame && frame < w.followThruEnd()) && !(t.m_holdDuringReload && status == WEAPON_RELOADING_CLIP))
	{
		cond = 0;
	}
	else
	{
		cond = (status >= 0 && status < 6) ? kTable[status] : 0;
	}
	if (status == WEAPON_READY_TO_FIRE && cond == 0 && isAttacking && (isFiringWeaponFlag || isAimingWeaponFlag))
	{
		cond = 2;
	}
	return cond;
}

// ---- the OpenBFME state hash (lane COMBAT-1) ------------------------------------------------------------------------------------
void Weapon::crc(StateHasher &h) const
{
	h.addString(m_template ? m_template->getName() : std::string());
	h.addU32(m_ownerID);
	h.addI32(m_slot);
	h.addI32(m_status);
	h.addU32(m_ammoInClip);
	h.addU32(m_whenWeCanFireAgain);
	h.addU32(m_whenPreAttackFinished);
	h.addU32(m_whenFiringEnds);
	h.addU32(m_followThruEnd);
	h.addU32(m_timerStart);
	h.addU32(m_lastFireFrame);
	h.addU32(m_suspendFXFrame);
	h.addI32(m_maxShotCount);
	h.addI32(m_curBarrel);
	h.addI32(m_numShotsForCurBarrel);
	h.addU32((std::uint32_t)m_scatterTargets.size());
	for (int v : m_scatterTargets)
	{
		h.addI32(v);
	}
	h.addBool(m_pitchLimited);
	h.addU32(m_leechRangeDeadline);
	h.addU32(m_linearTargetCursor);
	h.addI32(m_preAttackJitter);
}

void FiringTracker::crc(StateHasher &h) const
{
	h.addI32(m_count);
	h.addU32(m_victimID);
	h.addFloat(m_victimPos.x);
	h.addFloat(m_victimPos.y);
	h.addFloat(m_victimPos.z);
	h.addI32(m_positionMode);
	h.addU32(m_coastEnd);
	h.addU32(m_autoReloadDeadline);
	h.addU32(m_lastShotFrame);
	h.addFloat(m_lastOwnerPosition.x);
	h.addFloat(m_lastOwnerPosition.y);
	h.addFloat(m_lastOwnerPosition.z);
}
