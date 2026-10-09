// OpenBFME. GPL-3.0.
//
// The deterministic damage core: AdjustDamage, FillDamageInfo, the ActiveBody health arithmetic and the pending-damage countdown. See
// GameLogic/Damage.h and the spec weapons-and-damage.md for the target facts. Lane WEAPON-1.
//
// Numerics (PLAN rule 3, the lockstep contract): every float operation goes through the shared facade (Common/NumericState.h). The SSE sequences of
// the retail code are NumericState::sse* calls in the instruction order (never a bare `a * b + c`); the x87 sequences (taper-off, passenger
// fraction, delay distance) chain the WIDE pc24*W results and narrow with fstpDword only where the retail instruction sequence stores; square
// roots, floors and absolute values are the facade's (no libm).

#include "GameLogic/Damage.h"

#include "Common/NumericState.h"

#include "GameLogic/Armor.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponNugget.h"

#include <stdexcept>

// ---------------------------------------------------------------------------------------------------------------------------
// AdjustDamage (RW 0x5D893C). All steps are SSE float32.
// ---------------------------------------------------------------------------------------------------------------------------
float AdjustDamage(const ArmorTemplate *armor, const ArmorTemplate *setArmor, const DamageInfoInput &in, bool noFlank, AdjustDamageHost &host)
{
	const int type = in.m_damageType;
	if (type < 0 || type >= DAMAGE_NUM_TYPES)
	{
		throw std::out_of_range("AdjustDamage: damage type out of range");
	}
	const float amount = in.m_amount;
	if (type == DAMAGE_HEALING) // RW 0x5D8950
	{
		return amount;
	}
	using namespace NumericState;
	// step 2: the scalar of the template of the victim's CURRENT armor set entry
	float scalar = 1.0f;
	if (setArmor)
	{
		scalar = setArmor->m_damageScalar;
	}
	// step 3: the flank penalty
	float flank = 0.0f;
	if (armor && !noFlank && in.m_sourceID != 0 && host.victimFlankedByAttacker())
	{
		flank = armor->m_flankedPenalty;
	}
	// step 4: a = (1 - ((1 - s) * (1 - f))) * amount
	const float oneMinusFlank = sseSub(1.0f, flank);
	float t = sseSub(1.0f, scalar);
	t = sseMul(t, oneMinusFlank);
	t = sseSub(1.0f, t);
	float a = sseMul(t, amount);
	if (type == DAMAGE_UNRESISTABLE) // RW 0x5D8A3D: scalar and flank apply, the coefficient and the modifiers do not
	{
		return a;
	}
	// step 5: immunity
	if (host.invulnerableTo(type))
	{
		return 0.0f;
	}
	// step 6: the coefficient
	const float coefficient = armor ? armor->m_damageCoefficient[type] : 1.0f;
	float c = sseSub(1.0f, coefficient);
	c = sseMul(c, oneMinusFlank);
	c = sseSub(1.0f, c);
	a = sseMul(c, a);
	if (type == DAMAGE_FORCE) // RW 0x5D8ACC
	{
		return a;
	}
	// step 7: the ARMOR attribute modifiers, capped
	const float modifiers = host.armorModifierSum(type);
	const float cap = host.armorMaxBonus();
	const float x = (cap <= modifiers) ? cap : modifiers; // comiss cap, out; jbe
	float inner = sseSub(1.0f, x);
	float m = sseSub(1.0f, inner);
	m = sseMul(m, oneMinusFlank);
	m = sseSub(1.0f, m);
	return sseMul(m, a);
}

// ---------------------------------------------------------------------------------------------------------------------------
// FillDamageInfo (RW 0x90E28C)
// ---------------------------------------------------------------------------------------------------------------------------
bool FillDamageInfo(const DamageNugget &nugget, const WeaponTemplate &weapon, NuggetDamageHost &host, bool noFlank, const Coord3D *center, DamageInfo &out)
{
	DamageInfoInput &in = out.m_input;
	const bool hasVictim = host.hasVictim();
	float amount = nugget.m_damage;

	using namespace NumericState;
	// 1. taper-off (RW 0x90E2E8-0x90E335, x87): sqrt (PC24) stored as a float, the clamp (RW 0x40524B) returns a float, then
	// ((d - min) / (R - min)) * (Damage - TaperOff) subtracted from Damage in the register, stored once
	if (nugget.m_damageTaperOff != -1.0f && center)
	{
		float d = fstpDword(sqrtPC24((double)host.victimDistanceSqr2D(*center))); // RW 0x66137C, fstp qword, the CRT sqrt, fstp dword
		// RW 0x40524B clamp: min > d ? min : (d > R ? R : d)
		if (nugget.m_minRadius > d)
		{
			d = nugget.m_minRadius;
		}
		else if (d > nugget.m_radius)
		{
			d = nugget.m_radius;
		}
		const double num = pc24SubW((double)d, (double)nugget.m_minRadius);
		const double den = pc24SubW((double)nugget.m_radius, (double)nugget.m_minRadius);
		const double q = pc24DivW(num, den);
		const double span = pc24SubW((double)nugget.m_damage, (double)nugget.m_damageTaperOff);
		const double part = pc24MulW(q, span);
		amount = fstpDword(pc24SubW((double)nugget.m_damage, part));
	}

	// 2. PassengerProportionalAttack (RW 0x90E3A9-0x90E3E6): fild n, fidiv M stored as a float, min(q, 1.0f), mulss
	if (weapon.m_passengerProportionalAttack && host.sourceHasContain())
	{
		const int passengers = host.sourcePassengerCount();
		const int maxPassengers = (int)weapon.m_maxAttackPassengers;
		if (maxPassengers > 0)
		{
			float q = fstpDword(pc24DivW(fildU32((std::uint32_t)passengers), (double)maxPassengers));
			if (q > 1.0f)
			{
				q = 1.0f;
			}
			amount = sseMul(amount, q);
		}
	}

	// 3. the first DamageScalar whose filter allows the victim
	for (const DamageScalarEntry &entry : nugget.m_damageScalar)
	{
		if (host.filterAllowsVictim(entry.filter))
		{
			amount = sseMul(entry.scalar, amount);
			break;
		}
	}

	// 4. + DAMAGE_ADD (only when there is something to add to)
	if (nugget.m_acceptDamageAdd && amount != 0.0f)
	{
		float add = 0.0f;
		if (host.sourceAdditive(ATTRIBUTE_MODIFIER_DAMAGE_ADD, add))
		{
			amount = sseAdd(amount, add);
		}
	}

	// 5. * DAMAGE_MULT, or SPELL_DAMAGE for MAGIC (which always counts the innate attributes)
	{
		float mult = 1.0f;
		bool found;
		if (nugget.m_damageType == DAMAGE_MAGIC)
		{
			found = host.sourceMultiplicative(ATTRIBUTE_MODIFIER_SPELL_DAMAGE, true, mult);
		}
		else
		{
			found = host.sourceMultiplicative(ATTRIBUTE_MODIFIER_DAMAGE_MULT, weapon.m_useInnateAttributes, mult);
		}
		if (found)
		{
			amount = sseMul(amount, mult);
		}
	}

	// 6. the delay (RW 0x90E4F8-0x90E571): fild DelayTime (+2^32 when negative) stored as a float; then, with DamageSpeed > 0, the 3D length
	// (RW 0x403111: float32 sum of squares, CRT sqrt under PC24) / DamageSpeed + delay in the register, stored once
	float delay = fstpDword(fildU32(nugget.m_delayTime));
	if (hasVictim && nugget.m_damageSpeed > 0.0f)
	{
		const Coord3D v = host.victimPosition();
		const Coord3D s = host.sourcePosition();
		const float sum = SimMath::sumSquares3(sseSub(v.x, s.x), sseSub(v.y, s.y), sseSub(v.z, s.z));
		const double length = sqrtPC24((double)sum);
		const double travel = pc24DivW(length, (double)nugget.m_damageSpeed);
		delay = fstpDword(pc24AddW(travel, (double)delay));
	}

	// 7. flanking
	if (!noFlank && hasVictim)
	{
		if (host.victimFlankedBySource())
		{
			const float bonus = sseAdd(nugget.m_flankingBonus, 1.0f);
			amount = sseMul(bonus, amount);
		}
		if (host.sourceFlankedByVictim())
		{
			amount = sseMul(nugget.m_flankedScalar, amount);
			if (1.0f > amount) // comiss 1.0, A: the hit is dropped
			{
				return false;
			}
		}
	}

	// 8. kills
	bool kill = false;
	if (host.filterAllowsVictim(nugget.m_forceKillObjectFilter))
	{
		kill = true;
	}
	if (host.weaponSourceIsVictim() && (weapon.m_affectsMask & WEAPON_KILLS_SELF))
	{
		kill = true;
	}

	// 9. the fields
	in.m_sourceID = host.weaponSourceID();
	in.m_sourcePlayerMask = host.sourcePlayerMask();
	in.m_damageType = nugget.m_damageType;
	in.m_damageFXOverride = nugget.m_damageFXType;
	in.m_damageSubType = nugget.m_damageSubType;
	in.m_deathType = nugget.m_deathType;
	in.m_amount = amount;
	in.m_kill = kill;
	in.m_shouldPlayUnderAttackEva = weapon.m_shouldPlayUnderAttackEvaEvent;
	in.m_delay = delay;
	in.m_fxTrigger = weapon.m_fxTrigger;
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------------
// ActiveBody arithmetic
// ---------------------------------------------------------------------------------------------------------------------------
// RW 0x8C31A5
void InternalChangeHealth(BodyHealth &body, float delta)
{
	const float previous = body.health;
	float h = NumericState::sseAdd(delta, body.health);
	if (h > body.maxHealth)
	{
		h = body.maxHealth;
		body.healHistoryCleared = true;
	}
	if (0.0f > h)
	{
		h = 0.0f;
	}
	body.previousHealth = previous;
	body.health = h;
}

// RW 0x8C4AB4
void ImmortalInternalChangeHealth(BodyHealth &body, float delta)
{
	const float floorDelta = NumericState::sseSub(1.0f, body.health);
	if (floorDelta > delta)
	{
		delta = floorDelta;
	}
	InternalChangeHealth(body, delta);
}

bool ActiveBodyApplyDamage(BodyHealth &body, DamageInfo &info, float adjustedAmount, float bodyDamageScalar, bool fireCapped)
{
	float amount = adjustedAmount;
	if (info.m_input.m_damageType != DAMAGE_UNRESISTABLE) // RW 0x8C4077: mulss [esi+4]
	{
		amount = NumericState::sseMul(amount, bodyDamageScalar);
	}
	if (!(amount > 0.0f || info.m_input.m_kill)) // RW 0x8C40F0
	{
		return false;
	}
	if (fireCapped)
	{
		const float cap = NumericState::sseSub(body.health, 1.0f);
		if (amount > cap)
		{
			amount = cap;
		}
	}
	if (info.m_input.m_kill) // RW 0x8C4218: amount = the current health
	{
		amount = body.health;
	}
	const float delta = NumericState::sseSub(0.0f, amount); // xorps; subss
	InternalChangeHealth(body, delta);
	info.m_output.m_actualDamageDealt = amount;
	info.m_output.m_actualDamageClipped = NumericState::sseSub(body.previousHealth, body.health);
	return true;
}

float HighlanderClampAmount(float inputAmount, float health, int damageType)
{
	if (damageType == DAMAGE_UNRESISTABLE)
	{
		return inputAmount;
	}
	const float cap = NumericState::sseSub(health, 1.0f);
	return inputAmount > cap ? cap : inputAmount;
}

// RW 0x697EB6
bool PendingDamageDue(float &delay)
{
	delay = NumericState::sseSub(delay, 1.0f);
	return 0.0f > delay;
}
