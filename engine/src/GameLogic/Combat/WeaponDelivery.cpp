// OpenBFME. GPL-3.0.
// See GameLogic/Combat/WeaponDelivery.h for the target facts.

#include "GameLogic/Combat/WeaponDelivery.h"

#include "Common/Player.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Damage.h"
#include "GameLogic/FXEvents.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/HordeAIUpdate.h"
#include "GameLogic/ObjectFilterMatch.h"
#include "GameLogic/Object/Contain/HordeFlank.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponNugget.h"
#include "GameLogic/WeaponStores.h"

#include <cmath>

namespace
{
const char *const kWeaponCpp = "Weapon.cpp"; // the retail file string (RW 0xC18320): the call log keys on the line numbers

// the NuggetDamageHost of one (source, victim) pair
class DamageHost : public NuggetDamageHost
{
public:
	DamageHost(GameLogic &logic, ObjectID sourceId, Object *victim)
		: m_logic(logic)
		, m_sourceId(sourceId)
		, m_source(sourceId != INVALID_ID ? logic.findObjectByID(sourceId) : nullptr)
		, m_victim(victim)
	{
	}
	bool hasVictim() override { return m_victim != nullptr; }
	float victimDistanceSqr2D(const Coord3D &center) override { return CombatQueries::centerDistanceSquared2D(*m_victim->getPosition(), center); }
	Coord3D victimPosition() override { return *m_victim->getPosition(); }
	Coord3D sourcePosition() override { return m_source ? *m_source->getPosition() : Coord3D{ 0.0f, 0.0f, 0.0f }; }
	bool sourceHasContain() override { return m_source && m_source->getContain() != nullptr; }
	int sourcePassengerCount() override { return m_source && m_source->getContain() ? (int)m_source->getContain()->getContainCount() : 0; }
	bool filterAllowsVictim(const ObjectFilter &filter) override
	{
		const Player *p = m_source ? m_source->getControllingPlayer() : nullptr;
		return m_victim && ObjectFilterMatch::allows(m_logic, filter, *m_victim, p);
	}
	// lane XP-1: the source's AttributeModifierPoolUpdate (RW 0x68C818 sum / RW 0x68C82D product, name null)
	bool sourceAdditive(int type, float &out) override { return m_source && m_source->attributeModifierSum(type, nullptr, out); }
	bool sourceMultiplicative(int type, bool innate, float &out) override { return m_source && m_source->attributeModifierProduct(type, nullptr, innate, out); }
	// RW 0x90E57D / 0x90E5A6: Object::isFlankedBy (RW 0x68FB63), lane HORDE-2 (S-582)
	bool victimFlankedBySource() override { return m_source && m_victim && HordeFlank::isFlankedBy(*m_victim, *m_source); }
	bool sourceFlankedByVictim() override { return m_source && m_victim && HordeFlank::isFlankedBy(*m_source, *m_victim); }
	bool weaponSourceIsVictim() override { return m_source && m_source == m_victim; }
	std::uint32_t sourcePlayerMask() override
	{
		const Player *p = m_source ? m_source->getControllingPlayer() : nullptr;
		return p && p->getPlayerIndex() >= 0 && p->getPlayerIndex() < 32 ? (1u << p->getPlayerIndex()) : 0u;
	}
	unsigned weaponSourceID() override { return m_sourceId; }

private:
	GameLogic &m_logic;
	ObjectID m_sourceId;
	Object *m_source;
	Object *m_victim;
};

// RW 0x90D6F5 (the upgrade test of a nugget): no forbidden upgrade and every required upgrade, on the object or its player (inference on "all": the single-name case is
// what retail data uses)
bool upgradeTest(const WeaponNugget &n, const Object *source)
{
	if (n.m_requiredUpgradeNames.empty() && n.m_forbiddenUpgradeNames.empty())
	{
		return true;
	}
	if (!source)
	{
		return n.m_requiredUpgradeNames.empty();
	}
	const Player *p = source->getControllingPlayer();
	auto has = [&](const std::string &u) { return source->hasUpgrade(u) || (p && p->hasUpgradeComplete(u)); };
	for (const std::string &u : n.m_forbiddenUpgradeNames)
	{
		if (has(u))
		{
			return false;
		}
	}
	for (const std::string &u : n.m_requiredUpgradeNames)
	{
		if (!has(u))
		{
			return false;
		}
	}
	return true;
}

// RW 0x90E855 / 0x90D77C shouldDeliver (the parts that do not need the attribute pool)
bool shouldDeliver(const WeaponNugget &n, const WeaponTemplate &w, const Object *source, const Object &victim, GameLogic &logic)
{
	if (!upgradeTest(n, source))
	{
		return false;
	}
	if (!source)
	{
		return true;
	}
	const unsigned affects = w.m_affectsMask;
	if (!(affects & WEAPON_AFFECTS_SELF) && &victim == source)
	{
		return false;
	}
	const CombatNames::Kind &k = CombatNames::kinds();
	if ((affects & WEAPON_DOESNT_AFFECT_SIMILAR) && victim.getTemplate() == source->getTemplate() && source->getRelationship(victim) == ALLIES)
	{
		return false;
	}
	if (victim.isKindOf((unsigned)k.projectile) && !(affects & WEAPON_AFFECTS_PROJECTILES))
	{
		return false;
	}
	// the relationship mask (RW 0x68D7AB): ENEMIES -> 4, NEUTRAL -> 8, ALLIES -> 2
	const Relationship r = source->getRelationship(victim);
	const unsigned mask = r == ENEMIES ? WEAPON_AFFECTS_ENEMIES : r == NEUTRAL ? WEAPON_AFFECTS_NEUTRALS : WEAPON_AFFECTS_ALLIES;
	if ((affects & mask) == 0)
	{
		return false;
	}
	if (ObjectFilterMatch::isValid(&n.m_specialObjectFilter) && !ObjectFilterMatch::allows(logic, n.m_specialObjectFilter, victim, source->getControllingPlayer()))
	{
		return false;
	}
	return true;
}

// RW 0x90E683 applyToVictim: true when the hit took health (actualDamageClipped > 0)
bool applyToVictim(GameLogic &logic, ObjectID sourceId, const DamageNugget &nugget, const WeaponTemplate &weapon, Object &victim, const Coord3D *center)
{
	DamageHost host(logic, sourceId, &victim);
	DamageInfo info;
	if (!FillDamageInfo(nugget, weapon, host, false, center, info))
	{
		return false;
	}
	Object *source = sourceId != INVALID_ID ? logic.findObjectByID(sourceId) : nullptr;
	if (info.m_input.m_damageType == DAMAGE_HEALING)
	{
		victim.attemptHealing(info.m_input.m_amount, &victim);
		return false;
	}
	victim.attemptDamage(info);
	if (nugget.m_drainLife && source && !source->isKindOf((unsigned)CombatNames::kinds().structure))
	{
		const float drained = SimMath::mulf32(nugget.m_drainLifeMultiplier, info.m_output.m_actualDamageDealt);
		if (drained != 0.0f)
		{
			source->attemptHealing(drained, nullptr);
		}
	}
	return info.m_output.m_actualDamageClipped > 0.0f;
}

// RW 0x90DEF0 radiusDamage (the shape of the partition query is 3D bounding sphere: S-321 the 2D bounding circle stands in)
void radiusDamage(GameLogic &logic, ObjectID sourceId, const DamageNugget &nugget, const WeaponTemplate &weapon, const Coord3D &center, unsigned long long *unported)
{
	const float radius = nugget.m_radius > 1.0f ? nugget.m_radius : 1.0f;
	const CombatNames::Kind &k = CombatNames::kinds();
	Object *source = sourceId != INVALID_ID ? logic.findObjectByID(sourceId) : nullptr;
	if (nugget.m_damageArc < 3.14159274f && unported)
	{
		++*unported; // DamageArc < pi needs the fcos of RW 0x42F4E0 (facade item): the arc test is not applied
	}
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->isKindOf((unsigned)k.inert) || o->isKindOf((unsigned)k.unattackable) || o->isDestroyed() || o->isEffectivelyDead() || !o->isInWorld())
		{
			continue; // lane GARRISON-1: a contain's rider out of the world (RW 0x68C18F) is not in the partition
		}
		const float d2 = CombatQueries::centerDistanceSquared2D(*o->getPosition(), center);
		const float reach = SimMath::addf32(radius, CombatQueries::boundingCircleRadius(*o));
		if (d2 > SimMath::mulf32(reach, reach))
		{
			continue;
		}
		if (!shouldDeliver(nugget, weapon, source, *o, logic))
		{
			continue;
		}
		if (nugget.m_minRadius > 0.0f && d2 < SimMath::mulf32(nugget.m_minRadius, nugget.m_minRadius))
		{
			continue;
		}
		const bool took = applyToVictim(logic, sourceId, nugget, weapon, *o, &center);
		if (took && 4.0f >= radius) // GameData DamageRadiusMinimumForSplash (RW +0xB40, retail 4.0): a small radius hurts one object
		{
			break;
		}
	}
}
} // namespace

unsigned DeliverNuggets(GameLogic &logic, ObjectID sourceId, const WeaponTemplate &weapon, const WeaponBonus &, Object *victim, const Coord3D *pos, bool, unsigned long long *unported)
{
	unsigned applied = 0;
	Object *source = sourceId != INVALID_ID ? logic.findObjectByID(sourceId) : nullptr;
	for (const std::shared_ptr<WeaponNugget> &np : weapon.m_nuggets)
	{
		const WeaponNugget &n = *np;
		if (n.kind() != NUGGET_DAMAGE)
		{
			if (unported)
			{
				++*unported;
			}
			continue;
		}
		const DamageNugget &dn = static_cast<const DamageNugget &>(n);
		if (dn.m_radius == 0.0f)
		{
			// RW 0x90DAFD: Radius == 0 delivers to the victim when shouldDeliver allows
			if (victim && CombatQueries::isAlive(*victim) && shouldDeliver(n, weapon, source, *victim, logic))
			{
				applied += applyToVictim(logic, sourceId, dn, weapon, *victim, nullptr) ? 1u : 0u;
			}
		}
		else
		{
			const Coord3D center = victim ? *victim->getPosition() : (pos ? *pos : Coord3D{ 0.0f, 0.0f, 0.0f });
			radiusDamage(logic, sourceId, dn, weapon, center, unported);
		}
	}
	return applied;
}

ObjectWeaponDelivery::ObjectWeaponDelivery(Object &source, ObjectWeapons &weapons, const WeaponTemplate &weapon)
	: m_source(&source)
	, m_weapons(&weapons)
	, m_weapon(&weapon)
{
}

// RW 0x6CC915 fireWeaponTemplate
void ObjectWeaponDelivery::fireWeaponTemplate(const WeaponBonus &bonus, int curBarrel, const WeaponShotTarget &target, bool scattered, const WeaponFireGate &gate)
{
	GameLogic &logic = m_source->logic();
	const WeaponTemplate &t = *m_weapon;
	GameLogicRandom &rng = logic.random();
	Object *victim = target.hasVictim ? logic.findObjectByID(target.victimID) : nullptr;
	Coord3D pos = target.victimPosition;
	ObjectWeapons::Stats &stats = m_weapons->statsMutable();
	// RW 0x6CCA2C .. 0x6CCAA1, the range gate: without argument 6 and past the leech-range deadline (Weapon + 0x50 > frame keeps it open) a target beyond the
	// maximum range that RW 0x6CC07C does not reach either, or inside the minimum range, returns before the fire FX, the projectile and the damage. The shot
	// is still spent: privateFireWeapon's tail (ammo, barrel, delay, reload) runs after this call whatever it did
	if (!gate.ignoreRanges && !(gate.leechRangeDeadline > logic.getFrame()) &&
		!m_weapons->deliveryRangeAllows(t, bonus, victim, victim ? *victim->getPosition() : target.victimPosition))
	{
		++stats.shotsOutOfRange;
		return;
	}
	++stats.shotsFired;
	if (victim)
	{
		++stats.shotsAtVictim;
		pos = *victim->getPosition();
	}
	emitFireFX(curBarrel, victim, pos);
	// the miss rolls (scratch/weapon1/damage.md 2.7, in this order)
	bool miss = false;
	if (victim && !scattered)
	{
		if (t.m_infantryInaccuracyDist > 0.0f && victim->isKindOf((unsigned)CombatNames::kinds().infantry) && !t.m_disableScatterForTargetsOnWall)
		{
			miss = true; // (1) no roll
		}
		if (t.m_hitPercentage < 1.0f)
		{
			const float r = rng.getValueReal(0.0f, 1.0f, kWeaponCpp, 1489); // (2)
			if (r >= t.m_hitPercentage)
			{
				miss = true;
			}
		}
		if (t.m_canBeDodged && victim->getBodyModule())
		{
			const float dodge = victim->getBodyModule()->getDodgePercent();
			if (dodge > 0.0f)
			{
				const float r = rng.getValueReal(0.0f, 1.0f, kWeaponCpp, 1495); // (3)
				if (dodge >= r)
				{
					miss = true;
				}
			}
		}
		// (4) HitPassengerPercentage: the victim's contain module (garrison) is not a passenger target in this lane (S-321)
	}
	if (miss)
	{
		// RW 0x6CC399 computeScatter: the position path, no victim
		float radius = t.m_scatterRadius;
		if (t.m_infantryInaccuracyDist > 0.0f && victim && victim->isKindOf((unsigned)CombatNames::kinds().infantry))
		{
			radius = SimMath::addf32(radius, t.m_infantryInaccuracyDist);
		}
		const float targetRadius = victim ? SimMath::addf32(CombatQueries::boundingCircleRadius(*victim), 5.0f) : 0.0f;
		if (radius > targetRadius)
		{
			radius = rng.getValueReal(targetRadius, radius, kWeaponCpp, 1625);
		}
		const Coord3D src = *m_source->getPosition();
		float angle;
		if (victim)
		{
			const float base = (float)SimMath::atan2d(SimMath::subf32(src.y, pos.y), SimMath::subf32(src.x, pos.x));
			const float half = 1.5707964f;
			angle = rng.getValueReal(SimMath::subf32(base, half), SimMath::addf32(base, half), kWeaponCpp, 1639);
		}
		else
		{
			angle = rng.getValueReal(0.0f, 6.28318548f, kWeaponCpp, 1643);
		}
		pos.x = SimMath::addf32(pos.x, SimMath::mulf32((float)SimMath::cosd(angle), radius));
		pos.y = SimMath::addf32(pos.y, SimMath::mulf32((float)SimMath::sind(angle), radius));
		pos.z = logic.getGroundHeight(pos.x, pos.y);
		victim = nullptr;
	}
	// the nugget loop in parse order
	for (const std::shared_ptr<WeaponNugget> &np : t.m_nuggets)
	{
		const WeaponNugget &n = *np;
		switch (n.kind())
		{
		case NUGGET_DAMAGE:
		{
			const DamageNugget &dn = static_cast<const DamageNugget &>(n);
			if (dn.m_radius == 0.0f)
			{
				if (victim && shouldDeliver(n, t, m_source, *victim, logic))
				{
					applyToVictim(logic, m_source->getID(), dn, t, *victim, nullptr);
				}
			}
			else
			{
				const Coord3D center = victim ? *victim->getPosition() : pos;
				radiusDamage(logic, m_source->getID(), dn, t, center, &logic.combat().counters().unportedNuggets);
			}
			break;
		}
		case NUGGET_PROJECTILE:
		{
			const ProjectileNugget &pn = static_cast<const ProjectileNugget &>(n);
			if (!upgradeTest(n, m_source))
			{
				break;
			}
			ProjectileShot shot;
			shot.source = m_source->getID();
			shot.victim = victim ? victim->getID() : (ObjectID)INVALID_ID;
			shot.sourcePosition = *m_source->getPosition();
			shot.targetPosition = pos;
			shot.weapon = &t;
			shot.warhead = TheWeaponStore ? TheWeaponStore->findWeaponTemplate(pn.m_warheadTemplateName) : nullptr;
			shot.bonus = bonus;
			shot.barrel = curBarrel;
			shot.slot = m_weapons->curSlot();
			shot.nugget = &pn;
			if (pn.m_useAlwaysAttackOffset)
			{
				// RW 0x90FC99 (slot 6): the target is the AlwaysAttackHereOffset in the source's frame, whatever the victim is: ((m2*oz + m1*oy) + m0*ox) + t per row
				const float *b = m_source->getBasis();
				const Coord3D &o = pn.m_alwaysAttackHereOffset;
				const Coord3D &sp = *m_source->getPosition();
				auto row = [&](int r, float t0) {
					const float s = SimMath::addf32(SimMath::mulf32(b[r * 3 + 2], o.z), SimMath::mulf32(b[r * 3 + 1], o.y));
					return SimMath::addf32(SimMath::addf32(s, SimMath::mulf32(b[r * 3], o.x)), t0);
				};
				shot.targetPosition = Coord3D{ row(0, sp.x), row(1, sp.y), row(2, sp.z) };
				shot.victim = INVALID_ID;
			}
			if (!shot.warhead)
			{
				logic.reportError("weapon " + t.getName() + ": the ProjectileNugget's warhead '" + pn.m_warheadTemplateName + "' is not in the WeaponStore");
			}
			++stats.projectilesLaunched;
			logic.combat().launcher().launch(logic, shot);
			break;
		}
		case NUGGET_HORDE_ATTACK:
		{
			// RW 0x241F10 (the HordeAttackNugget's fire path, inferred): the ranks released when attacking shoot at the victim's horde
			if (!upgradeTest(n, m_source))
			{
				break;
			}
			if (HordeAIUpdate *h = dynamic_cast<HordeAIUpdate *>(m_source->getAIUpdateInterface()))
			{
				if (victim)
				{
					h->releaseMembersToAttack(*victim);
				}
			}
			break;
		}
		default:
			++logic.combat().counters().unportedNuggets;
			break;
		}
	}
}

// RW 0x6CB85A(out, source, victim, flag 1), the aim query of the fire FX block (lane FX-2 review). TARGET FACTS: RW 0x6CB81A first asks the victim for the
// weapon's PreferredTargetBone (+0x90): a non-empty name found exactly once among the victim drawable's pristine bones (RW 0x68C5AF -> 0x672A73, then
// 0x70BCE7 to world) is the answer with no draw; otherwise GameLogicRandomValue(0, 0xBC614E = 12345678) is drawn (Weapon.cpp:1749, RW 0x6CB8C2..0x6CB8D3)
// and handed as the seed to RW 0x690BD2, the victim geometry's contact point. The contact point is stop S-362's approximation (the geometry centre, the seed
// unused) exactly as BezierProjectileBehavior::aimPosition has it. The bone answer comes from the launch bone provider (template data and logic state);
// without one (a GameLogic without the W3D assets) no bone is found.
Coord3D ObjectWeaponDelivery::fireFXAimPosition(const Object &victim)
{
	GameLogic &logic = m_source->logic();
	const std::string &boneName = m_weapon->m_preferredTargetBone;
	if (!boneName.empty())
	{
		float bone[12];
		ProjectileLaunchOffsets *provider = logic.combat().launchOffsets();
		if (provider && provider->singleLogicalBone(victim, boneName, bone))
		{
			// RW 0x70BCE7 convertBonePosToWorldPos: the victim's transform applied to the bone's translation, retail's SSE order
			const float *t = victim.getBasis();
			const Coord3D &p = *victim.getPosition();
			const float tt[3] = { p.x, p.y, p.z };
			float w[3];
			for (int r = 0; r < 3; ++r)
			{
				const float v = SimMath::addf32(SimMath::mulf32(t[r * 3 + 2], bone[11]), SimMath::mulf32(t[r * 3 + 1], bone[7]));
				w[r] = SimMath::addf32(SimMath::addf32(v, SimMath::mulf32(t[r * 3 + 0], bone[3])), tt[r]);
			}
			return Coord3D{ w[0], w[1], w[2] };
		}
	}
	const int seed = logic.random().getValue(0, 12345678, kWeaponCpp, 1749);
	(void)seed; // RW 0x690BD2's aim point choice (S-362)
	Coord3D aim = *victim.getPosition();
	aim.z = SimMath::addf32(aim.z, SimMath::mulf32(CombatQueries::geometry(victim).height, 0.5f));
	return aim;
}

// RW 0x6CC915, the fire FX block (0x6CCAA7..0x6CCBF6; lane FX-2). It runs when the source has a drawable (RW 0x70E013): every object has one in retail
// (RW 0x628882 makes it at creation; DrawableManager likewise), so the block is taken for every shot whatever the client shows; eligibility is never the
// player's. First the victim aim query (above: the logic draw of Weapon.cpp:1749 unless the preferred bone answers), BEFORE anything can return. Then the FX:
// FireFlankFX (+0xB4) when the victim is flanked by the source (RW 0x68FB63; the flank test is not ported, S-322: never) else FireFX (+0xA4, the REGULAR
// entry: RW reads no veterancy entry here), none while the logic frame is below the weapon's suspendFXFrame (+0x30). The local-player / stealth tests
// (RW 0x68B749, 0x694C0D, +0x135) belong to the viewing client: the event carries PlayFXWhenStealthed.
void ObjectWeaponDelivery::emitFireFX(int curBarrel, const Object *victim, const Coord3D &victimPos)
{
	GameLogic &logic = m_source->logic();
	const Coord3D aim = victim ? fireFXAimPosition(*victim) : victimPos;
	const WeaponTemplate &t = *m_weapon;
	const Weapon *w = m_weapons->weaponInSlot(m_weapons->curSlot());
	if (w && w->getTemplate() == &t && logic.getFrame() < w->suspendFXFrame())
	{
		return;
	}
	if (!FXEventLog::isFXName(t.m_fireFXs[0]))
	{
		return;
	}
	FXEvent e = FXEventLog::objectEvent(FXEvent::WEAPON_FIRE_FX, "FireFX", logic.getFrame(), t.m_fireFXs[0], *m_source);
	e.secondary = victim ? victim->getID() : (ObjectID)INVALID_ID;
	e.secondaryPosition = aim;
	e.weaponSlot = m_weapons->curSlot();
	e.barrel = curBarrel;
	e.weaponSpeed = t.m_weaponSpeed;
	e.playWhenStealthed = t.m_playFXWhenStealthed;
	logic.fxEvents().emit(e);
}

// RW 0x6CB7BD: a projectile detonation delivers the nuggets of the template directly (the warhead path of DeliverNuggets)
void ObjectWeaponDelivery::fireProjectileDetonation(const WeaponBonus &bonus, const WeaponShotTarget &target)
{
	GameLogic &logic = m_source->logic();
	Object *victim = target.hasVictim ? logic.findObjectByID(target.victimID) : nullptr;
	DeliverNuggets(logic, m_source->getID(), *m_weapon, bonus, victim, &target.victimPosition, true, &logic.combat().counters().unportedNuggets);
}

// RW 0x6CAB32 processRequestAssistance: RequestAssistRange (retail data: a few weapons) asks allies to help; the AI lane's reaction is not ported (S-325)
void ObjectWeaponDelivery::requestAssistance(const WeaponShotTarget &)
{
}
